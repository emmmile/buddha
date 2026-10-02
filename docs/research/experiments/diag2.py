# Why Newton attempts fail, from boundary-pool starts (research doc, second table).
import numpy as np
from seeds import evaluate
from diag import newton, w, half

rng = np.random.default_rng(2)
# boundary pool: uniform points with 32 < L (escaping), like a cheap pilot pass
P = 60000
c = rng.uniform(-2, 0.5, P) + 1j * rng.uniform(-1.2, 1.2, P)
L, _ = evaluate(c, w, half)
pool = c[(L > 32)]
print("pool size", len(pool))
for label, nlo, nhi in [("pool n 2-10", 2, 10), ("pool n 2-40", 2, 40), ("pool n 10-40", 10, 40)]:
    M = 3000
    n = rng.integers(nlo, nhi, M)
    tgt = w + (rng.uniform(-half, half, M) + 1j * rng.uniform(-half, half, M))
    c, conv = newton(rng.choice(pool, M), n, tgt)
    L, C = evaluate(c, w, half)
    g = conv & (L > 32) & (C > 0)
    print(
        label,
        f"converged {conv.mean():.2f} inside {(conv & (L < 0)).mean():.2f} L<=32 {(conv & (L >= 0) & (L <= 32)).mean():.2f} good {g.mean():.3f} medianL {np.median(L[g]) if g.any() else 0} medianC {np.median(C[g]) if g.any() else 0} maxC {C[g].max() if g.any() else 0}",
    )
