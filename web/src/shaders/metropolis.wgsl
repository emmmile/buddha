// WGSL port of the Metropolis chains in core/buddha_kernel.h, with a naive mode: uniform samples
// over [-2, 2]^2, each escaping one drawn once and no chain, as the naive lane there.
//
// Differences from the native kernel:
// - no symmetric folding: the histogram covers the whole view;
// - the histogram is stored already rotated as the TIFF output is: x follows Im, y follows Re;
// - totals are summed over the lanes of a dispatch into one buffer, which the host reads and
//   clears after each dispatch. The counters are those of buddha_kernel::totals, so the metrics
//   match the Metal sampler's (metropolis in metal/render.metal).

struct Params {
  lowr: u32,
  highr: u32,
  lowg: u32,
  highg: u32,
  lowb: u32,
  highb: u32,
  low: u32, // min of the channel lows
  high: u32, // max of the channel highs
  width: u32,
  height: u32,
  threads: u32,
  steps: u32, // advance() calls per lane in a dispatch
  seeding: u32, // 0: random walk from the origin, 1: uniform over [-2, 2]^2, 2: Newton
  key: u32, // seeds the chains in begin
  exclusion_size: u32, // the exclusion map has exclusion_size x exclusion_size / 2 cells
  boundary_count: u32, // cells in boundary
  minre: f32, // Re at the top edge
  minim: f32, // Im at the left edge
  scale: f32, // pixels per unit
  radius: f32, // mutation radius, in complex units
  exponent_l: f32, // target density L^a * C^b
  exponent_c: f32,
  chain_scale: f32, // proposals per chain: chain_scale * max(256 C, 2 L)
  sampler: u32, // METROPOLIS or NAIVE
}

struct Chain {
  mode: u32,
  random: u32, // generator state
  i: u32,
  last: u32,
  contribute: u32,
  cr: f32, // orbit being evaluated or redrawn
  ci: f32,
  zr: f32,
  zi: f32,
  period_re: f32,
  period_im: f32,
  period_step: u32,
  current_r: f32, // chain state
  current_i: f32,
  current_log: f32, // a ln L + b ln C of the chain state
  left: u32, // proposals left in the chain
  walk_r: f32,
  walk_i: f32,
  walk_steps: u32,
  pad: u32,
  // Newton seeding: c (in cr, ci) is solved for z_n(c) = target, with z in zr, zi, the
  // derivative dz/dc in dr, di and the step in i.
  target_r: f32,
  target_i: f32,
  dr: f32,
  di: f32,
  solve_n: u32,
  solve_round: u32,
}

@group(0) @binding(0) var<uniform> p: Params;
@group(0) @binding(1) var<storage, read_write> histogram: array<atomic<u32>>;
// data/exclusion.map cells, one bit each: cell i in bit i % 32 of word i / 32.
@group(0) @binding(4) var<storage, read> exclusion: array<u32>;
// Exclusion map cells that are not excluded but have an excluded neighbour (see exclusion.ts):
// Newton seeding starts from points in them, near the boundary of the set.
@group(0) @binding(5) var<storage, read> boundary: array<u32>;
@group(0) @binding(2) var<storage, read_write> chains: array<Chain>;
@group(0) @binding(3) var<storage, read_write> stats: array<atomic<u32>, 9>;

// Counters of one lane in one dispatch, as in buddha_kernel::totals. Stats holds their sums in
// this order.
struct Totals {
  seeds: u32, // seed candidates tried
  proposals: u32,
  accepted: u32,
  chains: u32, // chains started
  escaped: u32, // proposals drawn
  increments: u32, // histogram channel increments
  iterations: u32, // counted recurrence steps in the escape/periodicity pass
  redraw: u32, // steps spent re-iterating orbits to draw them
  solve: u32, // orbit steps spent in Newton seeding
}

