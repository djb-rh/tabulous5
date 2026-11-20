#!/usr/bin/env python3
"""Draws the home-screen icons for our apps: 88x88, rounded tile, one flat mark each, in the launcher's own style.

    make_icons.py            -> <app>/icon.png for each of our apps, and sheet.png, here
Drawn at eight times the size and scaled down, so the edges come out smooth.
"""
import math, os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
S = 8; N = 88 * S
FONT = "/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf"

def tile(top, bottom, radius=20):
    """A rounded square with a top-to-bottom gradient and a soft sheen along the top."""
    g = Image.new("RGBA", (N, N))
    px = g.load()
    for y in range(N):
        t = y / (N - 1)
        c = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,)
        for x in range(N): px[x, y] = c
    sheen = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    ImageDraw.Draw(sheen).ellipse([-N * 0.3, -N * 0.95, N * 1.3, N * 0.42], fill=(255, 255, 255, 26))
    g.alpha_composite(sheen)
    mask = Image.new("L", (N, N), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, N - 1, N - 1], radius * S, fill=255)
    g.putalpha(mask)
    return g

def p(v): return v * S
def shadow(im, draw_fn, off=2.0, alpha=70):
    """Draws a shape's shadow: the same shape, dark, a little down and right."""
    layer = Image.new("RGBA", (N, N), (0, 0, 0, 0)); draw_fn(ImageDraw.Draw(layer), p(off), (0, 0, 0, alpha)); im.alpha_composite(layer)

def heart(d, cx, cy, r, fill):
    d.ellipse([cx - 2 * r, cy - r, cx, cy + r], fill=fill); d.ellipse([cx, cy - r, cx + 2 * r, cy + r], fill=fill)
    d.polygon([(cx - 1.93 * r, cy + 0.35 * r), (cx + 1.93 * r, cy + 0.35 * r), (cx, cy + 2.55 * r)], fill=fill)

def spade(d, cx, cy, r, fill):
    d.ellipse([cx - 2 * r, cy - r, cx, cy + r], fill=fill); d.ellipse([cx, cy - r, cx + 2 * r, cy + r], fill=fill)
    d.polygon([(cx - 1.93 * r, cy - 0.35 * r), (cx + 1.93 * r, cy - 0.35 * r), (cx, cy - 2.55 * r)], fill=fill)
    d.polygon([(cx, cy - 0.2 * r), (cx - 0.95 * r, cy + 2.1 * r), (cx + 0.95 * r, cy + 2.1 * r)], fill=fill)

def arcade():
    im = tile((92, 64, 200), (44, 24, 110))
    def stick(d, o, col=None):
        base = col or (28, 22, 54, 255)
        d.rounded_rectangle([p(14) + o, p(58) + o, p(74) + o, p(74) + o], p(8), fill=base)                       # the panel
        d.rounded_rectangle([p(27.5) + o, p(30) + o, p(32.5) + o, p(62) + o], p(2.5), fill=col or (200, 204, 216, 255))  # the shaft
        d.ellipse([p(18) + o, p(12) + o, p(42) + o, p(36) + o], fill=col or (235, 64, 64, 255))                  # the ball
        d.ellipse([p(47) + o, p(47) + o, p(59) + o, p(59) + o], fill=col or (255, 204, 51, 255))                 # two buttons
        d.ellipse([p(60) + o, p(40) + o, p(72) + o, p(52) + o], fill=col or (64, 212, 232, 255))
    shadow(im, lambda d, o, c: stick(d, o, c))
    d = ImageDraw.Draw(im); stick(d, 0)
    d.ellipse([p(22.5), p(15.5), p(30), p(23)], fill=(255, 160, 160, 255))                                          # the ball's shine
    d.ellipse([p(25.5), p(58.5), p(34.5), p(63.5)], fill=(60, 50, 100, 255))                                        # the shaft's boot
    return im

def minesweeper():
    im = tile((96, 122, 150), (46, 62, 84))
    cx, cy, r = p(44), p(46), p(19)
    def mine(d, o, col=None):
        ink = col or (22, 26, 34, 255)
        for k in range(8):
            a = k * math.pi / 4; long_ = r * (1.55 if k % 2 == 0 else 1.32); w = p(3.1)
            x1, y1 = cx + o + math.cos(a) * long_, cy + o + math.sin(a) * long_
            d.line([(cx + o, cy + o), (x1, y1)], fill=ink, width=round(w))
            d.ellipse([x1 - w / 2, y1 - w / 2, x1 + w / 2, y1 + w / 2], fill=ink)
        d.ellipse([cx - r + o, cy - r + o, cx + r + o, cy + r + o], fill=ink)
    shadow(im, lambda d, o, c: mine(d, o, c), off=2.5, alpha=80)
    d = ImageDraw.Draw(im); mine(d, 0)
    d.ellipse([cx - r * 0.55, cy - r * 0.6, cx - r * 0.05, cy - r * 0.1], fill=(255, 255, 255, 235))               # its shine
    d.ellipse([cx - r * 0.02, cy - r * 0.62, cx + r * 0.2, cy - r * 0.4], fill=(255, 255, 255, 150))
    return im

