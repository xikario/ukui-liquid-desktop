#!/usr/bin/env python3
"""Wait for two nonblank Xvfb frames; fail with a named artifact on timeout."""
import subprocess
import sys
import time
from PIL import Image, ImageStat

path = sys.argv[1]
deadline = time.monotonic() + 5
ready = 0
while time.monotonic() < deadline:
    subprocess.run(["import", "-window", "root", path], check=True)
    with Image.open(path) as image:
        nonblank = sum(ImageStat.Stat(image.convert("RGB")).var) >= 50
    ready = ready + 1 if nonblank else 0
    if ready >= 2:
        sys.exit(0)
    time.sleep(.1)
raise SystemExit("No painted frame before timeout: " + path)
