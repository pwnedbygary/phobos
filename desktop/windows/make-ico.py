#!/usr/bin/env python3
"""Wraps a 256 x 256 PNG in a Windows .ico file (Windows reads PNG-compressed icons)."""
import struct
import sys

png_path, ico_path = sys.argv[1], sys.argv[2]
with open(png_path, "rb") as f:
    png = f.read()
if png[:8] != b"\x89PNG\r\n\x1a\n":
    sys.exit(f"{png_path} is not a PNG")
width, height = struct.unpack(">II", png[16:24])
header = struct.pack("<HHH", 0, 1, 1)
# Width and height 0 mean 256; 32 bits per pixel; the image follows the 22-byte header.
entry = struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, 1, 32, len(png), 22)
with open(ico_path, "wb") as f:
    f.write(header + entry + png)