def sudoku():
    im = tile((40, 178, 132), (18, 106, 96))
    x0, y0, w = p(13), p(13), p(62)
    layer = Image.new("RGBA", (N, N), (0, 0, 0, 0)); ImageDraw.Draw(layer).rounded_rectangle([x0 + p(2), y0 + p(2.5), x0 + w + p(2), y0 + w + p(2.5)], p(9), fill=(0, 0, 0, 60)); im.alpha_composite(layer)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([x0, y0, x0 + w, y0 + w], p(9), fill=(250, 252, 250, 255))
    c = w / 3
    d.rectangle([x0 + c, y0 + c, x0 + 2 * c, y0 + 2 * c], fill=(255, 221, 102, 255))                                # the cell being filled
    for k in (1, 2):
        d.rectangle([x0 + c * k - p(0.9), y0 + p(3), x0 + c * k + p(0.9), y0 + w - p(3)], fill=(40, 60, 70, 255))
        d.rectangle([x0 + p(3), y0 + c * k - p(0.9), x0 + w - p(3), y0 + c * k + p(0.9)], fill=(40, 60, 70, 255))
    f = ImageFont.truetype(FONT, round(p(15.5)))
    for (col, row, ch, ink) in ((0, 0, "5", (40, 60, 70)), (2, 0, "3", (40, 60, 70)), (1, 1, "7", (200, 70, 30)), (0, 2, "9", (40, 60, 70)), (2, 2, "1", (40, 60, 70)), (2, 1, "4", (40, 60, 70))):
        d.text((x0 + c * (col + 0.5), y0 + c * (row + 0.5) + p(0.4)), ch, font=f, fill=ink + (255,), anchor="mm")
    return im

