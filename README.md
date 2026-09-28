# Radar Pose Playback

An independently versioned and deployed static module for replaying a real, contiguous excerpt from Oxford sequence `2019-01-15-13-06-37`.

The release contains real recorded radar and stereo frames, plus saved per-frame pose estimates for source frames **0–239**. That is 240 contiguous samples spanning about **59.73 seconds** of capture. The browser animates the records at an accelerated display interval; it does not run a model, call an inference API, or perform live inference.

## Module boundaries

- `web/` contains only the runtime website, excerpt records, and paired images.
- `tools/prepare_excerpt.py` selects a contiguous source interval, verifies each radar/stereo timestamp against the original pose rows, emits only that interval, and writes a self-contained static module. The complete dataset path is needed only when intentionally regenerating a different interval.
- `templates/data-license.html` is the attributed notice template used to generate a range-specific public license page.
- `tools/package_release.py` validates source row continuity, frame-to-image correspondence, attribution, license disclosures, and the allowlisted module files. It emits a deterministic versioned ZIP, JSON manifest, and SHA-256 file under `dist/`.
- `Dockerfile` serves the module's complete static runtime without data mounts or build-time dataset dependencies.
- `compose.yaml` binds the service to `127.0.0.1:18104`; the public project gateway can proxy `/projects/radar/` to it.

## Regenerate the current excerpt

Use a complete `radar-pose-demo` source directory that contains `indexbak.html`, `app.js`, `styles.css`, `config.js`, the full pose-data JS, and the matching `assets/radar` and `assets/stereo` image directories:

```bash
python3 tools/prepare_excerpt.py --source /path/to/radar-pose-demo
python3 tools/package_release.py
```

By default the preparer selects 240 contiguous frames starting at source frame 0 and plays them at a 100 ms interval. The source directory is not required to package or run the checked-in excerpt.

## Validate and serve

```bash
python3 tools/package_release.py
docker compose --project-name radar-pose-demo up -d --build
curl -fsS http://127.0.0.1:18104/index.html
```

Jenkins runs the same package gate, archives the ZIP/manifest/SHA-256, builds the static container, and deploys the loopback service with HTTP health and content checks.

## Attribution and use

Included Oxford sensor images and excerpted reference/derived pose records are attributed to the Oxford Radar RobotCar Dataset and the Oxford RobotCar Dataset and shared under **CC BY-NC-SA 4.0**. Redistribution is for non-commercial academic use and must retain attribution and share-alike terms. The license does not grant rights to unrelated project code. See [`web/data-license.html`](web/data-license.html) for the official license and privacy links and recommended citations. The release manifest records the same attribution and license scope.

Primary references include Barnes et al., *The Oxford Radar RobotCar Dataset: A Radar Extension to the Oxford RobotCar Dataset* (ICRA 2020, DOI `10.1109/ICRA40945.2020.9196884`); Maddern et al., *1 Year, 1000km: The Oxford RobotCar Dataset* (IJRR 2017, DOI `10.1177/0278364916679498`); and Maddern et al., *Real-time Kinematic Ground Truth for the Oxford RobotCar Dataset* (arXiv:2002.10152, 2020).
