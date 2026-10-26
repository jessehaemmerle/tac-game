#!/usr/bin/env python3
"""Build a game height field from Tilezen terrain tiles (Terrarium encoding).

Data: Mapzen/Tilezen Terrain Tiles on AWS Open Data (SRTM, GMTED, ETOPO1 and others).
Attribution: https://github.com/tilezen/joerd/blob/master/docs/attribution.md

Output: <out>.r16 (int16 little endian metres, row 0 = north edge) and <out>.json metadata
in the local equirectangular frame used by sim::Terrain.
"""
import argparse
import io
import json
import math
import os
import struct
import urllib.request

import numpy as np
from PIL import Image

TILE_URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"


def lonlat_to_pixel(lon, lat, z):
    n = 256 * (2 ** z)
    x = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n
    return x, y


def fetch_tile(z, x, y, cache):
    path = os.path.join(cache, f"{z}_{x}_{y}.png")
    if not os.path.exists(path):
        with urllib.request.urlopen(TILE_URL.format(z=z, x=x, y=y), timeout=60) as r:
            data = r.read()
        with open(path, "wb") as f:
            f.write(data)
    img = Image.open(path).convert("RGB")
    a = np.asarray(img, dtype=np.float64)
    return a[:, :, 0] * 256.0 + a[:, :, 1] + a[:, :, 2] / 256.0 - 32768.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--lat-min", type=float, required=True)
    ap.add_argument("--lat-max", type=float, required=True)
    ap.add_argument("--lon-min", type=float, required=True)
    ap.add_argument("--lon-max", type=float, required=True)
    ap.add_argument("--cell", type=float, default=100.0, help="cell size in metres")
    ap.add_argument("--zoom", type=int, default=11, help="zoom for land elevation")
    ap.add_argument("--bathy-zoom", type=int, default=8, help="zoom for sea depth (ETOPO1-derived)")
    ap.add_argument("--out", required=True, help="output base path (without extension)")
    ap.add_argument("--cache", default=".terrain_cache")
    args = ap.parse_args()
    os.makedirs(args.cache, exist_ok=True)

    lat0 = 0.5 * (args.lat_min + args.lat_max)
    lon0 = 0.5 * (args.lon_min + args.lon_max)
    kx = 111320.0 * math.cos(math.radians(lat0))
    ky = 110574.0
    width_m = (args.lon_max - args.lon_min) * kx
    height_m = (args.lat_max - args.lat_min) * ky
    w = int(round(width_m / args.cell)) + 1
    h = int(round(height_m / args.cell)) + 1

    xs = (np.arange(w) - (w - 1) / 2.0) * args.cell
    ys = ((h - 1) / 2.0 - np.arange(h)) * args.cell
    LON, LAT = np.meshgrid(lon0 + xs / kx, lat0 + ys / ky)
    land = sample(args, LON, LAT, args.zoom)
    sea = sample(args, LON, LAT, args.bathy_zoom)
    # High zoom tiles carry SRTM land but often 0 m over water; take depth from the coarser bathymetry and
    # keep a shallow shelf near the coast so the shoreline stays where SRTM puts it.
    v = np.where(land > 0.5, land, np.minimum(sea, -3.0))
    v = np.where((land <= 0.5) & (sea > -3.0), -3.0, v)
    heights = np.clip(np.round(v), -32768, 32767).astype("<i2")
    write(args, heights, w, h, lat0, lon0)


def sample(args, LON, LAT, z):
    x0p, y0p = lonlat_to_pixel(args.lon_min, args.lat_max, z)
    x1p, y1p = lonlat_to_pixel(args.lon_max, args.lat_min, z)
    tx0, ty0 = int(x0p // 256), int(y0p // 256)
    tx1, ty1 = int(x1p // 256), int(y1p // 256)
    mosaic = np.zeros(((ty1 - ty0 + 1) * 256, (tx1 - tx0 + 1) * 256))
    for ty in range(ty0, ty1 + 1):
        for tx in range(tx0, tx1 + 1):
            mosaic[(ty - ty0) * 256:(ty - ty0 + 1) * 256, (tx - tx0) * 256:(tx - tx0 + 1) * 256] = fetch_tile(z, tx, ty, args.cache)

    # Bilinear resampling onto the local grid.
    n = 256 * (2 ** z)
    px = (LON + 180.0) / 360.0 * n - tx0 * 256
    s = np.sin(np.radians(LAT))
    py = (0.5 - np.log((1 + s) / (1 - s)) / (4 * np.pi)) * n - ty0 * 256
    x0 = np.clip(np.floor(px).astype(int), 0, mosaic.shape[1] - 2)
    y0 = np.clip(np.floor(py).astype(int), 0, mosaic.shape[0] - 2)
    fx, fy = px - x0, py - y0
    v = (mosaic[y0, x0] * (1 - fx) * (1 - fy) + mosaic[y0, x0 + 1] * fx * (1 - fy) +
         mosaic[y0 + 1, x0] * (1 - fx) * fy + mosaic[y0 + 1, x0 + 1] * fx * fy)
    return v


def write(args, heights, w, h, lat0, lon0):
    with open(args.out + ".r16", "wb") as f:
        f.write(heights.tobytes())
    meta = {
        "name": args.name,
        "center_lat": lat0,
        "center_lon": lon0,
        "width": w,
        "height": h,
        "cell_size_m": args.cell,
        "heights": os.path.basename(args.out) + ".r16",
        "bounds": {"lat_min": args.lat_min, "lat_max": args.lat_max, "lon_min": args.lon_min, "lon_max": args.lon_max},
        "attribution": "Terrain Tiles (Mapzen/Tilezen, AWS Open Data): SRTM, GMTED2010, ETOPO1 and others; "
                       "see https://github.com/tilezen/joerd/blob/master/docs/attribution.md",
    }
    with open(args.out + ".json", "w") as f:
        json.dump(meta, f, indent=2)
    land = float((heights > 0).mean())
    print(f"{args.name}: {w}x{h} cells @ {args.cell} m, min {heights.min()} m, max {heights.max()} m, land {land:.1%}")


if __name__ == "__main__":
    main()
