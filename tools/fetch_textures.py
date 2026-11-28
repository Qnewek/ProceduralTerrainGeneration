"""Downloads the terrain textures listed in res/textures/terrain_materials.json.

The textures are CC0 (Poly Haven, ambientCG) and are not kept in git. Every material ends up in
ProceduralTerrainGenerator/res/textures/terrain/<asset>/ as albedo.jpg, normal.jpg (OpenGL convention),
roughness.jpg, ao.jpg and height.jpg; a map the source does not have is left out and the app uses a neutral value.
The app packs the channels itself when it loads them, so this script needs only the Python standard library.

Usage: python tools/fetch_textures.py [--resolution 1k|2k|4k] [--force]
"""

import argparse
import json
import pathlib
import sys
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fetch_common  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent / "ProceduralTerrainGenerator" / "res" / "textures"
MANIFEST = ROOT / "terrain_materials.json"
OUTPUT = ROOT / "terrain"
USER_AGENT = "terrain-texture-fetch/1.0"
MAX_ZIP_MEMBER_BYTES = 256 * 1024 * 1024

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


def download(url, expected_md5=None):
    return fetch_common.download_bytes(url, USER_AGENT, timeout=120, expected_md5=expected_md5)


def fetch_polyhaven(asset, resolution, folder):
    asset = fetch_common.validate_asset_name(asset)
    folder = pathlib.Path(folder)
    files = json.loads(download(f"https://api.polyhaven.com/files/{asset}"))
    for name, key in POLYHAVEN_MAPS.items():
        entry = files.get(key, {}).get(resolution, {}).get("jpg")
        if entry is None:
            print(f"  {name}: none")
            continue
        target = fetch_common.safe_target_path(folder, f"{name}.jpg")
        fetch_common.write_atomic(target, download(entry["url"], expected_md5=entry.get("md5")))
        print(f"  {name}: ok")
    fetch_common.mark_complete(folder, resolution)


def fetch_ambientcg(asset, resolution, folder):
    asset = fetch_common.validate_asset_name(asset)
    folder = pathlib.Path(folder)
    url = f"https://ambientcg.com/get?file={asset}_{resolution.upper()}-JPG.zip"
    with fetch_common.download_to_spooled_file(url, USER_AGENT, timeout=120) as spool:
        with zipfile.ZipFile(spool) as archive:
            infos = archive.infolist()
            for name, suffix in AMBIENTCG_MAPS.items():
                info = next((m for m in infos if m.filename.endswith(suffix + ".jpg")), None)
                if info is None:
                    print(f"  {name}: none")
                    continue
                if info.file_size > MAX_ZIP_MEMBER_BYTES:
                    raise RuntimeError(f"ZIP entry {info.filename} exceeds size limit ({info.file_size} bytes)")
                target = fetch_common.safe_target_path(folder, f"{name}.jpg")
                fetch_common.write_atomic(target, archive.read(info))
                print(f"  {name}: ok")
    fetch_common.mark_complete(folder, resolution)


FETCHERS = {"polyhaven": fetch_polyhaven, "ambientcg": fetch_ambientcg}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--resolution", choices=["1k", "2k", "4k"], default="2k")
    parser.add_argument("--force", action="store_true", help="download materials that are already there again")
    args = parser.parse_args()

    materials = json.loads(MANIFEST.read_text(encoding="utf-8"))["materials"]
    failed = []
    for material in materials:
        asset = fetch_common.validate_asset_name(material["asset"])
        source = material["source"]
        if source not in FETCHERS:
            print(f"{material['name']} ({source}/{asset}): unknown source")
            failed.append(asset)
            continue
        folder = fetch_common.safe_target_path(OUTPUT, asset)
        if fetch_common.is_complete(folder, "albedo.jpg") and not args.force:
            print(f"{material['name']} ({asset}): already there")
            continue
        print(f"{material['name']} ({source}/{asset}):")
        folder.mkdir(parents=True, exist_ok=True)
        try:
            if args.force:
                fetch_common.clear_complete(folder)
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
