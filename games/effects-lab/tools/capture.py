"""Run a repeatable native capture; preserve source, binary and timing evidence."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time


def command(args, cwd):
    return subprocess.check_output(args, cwd=cwd, text=True, stderr=subprocess.STDOUT).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def identity(root, paths):
    names = command(["git", "ls-files", "--cached", "--others", "--exclude-standard", "--", *paths], root)
    files = {name: sha(root / name) for name in names.splitlines() if (root / name).is_file()}
    return {"head": command(["git", "rev-parse", "HEAD"], root),
            "files_sha256": files,
            "tree_sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--scenario", type=int, default=0, choices=range(6))
    parser.add_argument("--view", type=int, default=0, choices=range(4))
    parser.add_argument("--light", type=int, default=0, choices=range(4))
    parser.add_argument("--quality", type=int, default=48)
    parser.add_argument("--frames", type=int, default=360)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    engine = (args.engine or root.parent / "TheOne").resolve()
    exe, output = args.exe.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    paths = ["games/effects-lab", "CMakeLists.txt", "engine-revision.txt"]
    engine_paths = ["src", "include", "shaders", "cmake", "CMakeLists.txt"]
    before = {"engine": identity(engine, engine_paths), "games": identity(root, paths)}
    binaries = {exe.name: sha(exe)}
    binaries.update({p.name: sha(p) for p in sorted((exe.parent / "shaders").glob("*.spv"))})
    # Depending on the consumer, shader copies can be directly beside the executable.
    binaries.update({p.name: sha(p) for p in sorted(exe.parent.glob("*.spv"))})
    argv = [str(exe), "--smoke-test", "--validate", "--clean", "--scenario", str(args.scenario),
            "--view", str(args.view), "--light", str(args.light), "--quality", str(args.quality), "--frames", str(args.frames),
            "--output", str(output)]
    env = os.environ.copy()
    candidates = [Path(env["VULKAN_SDK"]) / "Bin"] if env.get("VULKAN_SDK") else []
    for parent in exe.parents:
        cache = parent / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text(encoding="utf-8").splitlines():
                if line.startswith("Vulkan_INCLUDE_DIR:PATH="):
                    candidates.append(Path(line.split("=", 1)[1]).parent / "Bin")
    for candidate in candidates:
        if (candidate / "VkLayer_khronos_validation.json").exists():
            env["VK_LAYER_PATH"] = str(candidate)
            break
    env["VK_LAYER_ENABLES"] = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT"
    try:
        gpu = command(["nvidia-smi", "--query-gpu=name,driver_version,memory.total", "--format=csv,noheader"], root)
    except (OSError, subprocess.CalledProcessError):
        gpu = "nvidia-smi unavailable; inspect native log"
    started = time.time()
    with (output / "run.log").open("w", encoding="utf-8") as log:
        run = subprocess.run(argv, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT)
    after = {"engine": identity(engine, engine_paths), "games": identity(root, paths)}
    metrics = {}
    if (output / "frames.csv").exists():
        with (output / "frames.csv").open() as trace:
            rows = list(csv.DictReader(trace))[30:]
        for key in ("sim_ms", "light_ms", "ray_ms", "debt", "bytes", "velocity_clamps", "divergence"):
            values = sorted(float(row[key]) for row in rows)
            if values:
                metrics[key] = {"mean": statistics.mean(values), "p95": values[int((len(values)-1)*.95)],
                                "maximum": max(values)}
    metadata = {"argv": argv, "platform": platform.platform(), "gpu": gpu,
                "start_unix": started, "wall_seconds": time.time()-started,
                "returncode": run.returncode, "source_before": before, "source_after": after,
                "source_unchanged": before == after, "binary_sha256": binaries,
                "warm_frames_30_onward": metrics,
                "note": "GPU stage timings at 1280x720; no universal frame-rate or visual-fidelity claim."}
    (output / "manifest.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    print(json.dumps({"output": str(output), "exit": run.returncode,
                      "source_unchanged": before == after, "timings": metrics}, indent=2))
    if run.returncode or before != after:
        raise SystemExit(run.returncode or 2)


if __name__ == "__main__":
    main()
