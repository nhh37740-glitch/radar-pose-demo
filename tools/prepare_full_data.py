"""Build the complete, paged Oxford recording as a separate binary data pack.

The source directory is read only. Output lives in runtime-data/ beside this
repository and is deliberately excluded from Git; deliver it to the server as
binary data before running the Jenkins release job.
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA_FILE = "method_comparison_jan15_cfear_lite_pose_data.js"
PREFIX = "window.RADAR_POSE_METHOD_DATA="
SEQUENCE = "2019-01-15-13-06-37"
PAGE_SIZE = 240
OVERVIEW_LIMIT = 1200


def load_source(path: Path) -> dict:
    script = path.read_text(encoding="utf-8").strip()
    if not script.startswith(PREFIX):
        raise ValueError("Unexpected pose data wrapper")
    return json.loads(script[len(PREFIX):].rstrip(";"))


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "runtime-data")
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if output == source or source in output.parents or output in source.parents:
        raise ValueError("Output must be separate from the source directory")
    data = load_source(source / "data" / DATA_FILE)
    rows = data.get("frames")
    metadata = data.get("metadata", {})
    if metadata.get("sequence") != SEQUENCE or not isinstance(rows, list) or len(rows) != metadata.get("sampleCount"):
        raise ValueError("Unexpected sequence or pose row count")
    if not rows or any(not isinstance(row, list) or len(row) != 32 or row[0] != index for index, row in enumerate(rows)):
        raise ValueError("Pose rows must be contiguous 32-field records starting at frame zero")
    for field, label in ((1, "radar"), (31, "stereo")):
        stamps = [int(row[field]) for row in rows]
        if len(set(stamps)) != len(rows):
            raise ValueError(f"Duplicate {label} image timestamp")
        directory = source / "assets" / label
        for stamp in stamps:
            image = directory / f"{stamp}.jpg"
            if not image.is_file():
                raise FileNotFoundError(image)
            with image.open("rb") as stream:
                if stream.read(3) != b"\xff\xd8\xff":
                    raise ValueError(f"Invalid JPEG: {image}")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Output directory must be empty: {output}")
    output.mkdir(parents=True, exist_ok=True)

    page_entries = []
    for page_number, start in enumerate(range(0, len(rows), PAGE_SIZE)):
        page = rows[start:start + PAGE_SIZE]
        name = f"chunks/page-{page_number:05d}.json"
        write_json(output / name, {"startFrame": start, "frames": page})
        page_entries.append({"startFrame": start, "count": len(page), "file": name})

    for field, label in ((1, "radar"), (31, "stereo")):
        destination = output / label
        destination.mkdir()
        for row in rows:
            name = f"{int(row[field])}.jpg"
            shutil.copyfile(source / "assets" / label / name, destination / name)

    north = [row[2] for row in rows]
    east = [row[3] for row in rows]
    center_north = (min(north) + max(north)) / 2
    center_east = (min(east) + max(east)) / 2
    span = max(max(north) - min(north), max(east) - min(east), 10) * 1.12
    overview_stride = max(1, math.ceil(len(rows) / OVERVIEW_LIMIT))
    overview = rows[::overview_stride]
    if overview[-1] is not rows[-1]:
        overview.append(rows[-1])
    first_stamp = int(rows[0][1])
    last_stamp = int(rows[-1][1])
    duration = (last_stamp - first_stamp) / 1_000_000
    interval = 100
    public_metadata = {key: value for key, value in metadata.items() if key not in (
        "metrics", "stereoSource", "radarPrimarySource", "voPrimarySource"
    )}
    public_metadata.update({
        "sampleCount": len(rows), "sourceSampleCount": len(rows),
        "firstRadarTimestamp": first_stamp, "lastRadarTimestamp": last_stamp,
        "recordedDurationSeconds": round(duration, 6),
        "playbackIntervalMs": interval,
        "replaySpeed": round(duration / ((len(rows) - 1) * interval / 1000), 3),
        "recordingMode": "saved-per-frame-estimates", "liveInference": False,
        "dataLicense": "CC BY-NC-SA 4.0",
        "sourceAttribution": "Oxford Radar RobotCar and Oxford RobotCar datasets",
    })
    write_json(output / "manifest.json", {
        "formatVersion": 1, "metadata": public_metadata,
        "pageSize": PAGE_SIZE, "pages": page_entries,
        "globalBounds": {"minNorth": center_north - span / 2, "maxNorth": center_north + span / 2,
                         "minEast": center_east - span / 2, "maxEast": center_east + span / 2},
        "overviewRows": overview,
    })
    print(f"Prepared {len(rows)} real pose rows in {len(page_entries)} pages with {len(rows)} paired radar/stereo JPEGs at {output}")


if __name__ == "__main__":
    main()
