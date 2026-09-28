# Radar Pose Playback module

## Delivered behavior

The module is an independently deployable static replay of Oxford Radar RobotCar sequence `2019-01-15-13-06-37`, source frames 0–239. It contains 240 contiguous saved records and exactly one matching real radar image and stereo image per record. The page identifies the recorded subset and duration and says that all pose estimates are precomputed; no live inference runs in the browser.

## Boundaries and delivery

- `web/` is the complete isolated runtime root.
- `tools/prepare_excerpt.py` is an optional offline source importer. It requires the complete local source only to regenerate the excerpt.
- `tools/package_release.py` validates the data/image contract and builds the module ZIP, manifest, and SHA-256 delivery.
- `Dockerfile` copies only `web/` into Nginx; `compose.yaml` binds to host loopback port 18104.
- The portfolio contains only an entry card and gateway route. It does not vendor the excerpt or duplicate the media files.

## Rights and provenance

Oxford RobotCar dataset pages state CC BY-NC-SA 4.0 and non-commercial academic use. The page and manifest provide attribution, license scope, privacy notice, and requested citations for the radar dataset, RobotCar dataset, and released RTK ground truth. Data and excerpted reference/derived pose records are distributed with those terms; unrelated project code is outside that license. Recheck the official Oxford notices if the use or distribution context changes.

## Validation gates

1. Confirm the metadata names the expected sequence and explicitly identifies saved-data replay with `liveInference: false`.
2. Confirm frame indices are contiguous and the image filenames exactly match each row's radar and stereo timestamps.
3. Confirm the entry pages disclose the excerpt range, duration, no-live-inference behavior, attribution, and license links.
4. Package a deterministic ZIP with SHA-256 and per-file manifest, then build/deploy the isolated static container.
5. Smoke-check the page, disclosure, data script, paired images, and container health over loopback.
