"""Convert PNG to RGB565 C header for TFT_eSPI pushImage (stored in PROGMEM)."""
from PIL import Image
import struct, sys

TARGET_W, TARGET_H = 480, 320
img = Image.open(r"e:\reef 30032026_03042026_07042026\image.png").convert("RGB")
img = img.resize((TARGET_W, TARGET_H), Image.LANCZOS)

pixels = list(img.getdata())
rgb565 = []
for r, g, b in pixels:
    # RGB565: RRRRRGGGGGGBBBBB  (big-endian for TFT_eSPI pushImage)
    val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    # TFT_eSPI pushImage expects 16-bit values, store as uint16_t
    rgb565.append(val)

out = r"e:\reef 30032026_03042026_07042026\src\splash.h"
with open(out, "w") as f:
    f.write("// Auto-generated splash screen 480x320 RGB565\n")
    f.write("#pragma once\n")
    f.write("#include <pgmspace.h>\n\n")
    f.write(f"#define SPLASH_W {TARGET_W}\n")
    f.write(f"#define SPLASH_H {TARGET_H}\n\n")
    f.write(f"const uint16_t splash_img[{TARGET_W*TARGET_H}] PROGMEM = {{\n")
    for i, val in enumerate(rgb565):
        if i % 16 == 0:
            f.write("  ")
        f.write(f"0x{val:04X},")
        if i % 16 == 15:
            f.write("\n")
    f.write("\n};\n")

print(f"Done! Wrote {len(rgb565)} pixels ({len(rgb565)*2} bytes) to {out}")