const SEEK = 0u;
const PROPOSE = 1u;
const EVALUATE_SEED = 2u;
const EVALUATE_PROPOSAL = 3u;
const REDRAW = 4u;
const SOLVE = 5u;
const METROPOLIS = 0u;
const NAIVE = 1u;
const WALK_LIMIT = 256u;
// Newton seeding solves z_n(c) = w for a point w in the window, with n in [SOLVE_MIN, SOLVE_MAX)
// and at most SOLVE_ROUNDS Newton steps. Larger n gives roots nearer the boundary, which escape
// later, but converges less often (docs/SEEDING_AND_ZOOM_RESEARCH.md).
const SOLVE_MIN = 0u;
const SOLVE_MAX = 64u;
// Share of Newton starts on the antenna (the real axis in [-2, -1.4]) rather than in boundary
// cells: the exclusion map has no interior beside the antenna, so its cells never start there.
const SOLVE_ANTENNA = 0.5;
const SOLVE_ROUNDS = 24u;
const SOLVE_STEP = 0.5; // largest Newton step, in complex units
// Attempts are abandoned before f32 overflows: z beyond 1e8 has escaped, and dz/dc reaches about
// 4^n near the boundary, so it is only bounded per component.
const SOLVE_ESCAPE = 1e16; // |z|^2
const SOLVE_DERIVATIVE = 1e29; // max(|Re dz/dc|, |Im dz/dc|)
const EPSILON2: f32 = 1.4210855e-14; // FLT_EPSILON^2
const LN_2_24: f32 = 16.6355323; // 24 ln 2
const NO_PIXEL = 0xffffffffu;

var<private> c: Chain;
var<private> t: Totals;

fn mix32(v: u32) -> u32 {
  var value = v;
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  return value ^ (value >> 16u);
}

// Natural logarithm of an integer n >= 1 (single-precision Cephes polynomial).
fn log_uint(n: u32) -> f32 {
  var e = 31 - i32(countLeadingZeros(n));
  let top = select(n << u32(23 - e), n >> u32(e - 23), e > 23);
  var m = f32(top) * (1.0 / 8388608.0);
  if (m > 1.41421356) {
    m = m * 0.5;
    e += 1;
  }
  let x = m - 1.0;
  let z = x * x;
  var y = 7.0376836292e-2;
  y = y * x - 1.1514610310e-1;
  y = y * x + 1.1676998740e-1;
  y = y * x - 1.2420140846e-1;
  y = y * x + 1.4249322787e-1;
  y = y * x - 1.6668057665e-1;
  y = y * x + 2.0000714765e-1;
  y = y * x - 2.4999993993e-1;
  y = y * x + 3.3333331174e-1;
  y = y * x * z;
  let fe = f32(e);
  y = y - 2.12194440e-4 * fe;
  y = y - 0.5 * z;
  return x + y + 0.693359375 * fe;
}

// Direction at a uniform angle, from a 24-bit random value: the top two bits pick a quadrant and
// the rest an angle in [-pi/4, pi/4) within it.
fn direction(bits: u32) -> vec2f {
  let f = f32(bits & 0x3fffffu) * (1.0 / 4194304.0);
  let x = (f - 0.5) * 1.57079632679;
  let z = x * x;
  var s = -1.9515295891e-4;
  s = s * z + 8.3321608736e-3;
  s = s * z - 1.6666654611e-1;
  s = s * z * x + x;
  var k = 2.443315711809948e-5;
  k = k * z - 1.388731625493765e-3;
  k = k * z + 4.166664568298827e-2;
  k = k * z * z - 0.5 * z + 1.0;
  switch ((bits >> 22u) & 3u) {
    case 0u: {
      return vec2f(k, s);
    }
    case 1u: {
      return vec2f(-s, k);
    }
    case 2u: {
      return vec2f(-k, -s);
    }
    default: {
      return vec2f(s, -k);
    }
  }
}

fn next24() -> u32 {
  c.random = c.random * 747796405u + 2891336453u;
  return mix32(c.random) >> 8u;
}

fn uniform01() -> f32 {
  return f32(next24()) * (1.0 / 16777216.0);
}

// -ln(u) for u uniform in (0, 1].
fn exponential1() -> f32 {
  return LN_2_24 - log_uint(next24() + 1u);
}

// Whether c is marked as inside the set (exclusion_cell and excluded in buddha_kernel.h). The map
// covers [-2, 2] x [-2, 0], mirrored.
fn excluded(cr: f32, ci: f32) -> bool {
  let size = f32(p.exclusion_size);
  let x = i32(cr * size / 4.0 + size / 2.0);
  let y = i32(-abs(ci) * size / 4.0 + size / 2.0);
  if (x < 0 || x >= i32(p.exclusion_size) || y < 0 || y >= i32(p.exclusion_size / 2u)) {
    return false;
  }
  let cell = u32(y) * p.exclusion_size + u32(x);
  return ((exclusion[cell / 32u] >> (cell % 32u)) & 1u) != 0u;
}

