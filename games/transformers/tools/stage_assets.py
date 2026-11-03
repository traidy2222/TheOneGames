"""Stage Optimus and shared English dependencies from read-only MSI/cab inventories.

No installer is executed. All extracted and generated content stays in build/.
Run after extracting the disc cabinets and recording the MSI tables.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

BUILD = Path(__file__).resolve().parents[1] / "build"
SEVEN = Path("C:/Program Files/7-Zip/7z.exe")


def stage():
    read = lambda name: json.loads((BUILD / "inventory" / name).read_text(encoding="utf-8-sig"))
    directories = {r[0]: r[1:] for r in read("msi-Directory.json")}
    components = {r[0]: r[2] for r in read("msi-Component.json")}
    def directory(key):
        if key == "INSTALLDIR" or key not in directories:
            return ""
        parent, name = directories[key]
        return directory(parent) + "/" + name.split(":")[0].split("|")[-1]
    rows = [{**row, "path": (directory(components[row["component"]]) + "/" + row["name"].split("|")[-1]).lstrip("/")}
            for row in read("msi-files.json")]
    (BUILD / "inventory/resolved-files.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    wanted = []
    for row in rows:
        name = row["path"].lower()
        if (name.startswith("characters/optimus") or name.startswith("characters/eng/optimus")
                or name.startswith("audio/eng/")
                or name.startswith("levels/us_city_00/eng/")
                or name == "levels/us_west_town/mission.str"):
            wanted.append(row)
    members = BUILD / "cab-members"
    members.mkdir(exist_ok=True)
    for cab in ("Oth.cab", "ENG.cab", "audio.cab", "levels.cab"):
        source = BUILD / "source-disc" / cab
        listing = subprocess.check_output([str(SEVEN), "l", "-slt", str(source)], text=True)
        ids = {line[7:].strip() for line in listing.splitlines() if line.startswith("Path = ")}
        selected = [row["id"] for row in wanted if row["id"] in ids and not (members / row["id"]).exists()]
        if selected:
            subprocess.run([str(SEVEN), "x", str(source), *selected, f"-o{members}", "-aos", "-bso0", "-bsp0"], check=True)
    manifest = []
    for row in wanted:
        source = members / row["id"]
        if not source.exists():
            raise RuntimeError(f"Missing cabinet member: {row}")
        if source.stat().st_size != row["size"]:
            raise RuntimeError(f"Wrong extracted size: {row['path']}")
        destination = BUILD / "source-assets" / row["path"]
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        with source.open("rb") as stream:
            manifest.append({**row, "sha256": hashlib.file_digest(stream, "sha256").hexdigest()})
    for source in (BUILD / "source-disc/Setup/Data").glob("*"):
        if source.suffix in (".str", ".lsa", ".lxb"):
            shutil.copyfile(source, BUILD / "source-assets" / source.name)
    (BUILD / "inventory/staged-assets.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Staged and hashed {len(manifest)} Optimus/shared assets")


if __name__ == "__main__":
    stage()
