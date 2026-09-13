#!/usr/bin/env python3
"""Measure a render frame: background blackness, ink brightness, regions.
Usage: python3 tools/measure_frame.py <render.png> [portal.png]
"""
import sys
import numpy as np
from PIL import Image

A = np.asarray(Image.open(sys.argv[1]).convert('RGB'), dtype=np.float32)
h, w, _ = A.shape
pl = (0.299 * A[..., 0] + 0.587 * A[..., 1] + 0.114 * A[..., 2])
print(f"== {sys.argv[1]}  {w}x{h}")
print(f"  whole:  mean={pl.mean():6.1f}  frac>10={((pl>10).mean()*100):5.1f}%  frac>60={((pl>60).mean()*100):5.1f}%  frac>150={((pl>150).mean()*100):5.1f}%  max={pl.max():.0f}")
hist = np.histogram(pl, bins=[0, 5, 10, 25, 50, 100, 150, 200, 256])[0]
print("  hist: " + "  ".join(f"[{a}-{b}]={c/h/w*100:.1f}%" for c, (a, b) in zip(hist, [(0,5),(5,10),(10,25),(25,50),(50,100),(100,150),(150,200),(200,255)])))

bg = pl[:200]
face = pl[300:700, 280:540]
body = pl[700:, 200:560]
for name, m in [("BG top200", bg), ("FACE", face), ("BODY low", body)]:
    print(f"  {name:10s}: mean={m.mean():6.1f}  frac>10={((m>10).mean()*100):5.1f}%  frac>60={((m>60).mean()*100):5.1f}%  frac>150={((m>150).mean()*100):5.1f}%")
ratio = face.mean() / max(bg.mean(), 0.01)
print(f"  subject/bg luma ratio (face vs top200): {ratio:.1f}x   (portal target >= 3x, portal was 9.5x)")

if len(sys.argv) > 2:
    P = np.asarray(Image.open(sys.argv[2]).convert('RGB'), dtype=np.float32)
    ql = (0.299 * P[..., 0] + 0.587 * P[..., 1] + 0.114 * P[..., 2])
    ink = ql > 10
    print(f"== portal {sys.argv[2]}: ink frac={ink.mean()*100:.1f}%  mean ink={ql[ink].mean():.1f}  "
          f"frac>150={(ql>150).mean()*100:.1f}%  bg top200 mean={ql[:200].mean():.1f}")
