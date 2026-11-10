"""Downloads the terrain textures listed in res/textures/terrain_materials.json.

The textures are CC0 (Poly Haven, ambientCG) and are not kept in git. Every material ends up in
ProceduralTerrainGenerator/res/textures/terrain/<asset>/ as albedo.jpg, normal.jpg (OpenGL convention),
roughness.jpg, ao.jpg and height.jpg; a map the source does not have is left out and the app uses a neutral value.
The app packs the channels itself when it loads them, so this script needs only the Python standard library.

Usage: python tools/fetch_textures.py [--resolution 1k|2k|4k] [--force]
"""

import argparse
import io
import json
import pathlib
import sys
import urllib.request
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent / "ProceduralTerrainGenerator" / "res" / "textures"
MANIFEST = ROOT / "terrain_materials.json"
OUTPUT = ROOT / "terrain"
USER_AGENT = "terrain-texture-fetch/1.0"

# Output name -> map name in the Poly Haven files API
POLYHAVEN_MAPS = {
    "albedo": "Diffuse",
    "normal": "nor_gl",
    "roughness": "Rough",
    "ao": "AO",
    "height": "Displacement",
}
# Output name -> suffix of the file in an ambientCG zip (before .jpg)
AMBIENTCG_MAPS = {
    "albedo": "_Color",
    "normal": "_NormalGL",
    "roughness": "_Roughness",
    "ao": "_AmbientOcclusion",
    "height": "_Displacement",
}


def download(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def fetch_polyhaven(asset, resolution, folder):
    files = json.loads(download(f"https://api.polyhaven.com/files/{asset}"))
    for name, key in POLYHAVEN_MAPS.items():
        entry = files.get(key, {}).get(resolution, {}).get("jpg")
        if entry is None:
            print(f"  {name}: none")
            continue
        (folder / f"{name}.jpg").write_bytes(download(entry["url"]))
        print(f"  {name}: ok")


def fetch_ambientcg(asset, resolution, folder):
    data = download(f"https://ambientcg.com/get?file={asset}_{resolution.upper()}-JPG.zip")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        members = archive.namelist()
        for name, suffix in AMBIENTCG_MAPS.items():
            member = next((m for m in members if m.endswith(suffix + ".jpg")), None)
            if member is None:
                print(f"  {name}: none")
                continue
            (folder / f"{name}.jpg").write_bytes(archive.read(member))
            print(f"  {name}: ok")


FETCHERS = {"polyhaven": fetch_polyhaven, "ambientcg": fetch_ambientcg}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--resolution", choices=["1k", "2k", "4k"], default="2k")
    parser.add_argument("--force", action="store_true", help="download materials that are already there again")
    args = parser.parse_args()

    materials = json.loads(MANIFEST.read_text(encoding="utf-8"))["materials"]
    failed = []
    for material in materials:
        asset, source = material["asset"], material["source"]
        folder = OUTPUT / asset
        if (folder / "albedo.jpg").exists() and not args.force:
            print(f"{material['name']} ({asset}): already there")
            continue
        print(f"{material['name']} ({source}/{asset}):")
        folder.mkdir(parents=True, exist_ok=True)
        try:
            FETCHERS[source](asset, args.resolution, folder)
        except Exception as error:  # keep going, report at the end
            print(f"  failed: {error}")
            failed.append(asset)
    if failed:
        print("Failed: " + ", ".join(failed))
        return 1
    print(f"Done: {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
