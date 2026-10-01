// Display pipeline: channel maxima, tone curve, then the adjustments of buddha_image::adjust in
// core/image_pipeline.h (contrast, saturation, brightness, then clarity and texture from blurred
// luminance). Buffers are in display order: pixel (x, y) at y * width + x.

struct Display {
  width: u32,
  height: u32,
  gamma: f32,
  exposure: f32,
  brightness: f32, // midtone gain, exp2(brightness / 50)
  contrast: f32, // slope about the midpoint, 1 + contrast / 100
  saturation: f32, // factor about Rec. 709 luminance, 1 + saturation / 100
  clarity: f32, // clarity / 100
  texture: f32, // texture / 100
  pad0: f32,
  pad1: f32,
  pad2: f32,
}

const LUMA = vec3f(0.2126, 0.7152, 0.0722);

@group(0) @binding(0) var<uniform> d: Display;
@group(0) @binding(1) var<storage, read> counts: array<u32>;

// ---- channel maxima ----

@group(0) @binding(2) var<storage, read_write> maxima: array<atomic<u32>, 3>;

var<workgroup> local_max: array<atomic<u32>, 3>;

@compute @workgroup_size(256)
fn reduce(
  @builtin(global_invocation_id) gid: vec3u,
  @builtin(local_invocation_index) lid: u32,
) {
  let pixel = gid.x;
  if (pixel < d.width * d.height) {
    atomicMax(&local_max[0], counts[pixel * 3u]);
    atomicMax(&local_max[1], counts[pixel * 3u + 1u]);
    atomicMax(&local_max[2], counts[pixel * 3u + 2u]);
  }
  workgroupBarrier();
  if (lid < 3u) {
    atomicMax(&maxima[lid], atomicLoad(&local_max[lid]));
  }
}

// ---- tone curve and per-pixel adjustments ----

@group(0) @binding(3) var<storage, read> peaks: array<u32, 3>;
@group(0) @binding(4) var<storage, read_write> color: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> luma: array<f32>;

// Endpoints stay fixed: unlike exposure, bright values bend toward white.
fn lift_midtones(value: vec3f, gain: f32) -> vec3f {
  return gain * value / (1.0 + (gain - 1.0) * value);
}

@compute @workgroup_size(256)
fn tone(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= d.width * d.height) {
    return;
  }
  let k = vec3f(f32(counts[i * 3u]), f32(counts[i * 3u + 1u]), f32(counts[i * 3u + 2u]));
  let m = max(vec3f(f32(peaks[0]), f32(peaks[1]), f32(peaks[2])), vec3f(1.0));
  let curve = select(pow(k / m, vec3f(d.gamma)), vec3f(0.0), k == vec3f(0.0));
  let base = clamp(curve * d.exposure, vec3f(0.0), vec3f(1.0));

  let channels = (base - 0.5) * d.contrast + 0.5;
  let gray = dot(channels, LUMA);
  let saturated = clamp(gray + (channels - gray) * d.saturation, vec3f(0.0), vec3f(1.0));
  let out = lift_midtones(saturated, d.brightness);
  color[i] = vec4f(out, 1.0);
  luma[i] = dot(out, LUMA);
}

// ---- box blur: one axis of one pass of smooth_blur ----

struct Blur {
  width: u32,
  height: u32,
  radius: u32,
  vertical: u32,
}

@group(0) @binding(6) var<uniform> b: Blur;
@group(0) @binding(7) var<storage, read> blur_in: array<f32>;
@group(0) @binding(8) var<storage, read_write> blur_out: array<f32>;

// Boxes are clipped and renormalized at the edges, as on the CPU.
@compute @workgroup_size(256)
fn blur(@builtin(global_invocation_id) gid: vec3u) {
  let i = gid.x;
  if (i >= b.width * b.height) {
    return;
  }
  let x = i % b.width;
  let y = i / b.width;
  let along = select(x, y, b.vertical != 0u);
  let size = select(b.width, b.height, b.vertical != 0u);
  let stride = select(1u, b.width, b.vertical != 0u);
  let first = along - min(along, b.radius);
  let last = min(size - 1u, along + b.radius);
  var sum = 0.0;
  for (var j = first; j <= last; j++) {
    sum += blur_in[i - along * stride + j * stride];
  }
  blur_out[i] = sum / f32(last - first + 1u);
}

// ---- clarity, texture and output ----

@group(0) @binding(9) var<storage, read> shade_color: array<vec4f>;
@group(0) @binding(10) var<storage, read> shade_luma: array<f32>;
@group(0) @binding(11) var<storage, read> broad: array<f32>;
@group(0) @binding(12) var<storage, read> fine: array<f32>;

@vertex
fn fullscreen(@builtin(vertex_index) v: u32) -> @builtin(position) vec4f {
  let uv = vec2f(f32((v << 1u) & 2u), f32(v & 2u));
  return vec4f(uv * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn shade(@builtin(position) position: vec4f) -> @location(0) vec4f {
  let i = u32(position.y) * d.width + u32(position.x);
  let l = shade_luma[i];
  let midtone = 4.0 * l * (1.0 - l);
  var delta = 0.0;
  if (d.clarity != 0.0) {
    delta += d.clarity * (l - broad[i]) * midtone * midtone;
  }
  if (d.texture != 0.0) {
    delta += d.texture * (l - fine[i]) * midtone;
  }
  return vec4f(clamp(shade_color[i].rgb + delta, vec3f(0.0), vec3f(1.0)), 1.0);
}
