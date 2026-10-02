# Compares renders of one view block by block. Each export has its own tone mapping, so it uses
# Spearman rank correlation of block-mean luminance: whether the same regions are bright, not how
# bright. Usage: compare.py DIR BLOCK reference other...
import sys
import numpy as np
from PIL import Image

directory, block, ref, others = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4:]


def blocks(name):
    a = np.asarray(Image.open(f"{directory}/{name}.png").convert("RGB")).astype(float) / 255
    lum = a @ [0.2126, 0.7152, 0.0722]
    h, w = (lum.shape[0] // block) * block, (lum.shape[1] // block) * block
    return lum[:h, :w].reshape(h // block, block, w // block, block).mean((1, 3)).ravel()


def ranks(x):
    r = np.empty(len(x))
    r[np.argsort(x, kind="stable")] = np.arange(len(x))
    return r


base = ranks(blocks(ref))
for name in others:
    print(f"  {name:12s} vs {ref:6s}: rank corr {np.corrcoef(base, ranks(blocks(name)))[0, 1]:.3f}")
