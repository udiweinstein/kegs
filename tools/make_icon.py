import re, sys
from PIL import Image, ImageDraw, ImageFilter
src = sys.argv[1]
def load(p):
    rows = re.findall(r'\{([^}]*)\}', open(p).read())
    return [[int(x, 16) for x in r.split(',')] for r in rows]
font = load(src + '/kegsfont.h')        # 256 entries
heb = load(src + '/hebfont.h')          # 27 entries, 0x60..0x7a

S = 1024
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))

def rrect(d, box, r, **kw): d.rounded_rectangle(box, r, **kw)

# drop shadow
sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
ImageDraw.Draw(sh).rounded_rectangle((110, 130, 914, 934), 180, fill=(0, 0, 0, 110))
img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(24)))

d = ImageDraw.Draw(img)
# platinum case (vertical gradient)
case = Image.new('RGBA', (S, S), (0, 0, 0, 0))
cd = ImageDraw.Draw(case)
for y in range(100, 925):
    t = (y - 100) / 825
    c = (int(232 - 40 * t), int(228 - 42 * t), int(214 - 44 * t), 255)
    cd.line((100, y, 924, y), fill=c)
mask = Image.new('L', (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle((100, 100, 924, 924), 180, fill=255)
img.paste(case, (0, 0), mask)
d = ImageDraw.Draw(img)
d.rounded_rectangle((100, 100, 924, 924), 180, outline=(150, 145, 130, 255), width=6)

# screen bezel and glass
d.rounded_rectangle((180, 170, 844, 700), 70, fill=(70, 68, 62, 255))
d.rounded_rectangle((215, 205, 809, 665), 50, fill=(12, 22, 14, 255))

# pixel text on the screen (green phosphor)
G = (90, 255, 120, 255)
def glyph(rows, x, y, px):
    for r, bits in enumerate(rows):
        for c in range(7):
            if bits & (0x80 >> c):
                d.rectangle((x + c * px, y + r * px, x + (c + 1) * px - 2, y + (r + 1) * px - 2), fill=G)
px = 11
def ascii_rows(ch): return font[0x80 + (ord(ch) & 0x3f) + (0x00 if ord(ch) >= 0x40 else 0x00)] if False else font[0x80 | (ord(ch) & 0x7f)]
# line 1: ]KEGS
x0, y0 = 250, 290
for i, ch in enumerate(']KEGS'):
    glyph(ascii_rows(ch), x0 + i * 8 * px, y0, px)
# line 2: Hebrew "שלום" right-to-left, right-aligned
word = [0x79, 0x6c, 0x65, 0x6d]           # ש ל ו ם
xr = 250 + 6*8*11 - 8*11
for i, code in enumerate(word):
    glyph(heb[code - 0x60], xr - (i + 1) * 8 * px, y0 + 12 * px, px)
# cursor block
d.rectangle((x0 + 5 * 8 * px, y0, x0 + 5 * 8 * px + 7 * px - 2, y0 + 8 * px - 2), fill=G)

# screen glare
gl = Image.new('RGBA', (S, S), (0, 0, 0, 0))
ImageDraw.Draw(gl).polygon([(230, 215), (520, 215), (330, 650), (230, 650)], fill=(255, 255, 255, 18))
img.alpha_composite(gl)
d = ImageDraw.Draw(img)

# front: drive slot and power LED
d.rounded_rectangle((520, 770, 820, 800), 14, fill=(95, 92, 84, 255))
d.ellipse((215, 765, 255, 805), fill=(60, 220, 90, 255))
img.save('icon_1024.png')
