"""Downloads the raw source data used to build jpy_election's JSON/TopoJSON.

Everything lands in data/raw/ (git-ignored). Re-run safely: existing files
are kept unless --force is given.

Sources
  * MIC (総務省) official results of the 51st House of Representatives general
    election and the 27th Supreme Court national review, 2026-02-08:
    https://www.soumu.go.jp/senkyo/senkyo_s/data/shugiin51/index.html
  * 2022 single-member district polygons (289 districts) by 西澤明
    (地域・交通データ研究所 / CSIS, Univ. of Tokyo): https://gtfs-gis.jp/senkyoku/
  * Municipality polygons: smartnews-smri/japan-topography (derived from
    国土数値情報 N03, MLIT).
"""
import argparse
import os
import re
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW = os.path.join(ROOT, "data", "raw")
UA = {"User-Agent": "Mozilla/5.0 (jpy_election data builder)"}
MIC = "https://www.soumu.go.jp"


def get(url):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r:
        return r.read()


def save(url, name, force=False):
    path = os.path.join(RAW, name)
    if os.path.exists(path) and not force:
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = get(url)
    with open(path, "wb") as f:
        f.write(data)
    print(f"  {name}  ({len(data):,} bytes)")
    return path


def links(html_bytes):
    t = html_bytes.decode("cp932", "replace")
    out = []
    for href, label in re.findall(r'<a[^>]+href="([^"]+?\.(?:xlsx?|pdf|csv|zip))"[^>]*>(.*?)</a>', t, flags=re.S):
        out.append((href, re.sub(r"<[^>]+>", "", label).strip()))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()
    os.makedirs(RAW, exist_ok=True)

    print("MIC summary tables (shugiin51)")
    idx = get(MIC + "/senkyo/senkyo_s/data/shugiin51/index.html")
    with open(os.path.join(RAW, "mic_index.html"), "wb") as f:
        f.write(idx)
    for href, label in links(idx):
        name = "mic/" + os.path.basename(href)
        save(MIC + href, name, args.force)

    print("MIC municipality-level tables")
    shiku = get(MIC + "/senkyo/senkyo_s/data/shugiin51/shikuchouson.html")
    with open(os.path.join(RAW, "mic_shikuchouson.html"), "wb") as f:
        f.write(shiku)
    manifest = []
    for href, label in links(shiku):
        kind = "pr" if label.endswith("選挙区") else "smd"
        name = f"mic_shiku/{kind}_{os.path.basename(href)}"
        save(MIC + href, name, args.force)
        manifest.append(f"{kind}\t{label}\t{name}")
    with open(os.path.join(RAW, "mic_shiku_manifest.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(manifest) + "\n")

    print("District polygons (senkyoku2022)")
    save("https://gtfs-gis.jp/senkyoku2022/senkyoku2022.zip", "senkyoku2022.zip", args.force)
    save("https://gtfs-gis.jp/senkyoku2022/senkyoku_ichiran.xlsx", "senkyoku_ichiran.xlsx", args.force)
    zpath = os.path.join(RAW, "senkyoku2022")
    if not os.path.exists(os.path.join(zpath, "senkyoku2022.shp")):
        import zipfile

        with zipfile.ZipFile(os.path.join(RAW, "senkyoku2022.zip")) as z:
            for n in z.namelist():
                base = os.path.basename(n)
                if base:
                    os.makedirs(zpath, exist_ok=True)
                    with open(os.path.join(zpath, base), "wb") as f:
                        f.write(z.read(n))

    print("Municipality polygons (smartnews-smri/japan-topography, from MLIT N03)")
    gh = "https://raw.githubusercontent.com/smartnews-smri/japan-topography/main/data/municipality/topojson"
    save(f"{gh}/s0010/N03-21_210101.json", "N03-21_210101.topo.json", args.force)

    print("MIC local government codes")
    save(MIC + "/main_content/000925835.xlsx", "jis_codes.xlsx", args.force)

    print("MIC municipality-level tables, 50th general election (2024) -> incumbents")
    shiku50 = get(MIC + "/senkyo/senkyo_s/data/shugiin50/shikuchouson.html")
    with open(os.path.join(RAW, "mic50_shiku.html"), "wb") as f:
        f.write(shiku50)
    manifest = []
    for href, label in links(shiku50):
        if not label or label.endswith("選挙区"):
            continue  # PR blocs are not needed
        name = f"mic50_shiku/smd_{os.path.basename(href)}"
        save(MIC + href, name, args.force)
        manifest.append(f"smd\t{label}\t{name}")
    with open(os.path.join(RAW, "mic50_shiku_manifest.tsv"), "w", encoding="utf-8") as f:
        f.write("\n".join(manifest) + "\n")
    print("done")


if __name__ == "__main__":
    sys.exit(main())
