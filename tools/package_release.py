"""Validate and package the real-data Radar Pose module as a versioned binary release."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import zipfile


ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
DIST = ROOT / "dist"
POSE_DATA = WEB / "data" / "pose-excerpt.js"
STATIC_FILES = {
    "index.html",
    "indexbak.html",
    "index-global.html",
    "app.js",
    "config.js",
    "styles.css",
    "data-license.html",
    "data/pose-excerpt.js",
}
FIXED_ZIP_TIME = (2026, 1, 1, 0, 0, 0)
ATTRIBUTION = "Oxford Radar RobotCar Dataset and Oxford RobotCar Dataset"
LICENSE_URL = "https://creativecommons.org/licenses/by-nc-sa/4.0/"
RELEASE_START_FRAME = 0
RELEASE_FRAME_COUNT = 240


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_pose_data() -> dict:
    script = POSE_DATA.read_text(encoding="utf-8").strip()
    prefix = "window.RADAR_POSE_METHOD_DATA="
    if not script.startswith(prefix):
        raise ValueError("Pose excerpt has an unexpected JavaScript wrapper")
    return json.loads(script[len(prefix) :].rstrip(";"))


def validate() -> tuple[list[Path], dict, dict]:
    data = read_pose_data()
    metadata = data.get("metadata", {})
    frames = data.get("frames", [])
    if metadata.get("sequence") != "2019-01-15-13-06-37":
        raise ValueError("Unexpected recorded sequence")
    if metadata.get("recordingMode") != "saved-per-frame-estimates" or metadata.get("liveInference") is not False:
        raise ValueError("The release must explicitly be a saved-data replay with live inference disabled")
    if metadata.get("dataLicense") != "CC BY-NC-SA 4.0":
        raise ValueError("Dataset license declaration is missing")
    if len(frames) <= 0 or len(frames) != int(metadata.get("sampleCount", -1)):
        raise ValueError("Excerpt sample count does not match its metadata")
    start = int(metadata.get("excerptStartFrame", -1))
    end = int(metadata.get("excerptEndFrame", -1))
    if len(frames) != end - start + 1:
        raise ValueError("Excerpt must contain one contiguous range of sequence frames")
    if start != RELEASE_START_FRAME or len(frames) != RELEASE_FRAME_COUNT or end != RELEASE_FRAME_COUNT - 1:
        raise ValueError("This release must contain exactly source frames 0–239")

    radar_names: set[str] = set()
    stereo_names: set[str] = set()
    for offset, row in enumerate(frames):
        if not isinstance(row, list) or len(row) != 32 or int(row[0]) != start + offset:
            raise ValueError(f"Invalid or noncontiguous row at excerpt offset {offset}")
        radar_names.add(f"{int(row[1])}.jpg")
        stereo_names.add(f"{int(row[31])}.jpg")
    if len(radar_names) != len(frames) or len(stereo_names) != len(frames):
        raise ValueError("Every frame must have a unique paired radar and stereo image")

    radar_dir = WEB / "assets" / "radar"
    stereo_dir = WEB / "assets" / "stereo"
    if not radar_dir.is_dir() or not stereo_dir.is_dir():
        raise ValueError("Radar and stereo image directories are required")
    if {p.name for p in radar_dir.iterdir() if p.is_file()} != radar_names:
        raise ValueError("Radar images must exactly match excerpt frame timestamps")
    if {p.name for p in stereo_dir.iterdir() if p.is_file()} != stereo_names:
        raise ValueError("Stereo images must exactly match excerpt frame timestamps")
    for image in list(radar_dir.iterdir()) + list(stereo_dir.iterdir()):
        if image.read_bytes()[:3] != b"\xff\xd8\xff":
            raise ValueError(f"Sensor image is not a JPEG: {image.relative_to(WEB)}")

    required = [WEB / name for name in sorted(STATIC_FILES)]
    for path in required:
        if not path.is_file():
            raise FileNotFoundError(path)
    html = (WEB / "index.html").read_text(encoding="utf-8").lower()
    for phrase in (
        "2019-01-15-13-06-37",
        "precomputed pose estimates",
        "no live inference",
        "cc by-nc-sa 4.0",
        "data-license.html",
        "不在浏览器中运行模型推理",
        f"recorded frames {start}–{end}".lower(),
    ):
        if phrase not in html:
            raise ValueError(f"Public page is missing required disclosure: {phrase}")
    expected_paths = STATIC_FILES | {f"assets/radar/{name}" for name in radar_names} | {
        f"assets/stereo/{name}" for name in stereo_names
    }
    actual_paths = {
        path.relative_to(WEB).as_posix() for path in WEB.rglob("*") if path.is_file()
    }
    if actual_paths != expected_paths:
        unexpected = sorted(actual_paths - expected_paths)
        missing = sorted(expected_paths - actual_paths)
        raise ValueError(f"Public file allowlist mismatch; unexpected={unexpected[:1]}, missing={missing[:1]}")
    license_page = (WEB / "data-license.html").read_text(encoding="utf-8").lower()
    for phrase in (
        "oxford radar robotcar dataset",
        "oxford robotcar dataset",
        "cc by-nc-sa 4.0",
        "web-ready jpegs",
        "non-commercial academic use",
        "real-time kinematic",
        f"{metadata['sampleCount']}-frame excerpt",
        f"frames {start}–{end}",
        "不在浏览器中运行模型推理",
        "https://oxford-robotics-institute.github.io/radar-robotcar-dataset/",
        "https://oxford-robotics-institute.github.io/radar-robotcar-dataset/citation",
        "https://robotcar-dataset.robots.ox.ac.uk/citation/",
        "https://creativecommons.org/licenses/by-nc-sa/4.0/",
        "arxiv:2002.10152",
    ):
        if phrase not in license_page:
            raise ValueError(f"Data-license page is missing required detail: {phrase}")

    release_files = [WEB / name for name in sorted(expected_paths)]
    return release_files, data, metadata


def main() -> None:
    files, data, metadata = validate()
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not version or any(ch not in "0123456789." for ch in version):
        raise ValueError("VERSION must contain a numeric semantic version")
    name = f"radar-pose-demo-{version}"
    manifest = {
        "name": "radar-pose-demo",
        "version": version,
        "status": "recorded-sequence-replay",
        "live_inference": False,
        "sequence": metadata["sequence"],
        "frame_range_inclusive": [metadata["excerptStartFrame"], metadata["excerptEndFrame"]],
        "source_sample_count": metadata["sourceSampleCount"],
        "excerpt_sample_count": metadata["sampleCount"],
        "recorded_duration_seconds": metadata["recordedDurationSeconds"],
        "playback_interval_ms": metadata["playbackIntervalMs"],
        "replay_speed": metadata["replaySpeed"],
        "changes": "Selected a contiguous frame interval, paired the corresponding images, packaged web-ready JPEGs, and retained the matching saved pose rows.",
        "data_attribution": ATTRIBUTION,
        "data_license": {
            "name": "Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International",
            "spdx_id": "CC-BY-NC-SA-4.0",
            "url": LICENSE_URL,
            "scope": "Included Oxford sensor images and excerpted released reference/derived pose records; this license does not cover unrelated project code.",
            "official_sources": [
                "https://oxford-robotics-institute.github.io/radar-robotcar-dataset/",
                "https://robotcar-dataset.robots.ox.ac.uk/privacy/",
            ],
        },
        "dataset_citations": [
            {
                "citation": "D. Barnes, M. Gadd, P. Murcutt, P. Newman, and I. Posner. The Oxford Radar RobotCar Dataset: A Radar Extension to the Oxford RobotCar Dataset. ICRA, 2020.",
                "doi": "10.1109/ICRA40945.2020.9196884",
                "url": "https://doi.org/10.1109/ICRA40945.2020.9196884",
            },
            {
                "citation": "W. Maddern, G. Pascoe, C. Linegar, and P. Newman. 1 Year, 1000km: The Oxford RobotCar Dataset. IJRR, 2017.",
                "doi": "10.1177/0278364916679498",
                "url": "https://doi.org/10.1177/0278364916679498",
            },
            {
                "citation": "W. Maddern, G. Pascoe, M. Gadd, D. Barnes, B. Yeomans, and P. Newman. Real-time Kinematic Ground Truth for the Oxford RobotCar Dataset. arXiv:2002.10152, 2020.",
                "url": "https://arxiv.org/abs/2002.10152",
            },
        ],
        "files": [
            {
                "path": path.relative_to(WEB).as_posix(),
                "bytes": path.stat().st_size,
                "sha256": sha256(path.read_bytes()),
            }
            for path in sorted(files, key=lambda item: item.relative_to(WEB).as_posix())
        ],
    }
    manifest_bytes = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    DIST.mkdir(exist_ok=True)
    for old in DIST.glob("radar-pose-demo-*"):
        if old.resolve().parent != DIST.resolve() or not old.is_file():
            raise RuntimeError(f"Unexpected stale release path: {old}")
        old.unlink()
    archive = DIST / f"{name}.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(files, key=lambda item: item.relative_to(WEB).as_posix()):
            item = zipfile.ZipInfo(path.relative_to(WEB).as_posix(), FIXED_ZIP_TIME)
            item.compress_type = zipfile.ZIP_DEFLATED
            bundle.writestr(item, path.read_bytes())
        item = zipfile.ZipInfo("release-manifest.json", FIXED_ZIP_TIME)
        item.compress_type = zipfile.ZIP_DEFLATED
        bundle.writestr(item, manifest_bytes)
    digest = sha256(archive.read_bytes())
    (DIST / f"{name}.manifest.json").write_bytes(manifest_bytes)
    (DIST / f"{name}.zip.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    with zipfile.ZipFile(archive) as bundle:
        expected = {path.relative_to(WEB).as_posix() for path in files} | {"release-manifest.json"}
        if set(bundle.namelist()) != expected:
            raise RuntimeError("Release archive contains unexpected or missing files")
        bundled_manifest = json.loads(bundle.read("release-manifest.json"))
        if bundled_manifest != manifest:
            raise RuntimeError("Embedded release manifest does not match the generated manifest")
    print(
        f"Validated {len(data['frames'])} contiguous real frames "
        f"({len(list((WEB / 'assets/radar').iterdir()))} radar + "
        f"{len(list((WEB / 'assets/stereo').iterdir()))} stereo images); "
        f"release={archive.stat().st_size} bytes; sha256={digest}"
    )


if __name__ == "__main__":
    main()
