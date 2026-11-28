"""Downloads the vegetation and rock models listed in res/vegetation/vegetation_models.json.

The models are CC0 glTF files from Poly Haven and are not kept in git. Every model ends up in
ProceduralTerrainGenerator/res/vegetation/models/<asset>/ as <asset>.gltf with its .bin and textures/ next to it,
exactly as Poly Haven lays them out, plus the alpha maps of leaves (the glTF textures are JPEG, so they carry no alpha).
The app simplifies the meshes when it loads them and keeps the result in res/vegetation/cache/.

Usage: python tools/fetch_models.py [--resolution 1k|2k|4k] [--force]
"""

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import fetch_common  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parent.parent / "ProceduralTerrainGenerator" / "res" / "vegetation"
MANIFEST = ROOT / "vegetation_models.json"
OUTPUT = ROOT / "models"
USER_AGENT = "terrain-model-fetch/1.0"


def download(url, expected_md5=None):
    return fetch_common.download_bytes(url, USER_AGENT, timeout=300, expected_md5=expected_md5)


def fetch_model(asset, resolution, folder):
    asset = fetch_common.validate_asset_name(asset)
    folder = pathlib.Path(folder)
    files = json.loads(download(f"https://api.polyhaven.com/files/{asset}"))
    gltf = files.get("gltf", {}).get(resolution, {}).get("gltf")
    if gltf is None:
        raise RuntimeError(f"no glTF at {resolution}")
    total = 0
    for relative, entry in gltf.get("include", {}).items():
        target = fetch_common.safe_target_path(folder, relative)
        data = download(entry["url"], expected_md5=entry.get("md5"))
        fetch_common.write_atomic(target, data)
        total += len(data)
    # Alpha maps are separate files (keys like "Alpha" or "leaves_alpha"); the app finds them next to the albedo
    # by replacing "_diff_" with "_alpha_" in its name, which is how Poly Haven names them
    for key, maps in files.items():
        if "alpha" not in key.lower() or not isinstance(maps, dict):
            continue
        entry = maps.get(resolution, {}).get("jpg")
        if entry is None:
            continue
        name = entry["url"].rsplit("/", 1)[-1]
        target = fetch_common.safe_target_path(folder, f"textures/{name}")
        data = download(entry["url"], expected_md5=entry.get("md5"))
        fetch_common.write_atomic(target, data)
        total += len(data)
    # Written last, followed by .complete, so interrupted downloads are retried next time
    gltf_data = download(gltf["url"], expected_md5=gltf.get("md5"))
    fetch_common.write_atomic(fetch_common.safe_target_path(folder, f"{asset}.gltf"), gltf_data)
    total += len(gltf_data)
    fetch_common.mark_complete(folder, resolution)
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--resolution", choices=["1k", "2k", "4k"], default="1k")
    parser.add_argument("--force", action="store_true", help="download models that are already there again")
    args = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    failed = []
    for model in manifest["models"]:
        asset = fetch_common.validate_asset_name(model["asset"])
        folder = fetch_common.safe_target_path(OUTPUT, asset)
        if fetch_common.is_complete(folder, f"{asset}.gltf") and not args.force:
            print(f"{model['name']} ({asset}): already there")
            continue
        print(f"{model['name']} ({asset}):", end=" ", flush=True)
        try:
            folder.mkdir(parents=True, exist_ok=True)
            if args.force:
                fetch_common.clear_complete(folder)
            size = fetch_model(asset, args.resolution, folder)
            print(f"{size / 1e6:.1f} MB")
        except Exception as error:  # keep going with the other models
            print(f"failed: {error}")
            failed.append(asset)
    if failed:
        print("Failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
