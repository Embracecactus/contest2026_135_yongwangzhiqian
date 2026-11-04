#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Story thumbnail and covers from a real frame of the film.

Frames (film time): 387.5 s, the blackout, his face lit only by her eyes, her eyes
reflected in his glasses; 415.37 s, her eyes in the dark. Outputs thumbnail-16x9.png, cover-4x3.png, cover-3x4.png,
cover-9x16.png: same frame and title, each cropped and laid out for its shape.

Usage: python3 tools/build_covers.py <film.mp4> <out-dir> [--at <film seconds>] [--at2 <film seconds>]
"""
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = str(ROOT / 'assets/fonts/NotoSansSC-VF.ttf')


def grab(film, t, path):
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-ss', f'{t:.3f}', '-i', str(film), '-frames:v', '1', str(path)], check=True)
    return Image.open(path).convert('RGB')


def font(size, weight=400):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def compose(img, size, focus, title_xy, align='left', scale=1.0):
    W, H = size
    s = max(W / img.width, H / img.height)
    im = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    fx, fy = focus[0] * im.width, focus[1] * im.height
    x0 = int(min(max(0, fx - W / 2), im.width - W)); y0 = int(min(max(0, fy - H / 2), im.height - H))
    im = im.crop((x0, y0, x0 + W, y0 + H))
    # Darken under the title for legibility.
    shade = Image.new('L', (W, H), 0); d = ImageDraw.Draw(shade)
    tx, ty = title_xy[0] * W, title_xy[1] * H
    d.ellipse((tx - W * 0.55, ty - H * 0.22, tx + W * 0.55, ty + H * 0.26), fill=150)
    shade = shade.filter(ImageFilter.GaussianBlur(W * 0.08))
    im = Image.composite(Image.new('RGB', (W, H), (6, 8, 12)), im, shade)
    d = ImageDraw.Draw(im)
    big = font(int(H * 0.16 * scale), 300); small = font(int(H * 0.035 * scale), 400)
    t1, t2 = '我在', '一个关于傻妞的故事'
    bw = d.textlength(t1, font=big); sw = d.textlength(t2, font=small)
    x = tx - (bw / 2 if align == 'center' else 0); x2 = tx - (sw / 2 if align == 'center' else 0)
    d.text((x, ty - H * 0.10 * scale), t1, font=big, fill=(244, 239, 230))
    d.text((x2 + (0 if align == 'center' else H * 0.006), ty + H * 0.085 * scale), t2, font=small, fill=(170, 182, 190))
    return im


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('film', type=Path); ap.add_argument('out', type=Path)
    ap.add_argument('--at', type=float, default=387.487); ap.add_argument('--at2', type=float, default=415.353)
    a = ap.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    face = grab(a.film, a.at, a.out / '_frame-a.png')
    eyes = grab(a.film, a.at2, a.out / '_frame-b.png')
    # Landscape: his profile on the left, title in the dark space past his ear.
    # Portrait: her eyes fill the middle; the title sits in the dark band above her.
    jobs = [('thumbnail-16x9.png', face, (1920, 1080), (0.50, 0.50), (0.69, 0.58), 'left', 1.0),
            ('cover-4x3.png', face, (1440, 1080), (0.453, 0.50), (0.775, 0.60), 'left', 0.7),
            ('cover-3x4.png', eyes, (1080, 1440), (0.50, 0.48), (0.50, 0.115), 'center', 0.75),
            ('cover-9x16.png', eyes, (1080, 1920), (0.50, 0.46), (0.50, 0.11), 'center', 0.62)]
    for name, src, size, focus, txy, align, sc in jobs:
        out = a.out / name
        if out.exists():
            sys.exit(f'exists, refusing to overwrite: {out}')
        compose(src, size, focus, txy, align, sc).save(out, optimize=True)
        print(out)


if __name__ == '__main__':
    main()
