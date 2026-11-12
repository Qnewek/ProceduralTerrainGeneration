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
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent / "ProceduralTerrainGenerator" / "res" / "vegetation"
MANIFEST = ROOT / "vegetation_models.json"
OUTPUT = ROOT / "models"
USER_AGENT = "terrain-model-fetch/1.0"


def download(url):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=300) as response:
        return response.read()


def fetch_model(asset, resolution, folder):
    files = json.loads(download(f"https://api.polyhaven.com/files/{asset}"))
    gltf = files.get("gltf", {}).get(resolution, {}).get("gltf")
    if gltf is None:
        raise RuntimeError(f"no glTF at {resolution}")
    total = 0
    for relative, entry in gltf.get("include", {}).items():
        target = folder / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        data = download(entry["url"])
        target.write_bytes(data)
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
        data = download(entry["url"])
        (folder / "textures").mkdir(parents=True, exist_ok=True)
        (folder / "textures" / name).write_bytes(data)
        total += len(data)
    # Written last, so a model whose download broke off has no .gltf and is fetched again next time
    (folder / f"{asset}.gltf").write_bytes(download(gltf["url"]))
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--resolution", choices=["1k", "2k", "4k"], default="1k")
    parser.add_argument("--force", action="store_true", help="download models that are already there again")
    args = parser.parse_args()

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    failed = []
    for model in manifest["models"]:
        asset = model["asset"]
        folder = OUTPUT / asset
        if (folder / f"{asset}.gltf").exists() and not args.force:
            print(f"{model['name']} ({asset}): already there")
            continue
        print(f"{model['name']} ({asset}):", end=" ", flush=True)
        try:
            folder.mkdir(parents=True, exist_ok=True)
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
