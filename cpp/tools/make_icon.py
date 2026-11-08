"""Renders app/icon/xournal-lab.svg into app/icon/xournal-lab.ico (16..256 px) and app/icon/xournal-lab-256.png.
Needs Inkscape on PATH and Pillow. The results are committed, so building the app needs neither."""
import os
import subprocess
import tempfile
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
icon_dir = os.path.normpath(os.path.join(here, "..", "app", "icon"))
svg = os.path.join(icon_dir, "xournal-lab.svg")
sizes = [16, 24, 32, 48, 64, 128, 256]

with tempfile.TemporaryDirectory() as tmp:
    images = []
    for s in sizes:
        out = os.path.join(tmp, f"{s}.png")
        subprocess.run(["inkscape", svg, "--export-type=png", f"--export-filename={out}", "-w", str(s), "-h", str(s)], check=True, capture_output=True)
        images.append(Image.open(out).convert("RGBA"))
    big = images[-1]
    big.save(os.path.join(icon_dir, "xournal-lab-256.png"))
    big.save(os.path.join(icon_dir, "xournal-lab.ico"), format="ICO", sizes=[(s, s) for s in sizes], append_images=images[:-1])
print("ok")
