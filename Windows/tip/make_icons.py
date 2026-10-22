# Makes the TIP's icons (run on the Mac; the .ico files are committed): KoEnJa.ico from the iOS app
# icon, for the profile and the input indicator while KoEnJa is on, and Off.ico, an "A" on the same
# gray, for while it is off (keys go to the app as plain English).
import os
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
sizes = [(s, s) for s in (16, 20, 24, 32, 48)]
icon = Image.open(os.path.join(here, "../../iOS/App/Assets.xcassets/AppIcon.appiconset/icon.png")).convert("RGBA")
icon.save(os.path.join(here, "KoEnJa.ico"), sizes=sizes)

off = Image.new("RGBA", (256, 256), (217, 217, 217, 255))
font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 230, index=1)  # Helvetica Bold
ImageDraw.Draw(off).text((128, 136), "A", font=font, fill="black", anchor="mm")
off.save(os.path.join(here, "Off.ico"), sizes=sizes)

# Ko.ico / Ja.ico: 한 or あ, the input indicator while KoEnJa is on (which language goes first).
# Drawn at each size, not scaled down from 256, so the 16 px one stays crisp.
def glyph(text, path, index, name):
    images = []
    for s, _ in sizes:
        image = Image.new("RGBA", (s, s), (217, 217, 217, 255))
        font = ImageFont.truetype(path, round(s * 0.85), index=index)
        draw = ImageDraw.Draw(image)
        left, top, right, bottom = draw.textbbox((0, 0), text, font=font)  # the ink, centred
        draw.text(((s - left - right) / 2, (s - top - bottom) / 2), text, font=font, fill="black")
        images.append(image)
    images[-1].save(os.path.join(here, name), sizes=sizes, append_images=images[:-1])

glyph("한", "/System/Library/Fonts/AppleSDGothicNeo.ttc", 6, "Ko.ico")  # Apple SD Gothic Neo Bold
glyph("あ", "/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc", 0, "Ja.ico")
