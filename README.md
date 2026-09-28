# Radar Pose Playback

An independently versioned and deployed static module for replaying the complete real Oxford sequence `2019-01-15-13-06-37`.

The complete binary data pack contains **7,203** paired radar and stereo frames, with saved pose estimates for frames **0–7202**, spanning about **1800.02 seconds** of capture. The browser fetches 240 pose rows per page and keeps only 32 radar and 32 stereo images in its application cache. A scrubber can seek any recorded frame. The existing 240-frame real excerpt remains an explicitly labeled fallback if the complete data pack is unavailable. The browser does not run a model, call an inference API, or perform live inference.

## Module boundaries

- `web/` contains the runtime website and the existing real 240-frame fallback. It remains small in the private source repository.
- `tools/prepare_full_data.py` validates the 7,203 source rows and paired JPEGs, then writes 31 JSON pages, a route overview, and the original images into the Git-ignored `runtime-data/` directory. Run it on the server against the transferred original data directory.
- `tools/package_release.py` validates every full pose row and image, attribution, and the public file allowlist. It streams image hashes and ZIP entries in bounded chunks. The versioned full binary ZIP, per-file manifest, and SHA-256 file are written under `dist/` by Jenkins.
- `Dockerfile` serves the small website and fallback. Compose mounts `runtime-data/` read-only at `/full/` in that isolated container.
- `compose.yaml` binds the service to `127.0.0.1:18104`; the public project gateway can proxy `/projects/radar/` to it.

## Prepare the complete binary data pack on the server

Transfer the original full pose-data JS and paired `assets/radar` and `assets/stereo` directories to a server-side source directory. Keep the data pack separate from GitHub source history. The source directory needs `data/method_comparison_jan15_cfear_lite_pose_data.js` and the two image directories. In the server Jenkins job, set `RADAR_SOURCE_DIRECTORY` to that directory; Jenkins prepares `runtime-data/` if it is absent, then validates and packages it. To prepare manually on the server:

```bash
python3 tools/prepare_full_data.py --source /path/to/radar-pose-demo
python3 tools/package_release.py
```

The generator refuses a nonempty output directory, preventing accidental mixing of data versions. To update an existing pack, prepare into a new empty directory and switch the pack after validation. The browser plays the complete sequence at a 100 ms interval. `tools/prepare_excerpt.py` remains only for regenerating the separate fallback; do not run it during full release preparation.

## Validate and serve

```bash
python3 tools/package_release.py
sudo docker compose --project-name radar-pose-demo up -d --build
curl -fsS http://127.0.0.1:18104/index.html
```

The Jenkins agent uses `sudo docker compose` because its service account does not have direct access to `/var/run/docker.sock`. Jenkins validates and archives the complete binary ZIP/manifest/SHA-256, builds the isolated static container on the server, and smoke-checks the final page and paired images over loopback.

## Attribution and use

Included Oxford sensor images and reference/derived pose records are attributed to the Oxford Radar RobotCar Dataset and the Oxford RobotCar Dataset and shared under **CC BY-NC-SA 4.0**. Redistribution is for non-commercial academic use and must retain attribution and share-alike terms. The license does not grant rights to unrelated project code. See [`web/data-license.html`](web/data-license.html) for the official license and privacy links and recommended citations. The release manifest records the same attribution and license scope.

Primary references include Barnes et al., *The Oxford Radar RobotCar Dataset: A Radar Extension to the Oxford RobotCar Dataset* (ICRA 2020, DOI `10.1109/ICRA40945.2020.9196884`); Maddern et al., *1 Year, 1000km: The Oxford RobotCar Dataset* (IJRR 2017, DOI `10.1177/0278364916679498`); and Maddern et al., *Real-time Kinematic Ground Truth for the Oxford RobotCar Dataset* (arXiv:2002.10152, 2020).
