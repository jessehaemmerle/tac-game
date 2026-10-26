"""Generates the tiling material textures used on ship models.

usage: make_ship_textures.py <out_dir> [size]

Writes two texture arrays as vertically stacked layers (size x size each):
  ship_albedo.png   RGB: albedo detail (sRGB, 128 = keep the paint colour), A: metal mask
  ship_surface.png  RG: tangent-space normal, B: roughness factor (128 = x1), A: cavity / occlusion
and ship_layers.json (layer order and the physical size of one tile in metres).

All patterns are periodic, so every layer tiles seamlessly. The ship shader projects the layers
triplanar in model space; the tile sizes below must match the table in data/shaders/ship.hlsl.
"""
import json
import os
import sys

import numpy as np
from PIL import Image

SIZE = 512
LAYERS = [  # name, tile size (m)
    ("hull_plating", 8.0),
    ("superstructure_plating", 4.0),
    ("teak_deck", 4.0),
    ("linoleum_deck", 4.0),
    ("steel_deck", 4.0),
    ("canvas", 1.5),
    ("gun_metal", 2.0),
    ("antifouling", 4.0),
    ("grating", 1.0),
]


# --------------------------------------------------------------------------------------- noise
def value_noise(n, cells, rng):
    """Periodic smooth value noise in [0, 1] on an n x n grid with `cells` lattice cells per tile."""
    g = rng.random((cells, cells))
    t = np.arange(n) * cells / n
    i0 = np.floor(t).astype(int)
    f = t - i0
    f = f * f * (3 - 2 * f)
    i1 = (i0 + 1) % cells
    i0 %= cells
    a = g[np.ix_(i0, i0)] * (1 - f)[None, :] + g[np.ix_(i0, i1)] * f[None, :]
    b = g[np.ix_(i1, i0)] * (1 - f)[None, :] + g[np.ix_(i1, i1)] * f[None, :]
    return a * (1 - f)[:, None] + b * f[:, None]


def fbm(n, cells, octaves, rng, gain=0.5):
    s, a, tot = np.zeros((n, n)), 1.0, 0.0
    for _ in range(octaves):
        s += a * value_noise(n, cells, rng)
        tot += a
        a *= gain
        cells *= 2
        if cells > n:
            break
    return s / tot


def stretched_noise(n, cells_u, cells_v, rng):
    """Noise with different frequencies along u (columns) and v (rows): wood grain, brushing."""
    g = rng.random((cells_v, cells_u))
    tu = np.arange(n) * cells_u / n
    tv = np.arange(n) * cells_v / n

    def axis(t, cells):
        i0 = np.floor(t).astype(int)
        f = t - i0
        f = f * f * (3 - 2 * f)
        return i0 % cells, (i0 + 1) % cells, f

    u0, u1, fu = axis(tu, cells_u)
    v0, v1, fv = axis(tv, cells_v)
    a = g[np.ix_(v0, u0)] * (1 - fu)[None, :] + g[np.ix_(v0, u1)] * fu[None, :]
    b = g[np.ix_(v1, u0)] * (1 - fu)[None, :] + g[np.ix_(v1, u1)] * fu[None, :]
    return a * (1 - fv)[:, None] + b * fv[:, None]


def blur(img, r):
    """Periodic box blur (applied twice ~ gaussian)."""
    out = img.copy()
    for _ in range(2):
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, axis=0)
        out = acc / (2 * r + 1)
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, axis=1)
        out = acc / (2 * r + 1)
    return out


def line_profile(coord, period, width):
    """1 on lines every `period` pixels (soft edges `width` px), 0 elsewhere. coord: pixel coordinate array."""
    d = np.abs(((coord + period / 2) % period) - period / 2)
    return np.clip(1.0 - d / width, 0.0, 1.0)


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5 * strength
    nx, ny, nz = -dx, dy, np.ones_like(h)  # image rows grow downwards, v grows upwards in the shader
    l = np.sqrt(nx * nx + ny * ny + nz * nz)
    return nx / l, ny / l


