# Why Newton attempts fail, by n range, from random starts (research doc, second table).
# diag2.py imports newton(), w and half from here.
import numpy as np
from seeds import evaluate

w, half = -0.1 + 0.75j, 1e-4


def newton(c, n, tgt, iters=30):
    M = len(c)
    for it in range(iters):
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
    return c, abs(z - tgt) < half


if __name__ == "__main__":
    rng = np.random.default_rng(2)
    for label, nlo, nhi in [("n 2-40", 2, 40), ("n 2-10", 2, 10), ("n 20-60", 20, 60)]:
        M = 3000
        n = rng.integers(nlo, nhi, M)
        tgt = w + (rng.uniform(-half, half, M) + 1j * rng.uniform(-half, half, M))
        c0 = rng.uniform(-2, 0.5, M) + 1j * rng.uniform(-1.2, 1.2, M)
        c, conv = newton(c0, n, tgt)
        L, C = evaluate(c, w, half)
        print(
            label,
            f"converged {conv.mean():.2f}  inside(L=-1) {(conv & (L < 0)).mean():.2f}  L<=32 {(conv & (L >= 0) & (L <= 32)).mean():.2f}  good {(conv & (L > 32) & (C > 0)).mean():.3f}  medianL(good) {np.median(L[conv & (L > 32)]) if (conv & (L > 32)).any() else 0}",
        )
