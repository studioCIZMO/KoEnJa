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
