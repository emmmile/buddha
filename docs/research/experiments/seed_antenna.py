# Newton starting from the exclusion-boundary proxy mixed with the antenna (real axis in
# [-2, -1.4], plus a little imaginary jitter), n in [0, 64), no rejection.
# Reference (uniform seeds): antenna 81%, cardioid-side 15%, k<10/10-39/40+ 29/35/36%,
# L p50/p90 46/170, log|d| p50 1.8.
import numpy as np
from seedlib import evaluate, stats, boundary_pool, newton

rng = np.random.default_rng(6)
pool = boundary_pool(rng)


def mixed(share):
    def start(M):
        a = rng.uniform(-2, -1.4, M) + 1j * rng.normal(0, 0.01, M)
        return np.where(rng.uniform(0, 1, M) < share, a, rng.choice(pool, M))

    return start


for share in [0.25, 0.5, 0.75]:
    c, dn, conv, steps = newton(rng, mixed(share), 0, 64, M=40000)
    c = c[conv]
    good, s = stats(c, *evaluate(c))
    print(
        f"antenna share {share}: good {good.sum():5d} of 40000 ({steps / max(1, good.sum()):7.0f} solve steps/good): {s}",
        flush=True,
    )
