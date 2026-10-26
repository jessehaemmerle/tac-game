"""Downloads ElevenLabs generations listed in saved flow-run status JSON files.

usage: fetch_generations.py <sessions.txt> <out_dir> <status.json>...
sessions.txt lines: "<category> <key> <session_id>..." (category: sfx, de, en)
Files are written as <out_dir>/<category>/<key>_<n>.mp3 (n = position of the session in the line).
"""
import json
import os
import re
import sys
import urllib.request


def main() -> None:
    sessions_file, out_dir, *status_files = sys.argv[1:]
    where = {}
    for line in open(sessions_file, encoding="utf-8"):
        parts = line.split()
        if len(parts) < 3:
            continue
        cat, key, ids = parts[0], parts[1], parts[2:]
        for n, sid in enumerate(ids):
            where[sid] = (cat, key, n)
    done = 0
    for sf in status_files:
        data = json.load(open(sf, encoding="utf-8"))
        for g in data.get("generations", []):
            if g.get("status") != "completed":
                print("not completed:", g.get("id"))
                continue
            url = g["content_url"]
            m = re.search(r"/content_generation/([^/]+)/", url)
            if not m or m.group(1) not in where:
                print("unknown session in", g.get("id"))
                continue
            cat, key, n = where[m.group(1)]
            os.makedirs(os.path.join(out_dir, cat), exist_ok=True)
            dst = os.path.join(out_dir, cat, f"{key}_{n}.mp3")
            if not os.path.exists(dst):
                urllib.request.urlretrieve(url, dst)
            done += 1
    print(f"{done} files present")


if __name__ == "__main__":
    main()
