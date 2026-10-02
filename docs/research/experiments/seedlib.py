# Shared helpers for the seeding experiments (docs/research/SEEDING_AND_ZOOM.md). The window is
# the user's comparison view re=1.4, im=0, span 0.0148875; orbits use the shader's z0 = c.
import numpy as np

W = 1.4 + 0j
HALF = 0.0148875 / 2
LOW, HIGH = 32, 8192


def evaluate(c):
    """L (escape step or -1), C (visits), first visit step k, |dz/dc| at k, for each c."""
    n = len(c)
    L = np.full(n, -1)
    C = np.zeros(n, int)
    K = np.full(n, -1)
    D = np.zeros(n)
    idx = np.arange(n)
    z = c.copy()
    d = np.ones(n, complex)
    cc = c.copy()
    for i in range(HIGH):
        inw = (abs(z.real - W.real) < HALF) & (abs(z.imag - W.imag) < HALF)
        C[idx[inw]] += 1
        first = inw & (K[idx] < 0)
        K[idx[first]] = i
        D[idx[first]] = abs(d[first])
        esc = (z.real**2 + z.imag**2) > 8
        L[idx[esc]] = i
        keep = ~esc
        idx, z, d, cc = idx[keep], z[keep], d[keep], cc[keep]
        if not len(idx):
            break
        d = 2 * z * d + 1
        z = z * z + cc
    return L, C, K, D


def inside_main(c):
    """Main cardioid and period-2 bulb."""
    q = (c.real - 0.25) ** 2 + c.imag**2
    return (q * (q + c.real - 0.25) < 0.25 * c.imag**2) | ((c.real + 1) ** 2 + c.imag**2 < 1 / 16)


def stats(c, L, C, K, D):
    good = (L > LOW) & (C > 0)
    c, L, K, D = c[good], L[good], K[good], D[good]
    if not len(c):
        return good, "none"
    return good, (
        f"antenna {100 * ((c.real < -1.4) & (abs(c.imag) < 0.05)).mean():3.0f}%  cardioid-side {100 * (c.real > -0.75).mean():3.0f}%  "
        f"k<10/10-39/40+ {100 * (K < 10).mean():3.0f}/{100 * ((K >= 10) & (K < 40)).mean():3.0f}/{100 * (K >= 40).mean():3.0f}%  "
        f"L p50/p90 {int(np.median(L)):4d}/{int(np.percentile(L, 90)):4d}  log|d| p50 {np.median(np.log10(D)):4.1f}"
    )


def boundary_pool(rng, P=200_000):
    """Random points escaping after more than LOW steps: a proxy for the exclusion-boundary cells."""
    s = rng.uniform(-2, 0.5, P) + 1j * rng.uniform(-1.2, 1.2, P)
    s = s[~inside_main(s)]
    return s[evaluate(s)[0] > LOW]


def newton(rng, starts, nlo, nhi, M=20000, rounds=24):
    """Solves z_n(c) = w for w uniform in the window; returns c, |dz_n/dc|, converged, solve steps."""
    n = rng.integers(nlo, nhi, M)
    tgt = W + rng.uniform(-HALF, HALF, M) + 1j * rng.uniform(-HALF, HALF, M)
    c = starts(M)
    steps = 0
    for it in range(rounds):
        z = c.copy()
        d = np.ones(M, complex)
        with np.errstate(all="ignore"):
            for k in range(n.max()):
                m = k < n
                d = np.where(m, 2 * z * d + 1, d)
                z = np.where(m, z * z + c, z)
            steps += n.sum()
            conv = abs(z - tgt) < 2 * HALF / 1000  # within ~half a pixel of a 1000 px view
            step = np.where(conv, 0, (z - tgt) / d)
        step = np.where(np.isfinite(step), step, 0)
        c = c - np.clip(abs(step), 0, 0.5) * np.exp(1j * np.angle(step))
    return c, abs(d), conv, steps