// Histogram index of the red count of a point, or NO_PIXEL outside the view.
fn pixel(re: f32, im: f32) -> u32 {
  let fx = (im - p.minim) * p.scale;
  let fy = (re - p.minre) * p.scale;
  if (!(fx >= 0.0 && fx < f32(p.width) && fy >= 0.0 && fy < f32(p.height))) {
    return NO_PIXEL;
  }
  return (u32(fy) * p.width + u32(fx)) * 3u;
}

fn in_channel(i: u32, low: u32, high: u32) -> bool {
  return i > low && i < high;
}

fn draw(index: u32, i: u32) {
  if (in_channel(i, p.lowr, p.highr)) {
    atomicAdd(&histogram[index], 1u);
    t.increments += 1u;
  }
  if (in_channel(i, p.lowg, p.highg)) {
    atomicAdd(&histogram[index + 1u], 1u);
    t.increments += 1u;
  }
  if (in_channel(i, p.lowb, p.highb)) {
    atomicAdd(&histogram[index + 2u], 1u);
    t.increments += 1u;
  }
}

fn start(r: f32, im: f32) {
  c.cr = r;
  c.zr = r;
  c.ci = im;
  c.zi = im;
  c.i = 0u;
  c.contribute = 0u;
  c.period_step = 8u;
}

fn step(rr: f32, ii: f32) {
  let next = rr - ii + c.cr;
  c.zi = 2.0 * c.zr * c.zi + c.ci;
  c.zr = next;
}

// Periodicity check: from step 8 on, z_i is compared with a checkpoint, which moves to steps 16,
// 32, 64, ... An orbit that returns to its checkpoint is periodic.
fn cyclic() -> bool {
  if (c.i == 8u) {
    c.period_re = c.zr;
    c.period_im = c.zi;
  } else if (c.i > c.period_step) {
    let dr = c.zr - c.period_re;
    let di = c.zi - c.period_im;
    if (dr * dr + di * di < EPSILON2) {
      return true;
    }
    if (c.i == c.period_step * 2u) {
      c.period_step *= 2u;
      c.period_re = c.zr;
      c.period_im = c.zi;
    }
  }
  return false;
}

// Picks the next seed candidate outside the excluded region; false if none was found here.
fn pick_seed() -> bool {
  var r: f32;
  var im: f32;
  if (p.seeding == 1u || p.sampler == NAIVE) {
    r = uniform01() * 4.0 - 2.0;
    im = uniform01() * 4.0 - 2.0;
  } else {
    if (c.walk_steps == WALK_LIMIT) {
      c.walk_r = 0.0;
      c.walk_i = 0.0;
      c.walk_steps = 0u;
    }
    // |N(0, 1)| approximated by four uniforms (Irwin-Hall).
    let sum = uniform01() + uniform01() + uniform01() + uniform01();
    let amplitude = abs(sum - 2.0) * 1.73205081;
    let d = direction(next24());
    c.walk_r += amplitude * d.x;
    c.walk_i += amplitude * d.y;
    c.walk_steps += 1u;
    r = c.walk_r;
    im = c.walk_i;
  }
  t.seeds += 1u;
  if (excluded(r, im)) {
    return false;
  }
  start(r, im);
  return true;
}

// A point near the boundary of the set: on the antenna (SOLVE_ANTENNA of the time), or uniform in a
// random boundary cell, on either side of the real axis (the map covers Im <= 0, mirrored).
// Uniform over [-2, 2]^2 if there are no boundary cells.
fn boundary_point() -> vec2f {
  if (uniform01() < SOLVE_ANTENNA) {
    return vec2f(-2.0 + 0.6 * uniform01(), 0.02 * (uniform01() - 0.5));
  }
  if (p.boundary_count == 0u) {
    return vec2f(uniform01() * 4.0 - 2.0, uniform01() * 4.0 - 2.0);
  }
  let cell = boundary[min(u32(uniform01() * f32(p.boundary_count)), p.boundary_count - 1u)];
  let size = f32(p.exclusion_size);
  let x = f32(cell % p.exclusion_size);
  let y = f32(cell / p.exclusion_size);
  let r = (x + uniform01()) * 4.0 / size - 2.0;
  let im = (size / 2.0 - y - uniform01()) * 4.0 / size;
  return vec2f(r, select(im, -im, (next24() & 1u) != 0u));
}

