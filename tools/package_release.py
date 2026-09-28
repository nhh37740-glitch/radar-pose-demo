"""Validate and stream a deterministic full-sequence Radar Pose binary release."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
FULL = ROOT / "runtime-data"
DIST = ROOT / "dist"
SEQUENCE = "2019-01-15-13-06-37"
EXPECTED_FRAMES = 7203
FIXED_ZIP_TIME = (2026, 1, 1, 0, 0, 0)
TEXT_SUFFIXES = {".css", ".html", ".js", ".json", ".md", ".txt", ".yaml"}
STATIC_FILES = {
    "index.html", "indexbak.html", "index-global.html", "app.js",
    "config.js", "styles.css", "data-license.html", "data/pose-excerpt.js",
}


def payload_chunks(path: Path):
    """Yield bounded pieces; normalize small text files to LF for reproducibility."""
    if path.suffix.lower() in TEXT_SUFFIXES:
        yield path.read_bytes().replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    else:
        with path.open("rb") as stream:
            while chunk := stream.read(1024 * 1024):
                yield chunk


def payload_hash(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    for chunk in payload_chunks(path):
        digest.update(chunk)
        size += len(chunk)
    return size, digest.hexdigest()


def zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_ZIP_TIME)
    info.compress_type = zipfile.ZIP_STORED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def check_image_set(directory: Path, expected: set[str], label: str) -> None:
    if not directory.is_dir():
        raise FileNotFoundError(directory)
    actual = {path.name for path in directory.iterdir() if path.is_file()}
    if actual != expected:
        raise ValueError(f"{label} images mismatch; missing={len(expected - actual)}, extra={len(actual - expected)}")
    for name in expected:
        with (directory / name).open("rb") as stream:
            if stream.read(3) != b"\xff\xd8\xff":
                raise ValueError(f"Invalid JPEG: {label}/{name}")


def validate() -> tuple[list[tuple[str, Path]], dict]:
    manifest_path = FULL / "manifest.json"
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Complete binary data pack missing: {manifest_path}")
    index = json.loads(manifest_path.read_text(encoding="utf-8"))
    metadata = index.get("metadata", {})
    pages = index.get("pages", [])
    if (index.get("formatVersion") != 1 or index.get("pageSize") != 240 or
            metadata.get("sequence") != SEQUENCE or metadata.get("sampleCount") != EXPECTED_FRAMES or
            metadata.get("recordingMode") != "saved-per-frame-estimates" or
            metadata.get("liveInference") is not False or metadata.get("dataLicense") != "CC BY-NC-SA 4.0"):
        raise ValueError("Full data pack metadata does not describe the expected real recording")
    if not isinstance(pages, list) or not pages:
        raise ValueError("Full data pack page index is missing")
    radar_names: set[str] = set()
    stereo_names: set[str] = set()
    chunk_paths: set[str] = set()
    next_frame = 0
    for page_number, page in enumerate(pages):
        start = page.get("startFrame")
        count = page.get("count")
        name = page.get("file")
        if (start != next_frame or start != page_number * 240 or not isinstance(count, int) or
                count < 1 or count > 240 or (page_number < len(pages) - 1 and count != 240) or
                name != f"chunks/page-{page_number:05d}.json"):
            raise ValueError(f"Invalid page index entry {page_number}")
        path = FULL / name
        if not path.is_file():
            raise FileNotFoundError(path)
        data = json.loads(path.read_text(encoding="utf-8"))
        rows = data.get("frames", [])
        if data.get("startFrame") != start or len(rows) != count:
            raise ValueError(f"Invalid page contents: {name}")
        for offset, row in enumerate(rows):
            if not isinstance(row, list) or len(row) != 32 or row[0] != start + offset:
                raise ValueError(f"Invalid pose row at frame {start + offset}")
            radar_names.add(f"{int(row[1])}.jpg")
            stereo_names.add(f"{int(row[31])}.jpg")
        chunk_paths.add(name)
        next_frame += count
    if next_frame != EXPECTED_FRAMES or len(radar_names) != EXPECTED_FRAMES or len(stereo_names) != EXPECTED_FRAMES:
        raise ValueError("Full recording must have 7,203 contiguous rows and unique paired sensor timestamps")
    overview = index.get("overviewRows")
    if (not isinstance(overview, list) or not overview or overview[0][0] != 0 or
            overview[-1][0] != EXPECTED_FRAMES - 1):
        raise ValueError("Complete route overview is missing")
    check_image_set(FULL / "radar", radar_names, "full/radar")
    check_image_set(FULL / "stereo", stereo_names, "full/stereo")

    excerpt_path = WEB / "data" / "pose-excerpt.js"
    script = excerpt_path.read_text(encoding="utf-8").strip()
    prefix = "window.RADAR_POSE_METHOD_DATA="
    if not script.startswith(prefix):
        raise ValueError("Fallback excerpt has an unexpected wrapper")
    excerpt = json.loads(script[len(prefix):].rstrip(";"))
    rows = excerpt.get("frames", [])
    if len(rows) != 240 or any(not isinstance(row, list) or len(row) != 32 or row[0] != i for i, row in enumerate(rows)):
        raise ValueError("Fallback excerpt must contain real frames 0–239")
    check_image_set(WEB / "assets" / "radar", {f"{int(row[1])}.jpg" for row in rows}, "excerpt/radar")
    check_image_set(WEB / "assets" / "stereo", {f"{int(row[31])}.jpg" for row in rows}, "excerpt/stereo")

    html = (WEB / "index.html").read_text(encoding="utf-8").lower()
    license_page = (WEB / "data-license.html").read_text(encoding="utf-8").lower()
    for phrase in (SEQUENCE, "precomputed pose estimates", "no live inference",
                   "data-license.html", "不在浏览器中运行模型推理", "frame-seek"):
        if phrase not in html:
            raise ValueError(f"Public page is missing: {phrase}")
    for phrase in ("7,203 contiguous frames", "0–7202", "cc by-nc-sa 4.0",
                   "non-commercial academic use", "web-ready jpegs", "real-time kinematic",
                   "https://oxford-robotics-institute.github.io/radar-robotcar-dataset/citation",
                   "https://robotcar-dataset.robots.ox.ac.uk/citation/"):
        if phrase not in license_page:
            raise ValueError(f"License page is missing: {phrase}")

    expected_web = STATIC_FILES | {f"assets/radar/{p.name}" for p in (WEB / "assets" / "radar").iterdir()} | {
        f"assets/stereo/{p.name}" for p in (WEB / "assets" / "stereo").iterdir()
    }
    actual_web = {p.relative_to(WEB).as_posix() for p in WEB.rglob("*") if p.is_file()}
    if actual_web != expected_web:
        raise ValueError(f"Unexpected web files: {sorted(actual_web ^ expected_web)[:3]}")
    expected_full = {"manifest.json"} | chunk_paths | {f"radar/{name}" for name in radar_names} | {
        f"stereo/{name}" for name in stereo_names
    }
    actual_full = {p.relative_to(FULL).as_posix() for p in FULL.rglob("*") if p.is_file()}
    if actual_full != expected_full:
        raise ValueError(f"Unexpected full data files: {sorted(actual_full ^ expected_full)[:3]}")
    files = [(name, WEB / name) for name in expected_web] + [(f"full/{name}", FULL / name) for name in expected_full]
    return sorted(files), metadata


def main() -> None:
    files, metadata = validate()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("VERSION must be a numeric semantic version")
    name = f"radar-pose-demo-{version}"
    release_manifest = {
        "name": "radar-pose-demo", "version": version,
        "status": "complete-recorded-sequence-replay", "live_inference": False,
        "sequence": SEQUENCE, "frame_range_inclusive": [0, EXPECTED_FRAMES - 1],
        "sample_count": EXPECTED_FRAMES,
        "recorded_duration_seconds": metadata["recordedDurationSeconds"],
        "changes": "Paired Oxford sensor JPEGs with saved pose rows; split records into pages for bounded browser memory.",
        "data_attribution": "Oxford Radar RobotCar Dataset and Oxford RobotCar Dataset",
        "data_license": {"spdx_id": "CC-BY-NC-SA-4.0", "url": "https://creativecommons.org/licenses/by-nc-sa/4.0/",
                         "scope": "Included Oxford sensor images and reference/derived pose records; unrelated project code is excluded."},
        "dataset_citations": [
            {"doi": "10.1109/ICRA40945.2020.9196884", "title": "The Oxford Radar RobotCar Dataset"},
            {"doi": "10.1177/0278364916679498", "title": "1 Year, 1000km: The Oxford RobotCar Dataset"},
            {"url": "https://arxiv.org/abs/2002.10152", "title": "Real-time Kinematic Ground Truth for the Oxford RobotCar Dataset"},
        ],
        "files": [{"path": archive_name, "bytes": size, "sha256": digest}
                  for archive_name, path in files for size, digest in [payload_hash(path)]],
    }
    manifest_bytes = (json.dumps(release_manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    DIST.mkdir(exist_ok=True)
    archive = DIST / f"{name}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as bundle:
        for archive_name, path in files:
            with bundle.open(zip_info(archive_name), "w", force_zip64=True) as destination:
                for chunk in payload_chunks(path):
                    destination.write(chunk)
        bundle.writestr(zip_info("release-manifest.json"), manifest_bytes)
    digest = payload_hash(archive)[1]
    (DIST / f"{name}.manifest.json").write_bytes(manifest_bytes)
    (DIST / f"{name}.zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8", newline="\n")
    print(f"Validated {EXPECTED_FRAMES} real frames and paired images; release={archive.stat().st_size} bytes; sha256={digest}")


if __name__ == "__main__":
    main()
