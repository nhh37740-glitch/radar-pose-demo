# Radar Pose Playback module

## Delivered behavior

The module is an independently deployable static replay of Oxford Radar RobotCar sequence `2019-01-15-13-06-37`, source frames 0–7202. Its separate binary data pack contains 7,203 contiguous saved records and exactly one matching real radar image and stereo image per record. The page loads pose records in bounded pages, supports seeking every frame, and says that all pose estimates are precomputed; no live inference runs in the browser.

## Boundaries and delivery

- `web/` is the small isolated runtime root and real 240-frame fallback.
- `tools/prepare_full_data.py` runs on the server against the transferred original sensor files; it validates a fresh sibling staging directory before publishing `runtime-data/`. That complete paged binary data pack is Git ignored and is never mutated while mounted live.
- `tools/package_release.py` validates the full data/image contract and streams the complete versioned ZIP, manifest, and SHA-256 delivery with bounded memory.
- `Dockerfile` copies only `web/` into Nginx; `compose.yaml` mounts `runtime-data/` read-only at `/full/` and binds to host loopback port 18104.
- The portfolio contains only an entry card and gateway route. It does not vendor or duplicate the media files.

## Rights and provenance

Oxford RobotCar dataset pages state CC BY-NC-SA 4.0 and non-commercial academic use. The page and manifest provide attribution, license scope, privacy notice, and requested citations for the radar dataset, RobotCar dataset, and released RTK ground truth. Included sensor data and reference/derived pose records are distributed with those terms; unrelated project code is outside that license. Recheck the official Oxford notices if the use or distribution context changes.

## Validation gates

1. Confirm the metadata names the expected sequence and explicitly identifies saved-data replay with `liveInference: false`.
2. Confirm all 7,203 frame indices are contiguous and the image filenames exactly match each row's radar and stereo timestamps.
3. Confirm the entry pages and data-license page disclose the complete range, fallback behavior, no-live-inference behavior, attribution, and license links.
4. Package a deterministic ZIP with SHA-256 and per-file manifest using streamed image reads, then build the isolated static container on the server.
5. Smoke-check a candidate on loopback port 18105, including the complete manifest, frame 7202 pose row and paired images, before switching production on 18104. Attempt previous-image rollback if the production switch fails.