class Layer:
    """Accumulates albedo multiplier (linear, 0.5 = neutral), metal mask, height (m), roughness factor, cavity."""

    def __init__(self, n):
        self.alb = np.full((n, n, 3), 0.5)
        self.metal = np.zeros((n, n))
        self.height = np.zeros((n, n))
        self.rough = np.ones((n, n))
        self.cavity = np.ones((n, n))

    def tint(self, factor):
        self.alb *= factor[..., None] if factor.ndim == 2 else factor


# ------------------------------------------------------------------------------------ materials
def plating(n, tile, rng, plate_w, plate_h, stagger, bulge, rivets):
    """Welded steel plates with weld beads, slightly dished panels, orange-peel paint and grime."""
    L = Layer(n)
    ppm = n / tile
    y, x = np.mgrid[0:n, 0:n].astype(float)
    ph, pw = plate_h * ppm, plate_w * ppm
    row = np.floor(y / ph)
    xs = x + (row % 2) * pw * stagger
    seam_h = line_profile(y, ph, 2.2)
    seam_v = line_profile(xs, pw, 2.2)
    seam = np.maximum(seam_h, seam_v)
    # Dished ("hungry horse") panels between the stiffeners.
    fy = ((y % ph) / ph - 0.5) * 2
    fx = ((xs % pw) / pw - 0.5) * 2
    dish = (1 - fx ** 2) * (1 - fy ** 2)
    panel_id = (row * 131 + np.floor(xs / pw) * 17) % 97
    panel_var = (np.sin(panel_id * 12.9898) * 43758.5453) % 1.0
    L.height += -bulge * dish * (0.6 + 0.8 * panel_var)
    L.height += 0.0012 * seam  # weld bead
    L.height += 0.0004 * (fbm(n, 64, 3, rng) - 0.5)  # orange peel
    if rivets:
        rv = line_profile(y, ph, 6.0) * (line_profile(xs, 0.09 * ppm, 1.6) > 0.5)
        L.height += 0.0015 * np.clip(rv, 0, 1)
    # Paint: touch-ups per panel, low-frequency fading, grime in the seams.
    tone = 0.95 + 0.06 * panel_var + 0.08 * (fbm(n, 4, 4, rng) - 0.5)
    L.tint(tone)
    L.tint(1.0 - 0.18 * blur(seam, 2))
    # Small rust bleeds starting at the seams and running down.
    rust_seed = (fbm(n, 32, 2, rng) > 0.78).astype(float) * blur(seam_h, 3) * 3
    streak = np.zeros((n, n))
    for k in range(0, int(ph * 0.6), 2):
        streak = np.maximum(streak, np.roll(rust_seed, k, axis=0) * (1 - k / (ph * 0.6)))
    streak = np.clip(streak * stretched_noise(n, 96, 6, rng) * 2.0, 0, 1)
    rust = np.array([0.62, 0.40, 0.27]) / 0.5
    L.alb = L.alb * (1 - streak[..., None] * 0.45) + (L.alb * rust) * (streak[..., None] * 0.45)
    L.rough *= 1.0 + 0.25 * streak + 0.15 * (fbm(n, 16, 3, rng) - 0.5)
    L.cavity *= 1.0 - 0.35 * seam
    return L


def teak(n, tile, rng):
    L = Layer(n)
    ppm = n / tile
    planks = 32  # 12.5 cm planks along u
    pw = n / planks
    y, x = np.mgrid[0:n, 0:n].astype(float)
    pid = np.floor(y / pw)
    offset = ((np.sin(pid * 78.233) * 43758.5453) % 1.0) * n
    butt_period = 2.0 * ppm  # butts every 2 m, staggered per plank
    butt = line_profile(x + offset, butt_period, 1.3)
    caulk = line_profile(y, pw, 1.4)
    grain = stretched_noise(n, 6, 256, rng) * 0.6 + stretched_noise(n, 24, 128, rng) * 0.4
    plank_tone = 0.86 + 0.24 * ((np.sin(pid * 12.9898 + np.floor((x + offset) / butt_period) * 4.1) * 43758.5453) % 1.0)
    weather = 0.92 + 0.16 * fbm(n, 4, 4, rng)
    L.tint(plank_tone * weather * (0.88 + 0.24 * grain))
    joint = np.maximum(caulk, butt)
    L.alb = L.alb * (1 - joint[..., None]) + np.array([0.06, 0.055, 0.05]) * joint[..., None]
    fy = ((y % pw) / pw - 0.5) * 2
    L.height += 0.0015 * (1 - fy ** 4) - 0.002 * joint + 0.0004 * (grain - 0.5)
    L.rough *= 1.05 + 0.2 * (grain - 0.5) + 0.2 * joint
    L.cavity *= 1.0 - 0.45 * joint
    return L


