#!/usr/bin/env python3
"""Extract a Terminal Reality POD1 archive to loose files.

POD1 layout: u32 fileCount, 80-byte comment, then `count` directory entries of
{ char name[32]; u32 size; u32 offset } (40 bytes each), followed by file data.
Fury3's data lives in startup.pod / fury3.pod; the game opens assets as loose
files (e.g. ART\\VGA.ACT) and falls back to disk, so extracting the PODs next to
the game lets those opens succeed.

Usage: py -3 tools/extract_pod.py <pod-file> <out-dir>
"""
import struct, os, sys

def extract(pod, outdir):
    d = open(pod, "rb").read()
    count = struct.unpack_from("<I", d, 0)[0]
    entries = []
    base = 4
    data_start = len(d)
    while base + 40 <= len(d):
        raw = d[base:base + 32]
        name = raw.split(b"\x00")[0].decode("latin1", "replace").strip()
        size, off = struct.unpack_from("<II", d, base + 32)
        base += 40
        if size and off and off + size <= len(d) and name and \
           all(32 <= ord(c) < 127 for c in name):
            entries.append((name, size, off))
            data_start = min(data_start, off)
        if base >= data_start:
            break
    n = 0
    for name, size, off in entries:
        rel = name.replace("\\", "/")
        path = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(d[off:off + size])
        n += 1
    print(f"{os.path.basename(pod)}: extracted {n} files (header count={count})")

if __name__ == "__main__":
    extract(sys.argv[1], sys.argv[2])
