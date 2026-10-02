# Newton seeding variants against the uniform reference, at the user's view re=1.4, im=0,
# span 0.0148875. Goal: Newton seeds distributed like uniform ones (the walk look), cheaply.
import numpy as np, time
from seedlib import evaluate, inside_main, stats, boundary_pool, newton as solve

rng = np.random.default_rng(4)

t = time.time()
N = 30_000_000
cu = rng.uniform(-2, 0.5, N) + 1j * rng.uniform(-1.2, 1.2, N)
cu = cu[~inside_main(cu)]
good, s = stats(cu, *evaluate(cu))
print(f"{'uniform reference':34s} good {good.sum():5d} of {N}: {s}", flush=True)

pool = boundary_pool(rng)
newton = lambda starts, nlo, nhi: solve(rng, starts, nlo, nhi)

boundary = lambda M: rng.choice(pool, M)
anywhere = lambda M: rng.uniform(-2, 0.5, M) + 1j * rng.uniform(-1.2, 1.2, M)
for name, starts, nlo, nhi, reject in [
    ("A boundary, n 10-40 (current)", boundary, 10, 40, None),
    ("B boundary, n 0-64", boundary, 0, 64, None),
    ("C anywhere, n 0-64", anywhere, 0, 64, None),
    ("D anywhere, n 0-64, reject |d|^-2", anywhere, 0, 64, 10.0),
    ("E boundary, n 0-64, reject |d|^-2", boundary, 0, 64, 10.0),
]:
    c, dn, conv, steps = newton(starts, nlo, nhi)
    keep = conv.copy()
    if reject:
        keep &= rng.uniform(0, 1, len(c)) < np.minimum(1, (reject / np.maximum(dn, 1e-30)) ** 2)
    c = c[keep]
    good, s = stats(c, *evaluate(c))
    print(
        f"{name:34s} good {good.sum():5d} of 20000 attempts ({steps / max(1, good.sum()):8.0f} solve steps/good): {s}",
        flush=True,
    )
print(f"({time.time() - t:.0f}s)")