def linoleum(n, tile, rng):
    """IJN linoleum held down by brass strips laid athwartships."""
    L = Layer(n)
    ppm = n / tile
    y, x = np.mgrid[0:n, 0:n].astype(float)
    strip = line_profile(x, 1.0 * ppm, 2.0)
    seam = line_profile(y, 2.0 * ppm, 1.0) * (1 - strip)
    mottle = fbm(n, 16, 5, rng)
    wear = blur(fbm(n, 4, 3, rng), 4)
    L.tint(0.9 + 0.12 * (mottle - 0.5) + 0.18 * (wear - 0.5))
    brass = np.array([0.95, 0.72, 0.36]) / 0.5 * 0.55
    L.alb = L.alb * (1 - strip[..., None]) + brass * strip[..., None]
    L.metal = strip
    L.height += 0.0012 * strip - 0.0006 * seam + 0.0002 * (mottle - 0.5)
    L.rough *= (1.0 + 0.15 * (mottle - 0.5)) * (1 - 0.6 * strip)
    L.cavity *= 1.0 - 0.3 * seam
    return L


def steel_deck(n, tile, rng):
    L = plating(n, tile, rng, plate_w=2.0, plate_h=1.0, stagger=0.5, bulge=0.002, rivets=False)
    grit = fbm(n, 256, 2, rng)
    L.height += 0.0005 * (grit - 0.5)
    wear = blur(fbm(n, 8, 3, rng), 3)
    L.tint(1.0 + 0.12 * np.clip(wear - 0.55, 0, 1) * 3)  # scuffed paint is lighter
    L.rough *= 1.1 + 0.2 * (grit - 0.5)
    return L


def canvas(n, tile, rng):
    L = Layer(n)
    y, x = np.mgrid[0:n, 0:n].astype(float)
    threads = n / tile * 0.004  # px per thread (4 mm)
    wu = np.sin(x / threads * np.pi) * np.sin(y / threads * np.pi * 0.5)
    wv = np.sin(y / threads * np.pi) * np.sin(x / threads * np.pi * 0.5)
    weave = 0.5 + 0.25 * (wu + wv)
    folds = blur(fbm(n, 6, 4, rng), 6)
    stains = np.clip((fbm(n, 8, 4, rng) - 0.6) * 3, 0, 1)
    L.tint((0.92 + 0.1 * weave) * (0.9 + 0.2 * folds) * (1 - 0.25 * stains))
    L.height += 0.0004 * weave + 0.01 * (folds - 0.5)
    L.rough *= 1.15
    L.cavity *= 0.85 + 0.15 * weave
    return L


def gun_metal(n, tile, rng):
    L = Layer(n)
    brushed = stretched_noise(n, 8, 384, rng)
    scratches = np.clip((stretched_noise(n, 3, 512, rng) - 0.82) * 6, 0, 1)
    blotch = fbm(n, 8, 4, rng)
    L.tint((0.9 + 0.12 * (brushed - 0.5)) * (0.92 + 0.16 * blotch) * (1 + 0.5 * scratches))
    L.height += 0.0003 * (brushed - 0.5) - 0.0003 * scratches
    L.rough *= (0.9 + 0.3 * (blotch - 0.5)) * (1 - 0.4 * scratches)
    return L


