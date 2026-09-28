#!/usr/bin/env python3
"""Fetches candidate portraits from Japanese Wikipedia / Wikimedia Commons.

For SMD candidates in data/election/2026/candidates.json (by default only
winners, 比例復活 winners and former/incumbent members: 前/元) without
assets/photos/<id>.jpg, looks up the ja.wikipedia article by the legal name
(name_legal, then "<name> (政治家)"), checks that the article's intro is about
a politician (政治家 / 議員), downloads its lead image from Commons, downsizes
it (longest side 400 px, needs Pillow) and records author/licence in
assets/photos/CREDITS.json.

Only images whose Commons licence is free (CC-BY*, CC0, PD, GFDL) are kept.
Several candidates share names with other people: review with --dry-run.

  python tools/fetch_photos.py --dry-run
  python tools/fetch_photos.py --all            # every SMD candidate
  python tools/fetch_photos.py --only 13-01-01,01-01-04
"""
import argparse
import io
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = "jpy_election/0.1 (candidate photo fetcher; https://github.com/)"
FREE = re.compile(r"^(cc[- ]by|cc0|public domain|pd|gfdl)", re.I)
sys.stdout.reconfigure(encoding="utf-8")


def get_json(url):
    for attempt in range(4):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=30) as r:
                return json.load(r)
        except Exception:  # rate limits / transient errors
            if attempt == 3:
                raise
            time.sleep(2 * (attempt + 1))


def get_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def article(title):
    q = urllib.parse.urlencode({
        "action": "query", "format": "json", "prop": "pageimages|pageprops|extracts",
        "piprop": "name", "exintro": 1, "explaintext": 1, "exchars": 400, "titles": title, "redirects": 1})
    pages = get_json("https://ja.wikipedia.org/w/api.php?" + q)["query"]["pages"]
    for page in pages.values():
        if "missing" in page or "disambiguation" in page.get("pageprops", {}):
            return None
        return {"title": page.get("title"), "image": page.get("pageimage"), "intro": page.get("extract", "")}
    return None


def commons_info(filename):
    q = urllib.parse.urlencode({
        "action": "query", "format": "json", "prop": "imageinfo",
        "iiprop": "url|extmetadata", "iiurlwidth": 400, "titles": "File:" + filename})
    pages = get_json("https://commons.wikimedia.org/w/api.php?" + q)["query"]["pages"]
    for page in pages.values():
        info = page.get("imageinfo", [{}])[0]
        meta = info.get("extmetadata", {})
        strip = lambda k: re.sub("<[^>]+>", "", meta.get(k, {}).get("value", "")).strip()  # noqa: E731
        return {
            "thumb": info.get("thumburl") or info.get("url"),
            "page": info.get("descriptionurl"),
            "license": strip("LicenseShortName"),
            "author": strip("Artist"),
        }
    return None


def save_jpeg(data, path):
    try:
        from PIL import Image
    except ImportError:
        with open(path, "wb") as f:
            f.write(data)
        return
    img = Image.open(io.BytesIO(data)).convert("RGB")
    img.thumbnail((400, 400))
    img.save(path, "JPEG", quality=88)


def is_politician(intro):
    return any(k in intro for k in ("政治家", "衆議院議員", "参議院議員", "議員"))


def find_article(c):
    names = [c.get("wiki")] if c.get("wiki") else []
    legal = c.get("name_legal") or c["name_ja"]
    names += [legal, f"{legal} (政治家)"]
    for n in names:
        a = article(n)
        if a and is_politician(a["intro"]):
            return a
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--all", action="store_true", help="all SMD candidates, not only notable ones")
    ap.add_argument("--only", default="", help="comma-separated candidate ids")
    ap.add_argument("--force", action="store_true", help="replace existing photos")
    args = ap.parse_args()

    cands = json.load(open(os.path.join(ROOT, "data/election/2026/candidates.json"), encoding="utf-8"))
    results = json.load(open(os.path.join(ROOT, "data/election/2026/results.json"), encoding="utf-8"))
    os.makedirs(os.path.join(ROOT, "assets/photos"), exist_ok=True)
    credits_path = os.path.join(ROOT, "assets/photos/CREDITS.json")
    credits = json.load(open(credits_path, encoding="utf-8")) if os.path.exists(credits_path) else {}
    only = set(filter(None, args.only.split(",")))

    winners = set()
    for race in cands["races"]:
        if race["kind"] != "smd":
            continue
        tot = {}
        for t in results["races"].get(race["id"], {}).get("regions", {}).values():
            for cid, v in t["votes"].items():
                tot[cid] = tot.get(cid, 0) + v
        if tot:
            winners.add(max(tot, key=tot.get))

    stats = {"ok": 0, "none": 0, "nonfree": 0}
    for race in cands["races"]:
        if race["kind"] != "smd":
            continue
        for c in race["candidates"]:
            cid = c["id"]
            if only and cid not in only:
                continue
            notable = cid in winners or c.get("pr_elected") or c.get("status") in ("前", "元")
            if not only and not args.all and not notable:
                continue
            out = os.path.join(ROOT, "assets/photos", cid + ".jpg")
            if os.path.exists(out) and not args.force:
                continue
            label = f"{cid} {c['name_ja']} ({c.get('name_legal', '')})"
            try:
                a = find_article(c)
            except Exception as e:  # network errors etc.
                print(f"{label}: lookup failed: {e}")
                continue
            if not a or not a["image"]:
                print(f"{label}: no politician article / lead image")
                stats["none"] += 1
                continue
            info = commons_info(a["image"])
            if not info or not info["thumb"]:
                print(f"{label}: image {a['image']} not on Commons")
                stats["none"] += 1
                continue
            if not FREE.match(info["license"] or ""):
                print(f"{label}: skipped non-free licence {info['license']!r}")
                stats["nonfree"] += 1
                continue
            print(f"{label}: {a['title']} -> {a['image']} [{info['license']}] {info['author'][:60]}")
            stats["ok"] += 1
            if args.dry_run:
                continue
            save_jpeg(get_bytes(info["thumb"]), out)
            credits[cid] = {
                "name_ja": c["name_ja"],
                "source": info["page"],
                "original_origin": f"Wikimedia Commons lead image of ja.wikipedia article {a['title']!r}",
                "author": info["author"],
                "license_note": info["license"],
            }
            time.sleep(0.2)
    if not args.dry_run:
        with open(credits_path, "w", encoding="utf-8") as f:
            json.dump(dict(sorted(credits.items())), f, ensure_ascii=False, indent=2)
    print(stats)


if __name__ == "__main__":
    sys.exit(main())
