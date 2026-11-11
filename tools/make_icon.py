# Draws the KEGSMAC app icon (3.5" floppy with a KEGS label) and writes
# lib/kegsudi.icns.  Run from the repository root:  python3 tools/make_icon.py
from PIL import Image, ImageDraw, ImageFilter, ImageFont
S = 1024
img = Image.new('RGBA', (S, S), (0, 0, 0, 0))
sh = Image.new('RGBA', (S, S), (0, 0, 0, 0))
ImageDraw.Draw(sh).rounded_rectangle((110, 130, 914, 934), 180, fill=(0, 0, 0, 110))
img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(24)))
# blue rounded-square background with a vertical gradient
g = Image.new('RGBA', (S, S)); gd = ImageDraw.Draw(g)
top, bot = (70, 110, 200), (30, 55, 130)
for y in range(100, 925):
    t = (y - 100) / 824
    gd.line((100, y, 924, y), fill=tuple(int(a + (b - a) * t) for a, b in zip(top, bot)) + (255,))
m = Image.new('L', (S, S), 0)
ImageDraw.Draw(m).rounded_rectangle((100, 100, 924, 924), 180, fill=255)
img.paste(g, (0, 0), m)
d = ImageDraw.Draw(img)
# the floppy
d.rounded_rectangle((210, 190, 814, 834), 40, fill=(48, 50, 58, 255))
d.rectangle((370, 190, 700, 420), fill=(190, 195, 205, 255))      # shutter
d.rectangle((560, 225, 640, 385), fill=(48, 50, 58, 255))         # shutter window
d.rounded_rectangle((290, 500, 734, 834), 20, fill=(245, 243, 236, 255))  # label
for y in range(570, 800, 48):
    d.line((320, y, 704, y), fill=(170, 190, 230, 255), width=4)
font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', 120)
d.text((512, 560), "KEGS", font=font, fill=(30, 55, 130, 255), anchor='mm')
d.rectangle((230, 770, 262, 802), fill=(30, 30, 36, 255))         # write-protect hole
img.save('lib/kegsudi.icns',
	sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
