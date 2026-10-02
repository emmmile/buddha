# Which seeds reach the window re=1.4, im=0, span 0.0148875 (the user's comparison view)?
# Uniform seeds (what walk/uniform seeding find) against Newton seeds (n in [10, 40), starting
# from random points with L > 32, as a proxy for the exclusion-boundary cells).
import time

import numpy as np

from seedlib import HALF, LOW, W, evaluate, inside_main

rng = np.random.default_rng(3)


def describe(name, L, C, K, D):
    good = (L > LOW) & (C > 0)
    L, C, K, D = L[good], C[good], K[good], D[good]
    print(f"{name}: {good.sum()} good")
    print(
        "  first-visit step k:  <10 %.0f%%  10-39 %.0f%%  >=40 %.0f%%"
        % (100 * (K < 10).mean(), 100 * ((K >= 10) & (K < 40)).mean(), 100 * (K >= 40).mean())
    )
    print(
        "  L quantiles 10/50/90:",
        np.percentile(L, [10, 50, 90]).astype(int),
        " C median/90:",
        np.percentile(C, [50, 90]).astype(int),
    )
    print(
        "  log10|dz/dc| at first visit 10/50/90:",
        np.round(np.percentile(np.log10(D), [10, 50, 90]), 1),
    )
    return good


t = time.time()
N = 30_000_000
c = rng.uniform(-2, 0.5, N) + 1j * rng.uniform(-1.2, 1.2, N)
c = c[~inside_main(c)]
L, C, K, D = evaluate(c)
g = describe("uniform", L, C, K, D)
cu = c[g]
print(f"  ({time.time() - t:.0f}s)")

# Newton from near-boundary points
P = 200_000
s = rng.uniform(-2, 0.5, P) + 1j * rng.uniform(-1.2, 1.2, P)
s = s[~inside_main(s)]
Ls, _, _, _ = evaluate(s)
pool = s[Ls > LOW]
M = 20000
n = rng.integers(10, 40, M)
tgt = W + rng.uniform(-HALF, HALF, M) + 1j * rng.uniform(-HALF, HALF, M)
c = rng.choice(pool, M)
for it in range(24):
    z = c.copy()
    d = np.ones(M, complex)
    with np.errstate(all="ignore"):
        for k in range(n.max()):
            m = k < n
            d = np.where(m, 2 * z * d + 1, d)
            z = np.where(m, z * z + c, z)
        step = (z - tgt) / d
    step = np.where(np.isfinite(step), step, 0)
    c = c - np.clip(abs(step), 0, 0.5) * np.exp(1j * np.angle(step))
L, C, K, D = evaluate(c)
g = describe("newton", L, C, K, D)
cn = c[g]
print(f"  ({time.time() - t:.0f}s)")
for name, cs in [("uniform", cu), ("newton", cn)]:
    print(
        f"  {name} seeds by region: antenna (re<-1.4, |im|<0.05) {100 * ((cs.real < -1.4) & (abs(cs.imag) < 0.05)).mean():.0f}%,  re>-0.75 {100 * (cs.real > -0.75).mean():.0f}%"
    )
