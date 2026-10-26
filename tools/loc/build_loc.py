"""Builds data/loc/en.json and data/loc/de.json from the string tables (flat dotted keys)."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import strings_content  # noqa: E402
import strings_ui  # noqa: E402

root = os.path.join(os.path.dirname(__file__), "..", "..", "data", "loc")
tables = {}
for mod in (strings_ui, strings_content):
    for k, v in mod.S.items():
        if k in tables:
            raise SystemExit(f"duplicate key {k}")
        tables[k] = v
for idx, lang in enumerate(("en", "de")):
    out = {k: v[idx] for k, v in sorted(tables.items())}
    with open(os.path.join(root, f"{lang}.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
print(f"{len(tables)} strings written")