// Starts a Newton seed attempt: a random target in the window, a step count and a start point near
// the boundary.
fn begin_solve() {
  t.seeds += 1u;
  c.target_r = p.minre + uniform01() * f32(p.height) / p.scale;
  c.target_i = p.minim + uniform01() * f32(p.width) / p.scale;
  c.solve_n = SOLVE_MIN + u32(uniform01() * f32(SOLVE_MAX - SOLVE_MIN));
  c.solve_round = 0u;
  let start_point = boundary_point();
  c.cr = start_point.x;
  c.ci = start_point.y;
  c.zr = c.cr;
  c.zi = c.ci;
  c.dr = 1.0;
  c.di = 0.0;
  c.i = 0u;
  c.mode = SOLVE;
}

// One orbit step of a Newton seed attempt. After solve_n steps, either z_n is within half a pixel
// of the target and c becomes a seed candidate, or c takes a Newton step and the orbit restarts.
fn solve() {
  t.solve += 1u;
  if (c.i < c.solve_n) {
    // d_{k+1} = 2 z_k d_k + 1, then z_{k+1} = z_k^2 + c.
    let dr = 2.0 * (c.zr * c.dr - c.zi * c.di) + 1.0;
    c.di = 2.0 * (c.zr * c.di + c.zi * c.dr);
    c.dr = dr;
    step(c.zr * c.zr, c.zi * c.zi);
    c.i += 1u;
    if (
      c.zr * c.zr + c.zi * c.zi > SOLVE_ESCAPE ||
      max(abs(c.dr), abs(c.di)) > SOLVE_DERIVATIVE
    ) {
      c.mode = SEEK;
    }
    return;
  }
  let er = c.zr - c.target_r;
  let ei = c.zi - c.target_i;
  let tolerance = 0.5 / p.scale;
  if (er * er + ei * ei < tolerance * tolerance) {
    if (excluded(c.cr, c.ci)) {
      c.mode = SEEK;
      return;
    }
    start(c.cr, c.ci);
    c.mode = EVALUATE_SEED;
    return;
  }
  c.solve_round += 1u;
  let m = max(abs(c.dr), abs(c.di));
  if (c.solve_round == SOLVE_ROUNDS || !(m > 0.0)) {
    c.mode = SEEK;
    return;
  }
  // (z - w) / d, scaled by m so that |d|^2 cannot overflow, and clamped to SOLVE_STEP.
  let dr = c.dr / m;
  let di = c.di / m;
  let d2 = (dr * dr + di * di) * m;
  var sr = (er * dr + ei * di) / d2;
  var si = (ei * dr - er * di) / d2;
  let norm = sqrt(sr * sr + si * si);
  if (norm > SOLVE_STEP) {
    sr *= SOLVE_STEP / norm;
    si *= SOLVE_STEP / norm;
  }
  c.cr -= sr;
  c.ci -= si;
  if (!(c.cr * c.cr + c.ci * c.ci < 16.0)) {
    c.mode = SEEK;
    return;
  }
  c.zr = c.cr;
  c.zi = c.ci;
  c.dr = 1.0;
  c.di = 0.0;
  c.i = 0u;
}

