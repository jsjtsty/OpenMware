"""Write keys.local.txt (one 256-bit hex key per line) from the reference MwareStuff sources.

Keys are game-specific archive keys; they stay out of version control (see .gitignore).
Usage: .venv/Scripts/python.exe -I scripts/extract_keys.py [game-name-substring]
"""
import re, sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = (root / "ref/MwareStuff/NPK3Tool/Program.cs").read_text(encoding="utf-8-sig")
want = sys.argv[1] if len(sys.argv) > 1 else "You and Me and Her"
keys = []
for name, body in re.findall(r'\("([^"]+)", new byte\[\] \{(.*?)\}', src, re.S):
    if want in name:
        keys.append(bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body)).hex())
(root / "keys.local.txt").write_text("\n".join(keys) + "\n")
print(f"wrote {len(keys)} key(s) to keys.local.txt")
