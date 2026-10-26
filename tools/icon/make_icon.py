"""Generates the application icon (brass anchor-and-wave emblem on a steel roundel) as a multi-size .ico."""
import math
import sys
from PIL import Image, ImageDraw


def draw(size: int) -> Image.Image:
    s = 4 * size  # supersample
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = s / 2
    r = s * 0.47
    d.ellipse([c - r, c - r, c + r, c + r], fill=(20, 32, 42, 255), outline=(219, 178, 102, 255), width=max(2, s // 28))
    brass = (219, 178, 102, 255)
    w = max(3, s // 22)
    # Anchor shank and ring
    d.line([(c, c - r * 0.55), (c, c + r * 0.55)], fill=brass, width=w)
    rr = r * 0.12
    d.ellipse([c - rr, c - r * 0.55 - rr * 2, c + rr, c - r * 0.55], outline=brass, width=w)
    d.line([(c - r * 0.3, c - r * 0.3), (c + r * 0.3, c - r * 0.3)], fill=brass, width=w)
    # Arms (arc)
    box = [c - r * 0.5, c - r * 0.05, c + r * 0.5, c + r * 0.65]
    d.arc(box, 20, 160, fill=brass, width=w)
    for sx in (-1, 1):
        x = c + sx * r * 0.47
        y = c + r * 0.38
        d.polygon([(x, y - r * 0.12), (x + sx * r * 0.1, y + r * 0.02), (x - sx * r * 0.06, y + r * 0.02)], fill=brass)
    # Waves
    for k in range(2):
        pts = []
        y0 = c + r * (0.62 + 0.12 * k)
        for i in range(41):
            x = c - r * 0.75 + i * r * 1.5 / 40
            pts.append((x, y0 + math.sin(i / 40 * math.pi * 4) * r * 0.035))
        d.line(pts, fill=(120, 170, 200, 255), width=max(2, w // 2))
    return img.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "IronTide.ico"
    sizes = [16, 24, 32, 48, 64, 128, 256]
    imgs = [draw(n) for n in sizes]
    imgs[-1].save(out, sizes=[(n, n) for n in sizes], append_images=imgs[:-1])
    draw(512).save(out.replace(".ico", ".png"))
