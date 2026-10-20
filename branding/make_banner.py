"""Render branding/banner.png (1500x500) from branding/logo.png."""
from PIL import Image, ImageDraw, ImageFont

W, H = 1500, 500
NAVY = (0, 31, 78)
SQUARE = (12, 46, 99)
GOLD = (222, 178, 82)
WHITE = (255, 255, 255)
MUTED = (150, 170, 205)
FONTS = "C:/Windows/Fonts/"

img = Image.new("RGB", (W, H), NAVY)

# A chessboard that fades in towards the right edge
board = Image.new("RGB", (W, H), NAVY)
d = ImageDraw.Draw(board)
S = 62
for r in range(H // S + 1):
    for c in range(W // S + 1):
        if (r + c) % 2 == 0:
            d.rectangle([c * S, r * S, c * S + S - 1, r * S + S - 1], fill=SQUARE)
mask = Image.new("L", (W, H))
md = ImageDraw.Draw(mask)
start = 820
for x in range(start, W):
    md.line([(x, 0), (x, H)], fill=int(255 * ((x - start) / (W - start)) ** 1.4))
img.paste(board, (0, 0), mask)

# Logo (its own background is the same navy)
logo = Image.open("branding/logo.png").convert("RGB").resize((420, 420), Image.LANCZOS)
img.paste(logo, (60, 40))

draw = ImageDraw.Draw(img)
title = ImageFont.truetype(FONTS + "segoeuib.ttf", 112)
sub = ImageFont.truetype(FONTS + "seguisb.ttf", 54)
small = ImageFont.truetype(FONTS + "segoeui.ttf", 30)
x = 520
draw.text((x, 120), "Capablanca", font=title, fill=GOLD)
draw.text((x, 262), "Attacking chess engine", font=sub, fill=WHITE)
draw.rectangle([x + 4, 352, x + 84, 356], fill=GOLD)
draw.text((x, 372), "Written from scratch  ·  UCI  ·  Sound sacrifices", font=small, fill=MUTED)

img.save("branding/banner.png")
print("wrote branding/banner.png")
