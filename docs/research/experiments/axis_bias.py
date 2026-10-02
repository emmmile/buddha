# Real-axis bias in full-view renders from capture.ts (re=0, im=0, span=4, 1352 x 913: Im = 0 is
# the centre column). Luminance of the display output, so values are relative to each image's
# brightest pixels.
#
#   uv run --with numpy --with pillow python axis_bias.py DIR walk uniform newton newton-no-antenna
import sys

import numpy as np
from PIL import Image

directory, modes = sys.argv[1], sys.argv[2:]
rows = {}
for mode in modes:
    a = np.asarray(Image.open(f"{directory}/{mode}.png").convert("RGB")).astype(float) / 255
    h, w, _ = a.shape
    lum = a @ [0.2126, 0.7152, 0.0722]
    cx = w // 2
    # Outside the main body: the bands above y=130 and below y=640.
    outside = np.r_[0:130, 640:h]
    axis = lum[outside][:, cx - 2 : cx + 3].mean()
    near = lum[outside][:, np.r_[cx - 60 : cx - 20, cx + 20 : cx + 60]].mean()
    body = lum[250:600, 470:880].mean()
    ghosts = max(
        lum[620:650, 510:540].max(), lum[620:650, 812:842].max()
    )  # off-axis minibrot ghosts
    rgb = a[250:600, 470:880].reshape(-1, 3).mean(0)
    rows[mode] = (axis, near, axis / near, body, ghosts, rgb)
print(
    f"{'mode':18s} {'axis':>6s} {'beside':>7s} {'ratio':>6s} {'body':>6s} {'ghosts':>7s}  body R/G/B"
)
for m, (axis, near, ratio, body, ghosts, rgb) in rows.items():
    print(
        f"{m:18s} {axis:6.3f} {near:7.3f} {ratio:6.1f} {body:6.3f} {ghosts:7.3f}  {rgb[0]:.3f}/{rgb[1]:.3f}/{rgb[2]:.3f}  (B/R {rgb[2] / rgb[0]:.2f})"
    )