// One orbit step of the chain (see chain::advance in buddha_kernel.h).
fn advance() {
  if (c.mode == SEEK && p.sampler == METROPOLIS && p.seeding == 2u) {
    begin_solve();
  }
  if (c.mode == SOLVE) {
    solve();
    return;
  }
  if (c.mode == SEEK) {
    if (!pick_seed()) {
      return;
    }
    c.mode = EVALUATE_SEED;
  } else if (c.mode == PROPOSE) {
    if (c.left == 0u) {
      c.mode = SEEK;
      return;
    }
    c.left -= 1u;
    t.proposals += 1u;
    let amplitude = exponential1() * 0.5 * uniform01() * p.radius;
    let d = direction(next24());
    let r = c.current_r + amplitude * d.x;
    let im = c.current_i + amplitude * d.y;
    if (excluded(r, im)) {
      return;
    }
    start(r, im);
    c.mode = EVALUATE_PROPOSAL;
  }

  let rr = c.zr * c.zr;
  let ii = c.zi * c.zi;
  if (c.mode == REDRAW) {
    // The escape test only guards a diverged redraw.
    if (c.i > c.last || rr + ii > 8.0) {
      t.redraw += c.i;
      c.mode = select(PROPOSE, SEEK, p.sampler == NAIVE);
      return;
    }
    if (c.i >= p.low) {
      let index = pixel(c.zr, c.zi);
      if (index != NO_PIXEL) {
        draw(index, c.i);
      }
    }
    step(rr, ii);
    c.i += 1u;
    return;
  }

  // Evaluating a seed or a proposal: escape, periodicity and points in the window.
  if (pixel(c.zr, c.zi) != NO_PIXEL) {
    c.contribute += 1u;
  }
  var ended = false;
  var escaped_orbit = false;
  if (rr + ii > 8.0) {
    ended = true;
    escaped_orbit = true;
  } else if (cyclic()) {
    ended = true;
  } else {
    step(rr, ii);
    c.i += 1u;
    ended = c.i == p.high;
  }
  if (!ended) {
    return;
  }
  t.iterations += c.i;

  // Log density of the orbit, or invalid if it cannot be drawn.
  let valid = escaped_orbit && c.i >= 2u && c.contribute != 0u;
  var log_density = 0.0;
  if (valid) {
    log_density = p.exponent_l * log_uint(c.i - 1u) + p.exponent_c * log_uint(c.contribute);
  }
  if (c.mode == EVALUATE_SEED) {
    if (!valid) {
      c.mode = SEEK;
      return;
    }
    // A naive sample is drawn once, and the lane takes the next one.
    if (p.sampler == NAIVE) {
      begin_redraw();
      return;
    }
    c.current_r = c.cr;
    c.current_i = c.ci;
    c.current_log = log_density;
    c.left = u32(p.chain_scale * max(f32(c.contribute) * 256.0, f32(c.i - 1u) * 2.0));
    c.walk_r = 0.0; // the next seed search starts again from the origin
    c.walk_i = 0.0;
    c.walk_steps = 0u;
    t.chains += 1u;
    c.mode = PROPOSE;
    return;
  }

  if (!valid) {
    c.mode = PROPOSE;
    return;
  }
  // Accept with probability min(1, f' / f), in logs: ln u < ln f' - ln f.
  let log_u = log_uint(next24() + 1u) - LN_2_24;
  if (log_u <= log_density - c.current_log) {
    c.current_r = c.cr;
    c.current_i = c.ci;
    c.current_log = log_density;
    t.accepted += 1u;
  }
  begin_redraw();
}

// Starts drawing the orbit just evaluated: it is iterated again from c, up to its last step.
fn begin_redraw() {
  t.escaped += 1u;
  c.last = min(c.i - 1u, p.high - 1u);
  c.zr = c.cr;
  c.zi = c.ci;
  c.i = 0u;
  c.mode = REDRAW;
}

// Starts one chain per lane, seeded from p.key.
@compute @workgroup_size(64)
fn begin(@builtin(global_invocation_id) gid: vec3u) {
  let tid = gid.x;
  if (tid >= p.threads) {
    return;
  }
  var fresh: Chain;
  fresh.mode = SEEK;
  fresh.random = mix32((tid * 0x9e3779b9u) ^ mix32(p.key));
  fresh.period_step = 8u;
  chains[tid] = fresh;
}

// Advances every chain p.steps orbit steps and saves it for the next dispatch.
@compute @workgroup_size(64)
fn metropolis(@builtin(global_invocation_id) gid: vec3u) {
  let tid = gid.x;
  if (tid >= p.threads) {
    return;
  }
  c = chains[tid];
  for (var s = 0u; s < p.steps; s++) {
    advance();
  }
  chains[tid] = c;
  atomicAdd(&stats[0], t.seeds);
  atomicAdd(&stats[1], t.proposals);
  atomicAdd(&stats[2], t.accepted);
  atomicAdd(&stats[3], t.chains);
  atomicAdd(&stats[4], t.escaped);
  atomicAdd(&stats[5], t.increments);
  atomicAdd(&stats[6], t.iterations);
  atomicAdd(&stats[7], t.redraw);
  atomicAdd(&stats[8], t.solve);
}