def solitaire():
    im = tile((44, 150, 86), (14, 84, 50))
    f = ImageFont.truetype(FONT, round(p(17)))
    def card(angle, cx, cy, draw_face):
        w, h = p(36), p(50)
        c = Image.new("RGBA", (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(c)
        d.rounded_rectangle([N / 2 - w / 2, N / 2 - h / 2, N / 2 + w / 2, N / 2 + h / 2], p(5.5), fill=(252, 252, 250, 255), outline=(210, 214, 210, 255), width=round(p(0.5)))
        draw_face(d, N / 2 - w / 2, N / 2 - h / 2, w, h)
        c = c.rotate(angle, resample=Image.BICUBIC, center=(N / 2, N / 2), translate=(cx - N / 2, cy - N / 2))
        sh = Image.new("RGBA", (N, N), (0, 0, 0, 0)); sh.paste((0, 0, 0, 70), (round(p(1.8)), round(p(2.2))), c.split()[3])
        im.alpha_composite(sh); im.alpha_composite(c)
    def red(d, x, y, w, h):
        d.text((x + p(8.5), y + p(10.5)), "K", font=f, fill=(214, 48, 49, 255), anchor="mm"); heart(d, x + w / 2, y + h * 0.56, p(5.6), (214, 48, 49, 255))
    def black(d, x, y, w, h):
        d.text((x + p(8.5), y + p(10.5)), "A", font=f, fill=(26, 30, 36, 255), anchor="mm"); spade(d, x + w / 2, y + h * 0.60, p(5.6), (26, 30, 36, 255))
    card(13, p(34), p(44), red)
    card(-9, p(53), p(47), black)
    mask = Image.new("L", (N, N), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, N - 1, N - 1], 20 * S, fill=255)
    im.putalpha(Image.composite(im.split()[3], Image.new("L", (N, N), 0), mask))                                    # nothing outside the tile
    return im

def phrasecraze():
    im = tile((240, 108, 62), (176, 52, 26))
    def bubble(d, o, col=None):
        ink = col or (255, 250, 244, 255)
        d.rounded_rectangle([p(12) + o, p(16) + o, p(76) + o, p(60) + o], p(15), fill=ink)
        d.polygon([(p(24) + o, p(56) + o), (p(22) + o, p(76) + o), (p(42) + o, p(58) + o)], fill=ink)             # the tail
    shadow(im, lambda d, o, c: bubble(d, o, c), off=2.5, alpha=70)
    d = ImageDraw.Draw(im); bubble(d, 0)
    for k, cx in enumerate((28, 44, 60)):                                                                         # "...": someone is describing it
        d.ellipse([p(cx - 5), p(33), p(cx + 5), p(43)], fill=(200, 64, 30, 255))
    return im

def fivehead():
    im = tile((58, 110, 236), (22, 54, 150))
    def head(d, o, col=None):
        skin = col or (250, 214, 170, 255)
        d.ellipse([p(22) + o, p(26) + o, p(66) + o, p(78) + o], fill=skin)                                         # the face
        d.rounded_rectangle([p(34) + o, p(70) + o, p(54) + o, p(92) + o], p(6), fill=skin)                         # the neck, off the tile's edge
    def card(d, o, col=None):
        d.rounded_rectangle([p(14) + o, p(12) + o, p(74) + o, p(40) + o], p(6), fill=col or (255, 255, 255, 255))  # the tablet on the forehead
    shadow(im, lambda d, o, c: head(d, o, c), off=2.0, alpha=60)
    d = ImageDraw.Draw(im); head(d, 0)
    d.ellipse([p(32), p(50), p(38), p(56)], fill=(40, 44, 60, 255)); d.ellipse([p(50), p(50), p(56), p(56)], fill=(40, 44, 60, 255))   # eyes
    d.arc([p(34), p(54), p(54), p(70)], 20, 160, fill=(170, 80, 60, 255), width=round(p(2.2)))                    # a grin
    shadow(im, lambda d, o, c: card(d, o, c), off=2.0, alpha=80)
    d = ImageDraw.Draw(im); card(d, 0)
    d.rounded_rectangle([p(14), p(12), p(74), p(40)], p(6), outline=(28, 40, 90, 255), width=round(p(1.6)))
    f = ImageFont.truetype(FONT, round(p(17)))
    d.text((p(44), p(26.5)), "5", font=f, fill=(29, 78, 216, 255), anchor="mm")
    mask = Image.new("L", (N, N), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, N - 1, N - 1], 20 * S, fill=255)
    im.putalpha(Image.composite(im.split()[3], Image.new("L", (N, N), 0), mask))
    return im

def joshua():
    im = tile((26, 34, 30), (6, 10, 8))                                                                            # a terminal's glass
    d = ImageDraw.Draw(im)
    green, dim = (96, 255, 150, 255), (40, 120, 70, 255)
    x0, y0, w = p(16), p(16), p(56); c = w / 3
    for k in (1, 2):
        d.rounded_rectangle([x0 + c * k - p(1.3), y0, x0 + c * k + p(1.3), y0 + w], p(1.3), fill=dim)
        d.rounded_rectangle([x0, y0 + c * k - p(1.3), x0 + w, y0 + c * k + p(1.3)], p(1.3), fill=dim)
    def X(col, row):
        cx, cy, r = x0 + c * (col + 0.5), y0 + c * (row + 0.5), c * 0.28
        d.line([(cx - r, cy - r), (cx + r, cy + r)], fill=green, width=round(p(3))); d.line([(cx - r, cy + r), (cx + r, cy - r)], fill=green, width=round(p(3)))
    def O(col, row):
        cx, cy, r = x0 + c * (col + 0.5), y0 + c * (row + 0.5), c * 0.30
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(255, 196, 60, 255), width=round(p(3)))
    X(0, 0); O(1, 1); X(2, 2); O(2, 0); X(0, 2)
    return im

icons = {"arcade": arcade(), "minesweeper": minesweeper(), "sudoku": sudoku(), "solitaire": solitaire(),
         "phrasecraze": phrasecraze(), "fivehead": fivehead(), "joshua": joshua()}
sheet = Image.new("RGBA", (len(icons) * 200 + 40, 200 + 130), (12, 18, 36, 255))
for i, (name, big) in enumerate(icons.items()):
    small = big.resize((88, 88), Image.LANCZOS)
    os.makedirs(os.path.join(HERE, name), exist_ok=True)
    small.save(os.path.join(HERE, name, "icon.png"))
    sheet.alpha_composite(big.resize((176, 176), Image.LANCZOS), (30 + i * 200, 20))
    sheet.alpha_composite(small, (30 + i * 200 + 44, 220))
sheet.convert("RGB").save(os.path.join(HERE, "sheet.png"))
print("wrote", ", ".join(n + "/icon.png" for n in icons), "and sheet.png")