def antifouling(n, tile, rng):
    L = plating(n, tile, rng, plate_w=6.0, plate_h=2.0, stagger=0.5, bulge=0.004, rivets=True)
    growth = np.clip((fbm(n, 16, 5, rng) - 0.45) * 2.5, 0, 1)
    green = np.array([0.42, 0.48, 0.34]) / 0.5
    L.alb = L.alb * (1 - 0.45 * growth[..., None]) + L.alb * green * (0.45 * growth[..., None])
    L.height += 0.002 * growth * fbm(n, 128, 2, rng)
    L.rough *= 1.0 + 0.3 * growth
    return L


def grating(n, tile, rng):
    L = Layer(n)
    ppm = n / tile
    y, x = np.mgrid[0:n, 0:n].astype(float)
    pitch = 0.05 * ppm  # 5 cm squares
    bar = np.maximum(line_profile(x, pitch, 0.012 * ppm), line_profile(y, pitch, 0.012 * ppm))
    hole = 1 - bar
    L.alb = L.alb * (1 - 0.85 * hole[..., None])
    L.height += -0.02 * hole
    L.cavity *= 1 - 0.8 * hole
    L.rough *= 1.0 + 0.2 * hole
    L.tint(0.9 + 0.2 * fbm(n, 8, 3, rng))
    return L


def build(name, tile, n, rng):
    if name == "hull_plating":
        return plating(n, tile, rng, plate_w=4.0, plate_h=2.0, stagger=0.5, bulge=0.004, rivets=True)
    if name == "superstructure_plating":
        return plating(n, tile, rng, plate_w=4.0 / 3.0, plate_h=1.0, stagger=0.0, bulge=0.006, rivets=False)
    return {"teak_deck": teak, "linoleum_deck": linoleum, "steel_deck": steel_deck, "canvas": canvas, "gun_metal": gun_metal,
            "antifouling": antifouling, "grating": grating}[name](n, tile, rng)


def to_srgb8(lin):
    lin = np.clip(lin, 0, 1)
    s = np.where(lin <= 0.0031308, lin * 12.92, 1.055 * np.power(lin, 1 / 2.4) - 0.055)
    return np.round(s * 255).astype(np.uint8)


def main():
    out_dir = sys.argv[1]
    n = int(sys.argv[2]) if len(sys.argv) > 2 else SIZE
    os.makedirs(out_dir, exist_ok=True)
    albedo, surface, meta = [], [], []
    for i, (name, tile) in enumerate(LAYERS):
        rng = np.random.default_rng(1000 + i)
        L = build(name, tile, n, rng)
        # Albedo multiplier: 0.5 linear is neutral. Stored sRGB so mid-tones keep precision.
        a = np.zeros((n, n, 4), np.uint8)
        a[..., :3] = to_srgb8(L.alb)
        a[..., 3] = np.round(np.clip(L.metal, 0, 1) * 255).astype(np.uint8)
        ppm = n / tile
        nx, ny = normal_from_height(L.height * ppm, 1.0)
        s = np.zeros((n, n, 4), np.uint8)
        s[..., 0] = np.round((nx * 0.5 + 0.5) * 255).astype(np.uint8)
        s[..., 1] = np.round((ny * 0.5 + 0.5) * 255).astype(np.uint8)
        s[..., 2] = np.round(np.clip(L.rough * 0.5, 0, 1) * 255).astype(np.uint8)
        s[..., 3] = np.round(np.clip(L.cavity, 0, 1) * 255).astype(np.uint8)
        albedo.append(a)
        surface.append(s)
        meta.append({"layer": i, "name": name, "tile_m": tile})
        print(f"  {i}: {name:24s} tile {tile:4.1f} m")
    Image.fromarray(np.concatenate(albedo, axis=0), "RGBA").save(os.path.join(out_dir, "ship_albedo.png"), optimize=True)
    Image.fromarray(np.concatenate(surface, axis=0), "RGBA").save(os.path.join(out_dir, "ship_surface.png"), optimize=True)
    with open(os.path.join(out_dir, "ship_layers.json"), "w", encoding="utf-8") as fh:
        json.dump({"size": n, "layers": meta}, fh, indent=1)
        fh.write("\n")


if __name__ == "__main__":
    main()
