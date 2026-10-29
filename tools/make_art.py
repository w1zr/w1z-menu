"""Draws the w1z's menu artwork (original, generated in code; fonts are the
Windows system fonts Segoe UI / Segoe UI Semibold).

Writes:
  art/logo.png                 512x512 emblem
  package/fluffy/screenshot.png 1280x720 Fluffy Mod Manager preview

usage: python tools/make_art.py
"""
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "Fonts")

BG = (11, 12, 14)
PANEL = (20, 21, 24)
ROW_SEL = (44, 46, 50)
LINE = (52, 55, 60)
TEXT = (236, 236, 236)
DIM = (140, 143, 148)
FAINT = (86, 89, 94)
AMBER = (245, 166, 35)

S = 2  # supersampling


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size * S)


def emblem(size):
    """The menu reduced to a symbol: a tile with three rows, the middle one
    selected (amber bar + chevron), and an amber rule under the 'header'."""
    W = size * S
    img = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(W * 0.16)
    d.rounded_rectangle([0, 0, W - 1, W - 1], radius=r, fill=PANEL + (255,), outline=LINE + (255,), width=max(2, W // 160))

    pad = int(W * 0.17)
    # header bar + amber rule
    hy = int(W * 0.22)
    d.rounded_rectangle([pad, hy, W - pad, hy + int(W * 0.045)], radius=int(W * 0.02), fill=TEXT + (255,))
    ry = hy + int(W * 0.105)
    d.rectangle([int(W * 0.08), ry, W - int(W * 0.08), ry + max(2, int(W * 0.012))], fill=AMBER + (255,))

    rowh = int(W * 0.15)
    top = ry + int(W * 0.06)
    for i in range(3):
        y0 = top + i * (rowh + int(W * 0.025))
        sel = i == 1
        if sel:
            d.rectangle([int(W * 0.08), y0, W - int(W * 0.08), y0 + rowh], fill=ROW_SEL + (255,))
            d.rectangle([int(W * 0.08), y0, int(W * 0.08) + max(3, int(W * 0.022)), y0 + rowh], fill=AMBER + (255,))
        bw = [0.42, 0.50, 0.34][i]
        ly = y0 + rowh // 2
        th = max(3, int(W * 0.034))
        d.rounded_rectangle([pad, ly - th // 2, pad + int(W * bw), ly + th // 2], radius=th // 2,
                            fill=(TEXT if sel else DIM) + (255,))
        # right side: chevron on the selected row, small pills on the others
        cx = W - pad
        if sel:
            c = int(W * 0.045)
            lw = max(3, int(W * 0.026))
            d.line([(cx - c, ly - c), (cx, ly), (cx - c, ly + c)], fill=AMBER + (255,), width=lw, joint="curve")
        else:
            pw = int(W * 0.09)
            d.rounded_rectangle([cx - pw, ly - th // 2, cx, ly + th // 2], radius=th // 2, fill=FAINT + (255,))
    return img.resize((size, size), Image.LANCZOS)


def spaced(draw, xy, text, fnt, fill, spacing):
    x, y = xy
    for ch in text:
        draw.text((x, y), ch, font=fnt, fill=fill)
        x += draw.textlength(ch, font=fnt) + spacing
    return x


def spaced_width(draw, text, fnt, spacing):
    return sum(draw.textlength(ch, font=fnt) for ch in text) + spacing * (len(text) - 1)


def logo():
    out = os.path.join(ROOT, "art")
    os.makedirs(out, exist_ok=True)
    emblem(512).save(os.path.join(out, "logo.png"))


def screenshot():
    W, H = 1280 * S, 720 * S
    img = Image.new("RGB", (W, H), BG)
    # soft amber glow behind the emblem, very subtle
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse([int(W * 0.08), int(H * 0.15), int(W * 0.46), int(H * 0.85)], fill=40)
    glow = glow.filter(ImageFilter.GaussianBlur(120 * S))
    img = Image.composite(Image.new("RGB", (W, H), (60, 40, 10)), img, glow)
    d = ImageDraw.Draw(img)

    e = emblem(300).resize((300 * S, 300 * S), Image.LANCZOS)
    ex, ey = int(W * 0.12), (H - 300 * S) // 2
    img.paste(e, (ex, ey), e)

    tx = int(W * 0.45)
    title = font("seguisb.ttf", 76)
    d.text((tx, int(H * 0.30)), "", font=title)
    end = spaced(d, (tx, int(H * 0.31)), "W1Z'S MENU", title, TEXT, 14 * S)
    d.rectangle([tx, int(H * 0.47), end - 14 * S, int(H * 0.47) + 4 * S], fill=AMBER)
    sub = font("segoeui.ttf", 24)
    spaced(d, (tx, int(H * 0.51)), "IN-GAME MOD MENU", sub, DIM, 6 * S)
    spaced(d, (tx, int(H * 0.56)), "THE LAST OF US PART I  \u00b7  PC", sub, DIM, 6 * S)

    small = font("segoeui.ttf", 20)
    keys = [("Insert / F9", "open the menu"), ("L3 + R3", "on a controller")]
    y = int(H * 0.68)
    for k, v in keys:
        kw = d.textlength(k, font=small)
        d.rounded_rectangle([tx, y - 4 * S, tx + kw + 24 * S, y + 30 * S], radius=6 * S, outline=LINE, width=2 * S)
        d.text((tx + 12 * S, y), k, font=small, fill=TEXT)
        d.text((tx + kw + 40 * S, y), v, font=small, fill=FAINT)
        y += 48 * S

    img = img.resize((1280, 720), Image.LANCZOS)
    out = os.path.join(ROOT, "package", "fluffy")
    os.makedirs(out, exist_ok=True)
    img.save(os.path.join(out, "screenshot.png"), optimize=True)


if __name__ == "__main__":
    logo()
    screenshot()
    print("art/logo.png and package/fluffy/screenshot.png written")
