#!/usr/bin/env python3
"""Write a minimal gamedata/mission.ini campaign manifest.

mission.ini is the campaign manifest the game reads on "New Game": a mission
count followed by one .LVL filename per line. It is not shipped with the game
(the original install and the PODs contain no mission.ini -- the game only ever
reads it), so we synthesize one. Each listed .LVL must exist under gamedata/
(resolved by the runtime's basename index, e.g. LEVELS/ATMOS.LVL).

Usage: py -3 tools/make_mission_ini.py [LEVEL.LVL ...]   (default: ATMOS.LVL)
"""
import os, sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
levels = sys.argv[1:] or ["ATMOS.LVL"]
path = os.path.join(HERE, "gamedata", "mission.ini")
with open(path, "wb") as f:                     # CRLF, matching the .LVL files
    f.write(f"{len(levels)}\r\n".encode())
    for lv in levels:
        f.write(f"{lv}\r\n".encode())
print(f"wrote {path}: {len(levels)} mission(s): {', '.join(levels)}")
