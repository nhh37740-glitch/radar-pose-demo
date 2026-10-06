# Qt native radar integration

Keep the existing website and its complete Oxford 7,203-frame recorded-data contract intact. This Qt application replays saved images/poses; it does not claim to run live inference.

Frozen public interfaces: include/radar/contracts.h. Each business module must be a real SHARED DLL with its public factory. No module includes another module's implementation source. One owner per module.

- radar_data_reader owns modules/data_reader and tests/data_reader_test.cpp.
- radar_playback owns modules/playback and tests/playback_test.cpp.
- radar_frontend owns modules/frontend and tests/frontend_test.cpp.
- Root owns contracts, application wiring, top-level build, SDK example, scripts, documentation, real-data/end-to-end verification and binary delivery.

Use explicit queued connections across GUI and file-reader threads. File/image reads and CSV export run in the file-reader thread. Qt Widgets remain in the main thread. All queued messages carry value copies; requests have monotonically increasing IDs so obsolete seek results cannot overwrite the current frame. Timer playback must stop when paused/end/error, and stale results must be ignored. No idle animation/render loops.

Do not mutate live runtime-data or original source assets. Keep generated data and archives out of Git. Record actual tests and unresolved issues; do not claim success before execution.
