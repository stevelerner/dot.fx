#!/usr/bin/env python3
"""Gate diagnosis for dot_spacengrave on model.mp4 frame 83.

Replicates, pixel-exactly (float32), what core/dot.c does:
  - portal_split(): Otsu on the luma bin histogram, minority side = subject
  - local_shade(): edge = max(|E-W|,|S-N|) gradient magnitude at probe=2
    and probe=6 (probe = pitch/2 = size/2 = 2 for recipe size 5)
  - dot_spacengrave gate:  pass iff e >= knee AND subject-side of split
  - ink: 255 * tri * lv * (0.15 + 0.85 * l0)   (current C formula, tri=1)

Prints per-region pass fractions and resulting ink levels, plus the
approved dot.portal render (/tmp/pcm.png) for comparison.
"""
import numpy as np
from PIL import Image

img = Image.open('/tmp/src83.png').convert('RGB')
A = np.asarray(img, dtype=np.float32)
h, w, _ = A.shape
print(f"frame: {w}x{h}, source mean luma = {A.mean(axis=(0,1)).mean()*255/255:.3f} (0..1) = {A.mean()*255/255*255:.1f}/255")

luma = (0.299 * A[..., 0] + 0.587 * A[..., 1] + 0.114 * A[..., 2]) / 255.0
bins = np.clip((luma * 256.0).astype(np.int64), 0, 255)
hist = np.bincount(bins.ravel(), minlength=256).astype(np.int64)
N = int(hist.sum())
tm = int((hist * np.arange(256)).sum())

# Otsu — exactly the C loop (i over 0..254, class0 = bins 0..i, class1 = i+1..255)
best, best_i = -1.0, 128
w0 = m0 = 0
for i in range(255):
    w0 += int(hist[i]); m0 += int(hist[i]) * i
    w1 = N - w0
    if w0 == 0 or w1 == 0:
        continue
    mu0 = m0 / w0
    mu1 = (tm - m0) / w1
    d = mu0 - mu1
    v = w0 * w1 * d * d
    if v > best:
        best, best_i = v, i
dark = int(hist[:best_i].sum())
bright = 1 if dark * 2 > N else 0
T = best_i / 256.0
print(f"\nportal_split: best_i={best_i}  T={T:.4f} ({best_i}/256)  dark={dark/N:.3f}  bright={bright}")
print(f"  subject side: {'l0 > T' if bright else 'l0 < T'}  (subject frac = {1-dark/N if bright else dark/N:.3f})")

# local_shade edge probe, probe=2 (pitch 5 // 2), wide probe 3*probe=6
xs = np.arange(w)[None, :]
ys = np.arange(h)[:, None]
def at(dx, dy):
    X = np.clip(xs + dx, 0, w - 1)
    Y = np.clip(ys + dy, 0, h - 1)
    return luma[Y, X]
gmag = np.sqrt((at(2, 0) - at(-2, 0)) ** 2 + (at(0, 2) - at(0, -2)) ** 2)
gmag2 = np.sqrt((at(6, 0) - at(-6, 0)) ** 2 + (at(0, 6) - at(0, -6)) ** 2)
edge = np.maximum(gmag, gmag2)
e = np.minimum(1.0, 4.0 * edge)

subject = (luma > T) if bright else (luma < T)
print(f"\nedge stats: mean={edge.mean():.4f}  p50={np.median(edge):.4f}  p90={np.percentile(edge,90):.4f}")

def ink(l0, lv):
    return 255.0 * lv * (0.15 + 0.85 * l0)   # tri=1 at stroke centre

regions = {
    'WHOLE':      np.ones((h, w), bool),
    'BG top200':  np.zeros((h, w), bool) | (np.arange(h)[:, None] < 200),
    'FACE':       np.zeros((h, w), bool) | ((np.arange(h)[:, None] >= 300) & (np.arange(h)[:, None] < 700) & (np.arange(w)[None, :] >= 280) & (np.arange(w)[None, :] < 540)),
    'BODY low':   np.zeros((h, w), bool) | ((np.arange(h)[:, None] >= 700) & (np.arange(w)[None, :] >= 200) & (np.arange(w)[None, :] < 560)),
}
print(f"\n{'region':10s} {'frac':>6s} {'luma':>6s} {'subj':>6s} {'e>=.30':>6s} {'e>=.05':>6s} "
      f"{'PASS@.30':>8s} {'PASS@.05':>8s} {'ink@lv1':>7s} {'ink@lv2':>7s}")
masks = {}
for name, m in regions.items():
    sub = subject[m]
    e30 = (e >= 0.30)[m]
    e05 = (e >= 0.05)[m]
    p30 = sub & e30
    p05 = sub & e05
    ink30 = ink(luma[m][p30], 1.0).mean() if p30.any() else 0
    ink05 = ink(luma[m][p05], 1.0).mean() if p05.any() else 0
    ink30b = ink(luma[m][p30], 2.0).mean() if p30.any() else 0
    ink05b = ink(luma[m][p05], 2.0).mean() if p05.any() else 0
    masks[name] = (p30, p05)
    print(f"{name:10s} {m.mean():6.2f} {luma[m].mean():6.3f} {sub.mean():6.2f} {e30.mean():6.2f} {e05.mean():6.2f} "
          f"{p30.mean():8.2f} {p05.mean():8.2f} {ink30:7.1f} {ink05:7.1f} / {ink30b:6.1f} {ink05b:6.1f}")

# histogram of e in the subject side (what knee would exclude face vs concrete)
sm = subject
print(f"\nsubject-side e percentiles: p10={np.percentile(e[sm],10):.3f} p25={np.percentile(e[sm],25):.3f} "
      f"p50={np.percentile(e[sm],50):.3f} p75={np.percentile(e[sm],75):.3f} p90={np.percentile(e[sm],90):.3f}")
print(f"  e>=0.30 within subject: {(e[sm]>=0.30).mean():.3f}   e>=0.05 within subject: {(e[sm]>=0.05).mean():.3f}")

# approved dot.portal frame 83 for comparison
pcm = Image.open('/tmp/pcm.png').convert('RGB')
P = np.asarray(pcm, dtype=np.float32)
pl = (0.299 * P[..., 0] + 0.587 * P[..., 1] + 0.114 * P[..., 2]) / 255.0
pinch = pl > 0.04
print(f"\nportal pcm: ink frac (>0.04) = {pinch.mean():.3f}  mean ink luma = {pl[pinch].mean():.3f}  "
      f"frac>150/255 = {(pl > 0.588).mean():.3f}  max = {pl.max():.3f}")
p30, p05 = masks['WHOLE']
print(f"overlap gate@0.30 with portal ink: {(p30 & pinch).sum() / max(pinch.sum(),1):.2f} of portal, {(p30 & pinch).sum() / max(p30.sum(),1):.2f} of ours")
print(f"overlap gate@0.05 with portal ink: {(p05 & pinch).sum() / max(pinch.sum(),1):.2f} of portal, {(p05 & pinch).sum() / max(p05.sum(),1):.2f} of ours")
# where is portal's ink?
ys_i, xs_i = np.nonzero(pinch)
print(f"portal ink bbox: x {xs_i.min()}-{xs_i.max()}  y {ys_i.min()}-{ys_i.max()}")
facebox = pinch[300:700, 280:540]
print(f"portal ink inside FACE box: {facebox.mean():.3f} of box, {pinch[300:700, 280:540].sum()/pinch.sum():.2f} of all ink")
