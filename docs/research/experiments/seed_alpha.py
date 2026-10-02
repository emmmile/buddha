# Sweep the rejection exponent: keep a Newton seed with probability min(1, (10 / |dz_n/dc|)^alpha).
# Reference (seed_variants.py, uniform seeds): antenna 81%, cardioid-side 15%,
# k<10/10-39/40+ 29/35/36%, L p50/p90 46/170, log|d| p50 1.8.
import numpy as np
from seedlib import evaluate, stats, boundary_pool, newton

rng = np.random.default_rng(5)
pool = boundary_pool(rng)
starts = {
    "anywhere": lambda M: rng.uniform(-2, 0.5, M) + 1j * rng.uniform(-1.2, 1.2, M),
    "boundary": lambda M: rng.choice(pool, M),
}
for sname, start in starts.items():
    c0, dn0, conv0, steps = newton(rng, start, 0, 64, M=40000)
    for alpha in [0.5, 1.0, 1.5]:
        keep = conv0 & (
            rng.uniform(0, 1, len(c0)) < np.minimum(1, (10.0 / np.maximum(dn0, 1e-30)) ** alpha)
        )
        c = c0[keep]
        good, s = stats(c, *evaluate(c))
        print(
            f"{sname:8s} alpha {alpha}: good {good.sum():5d} of 40000 ({steps / max(1, good.sum()):8.0f} solve steps/good): {s}",
            flush=True,
        )
