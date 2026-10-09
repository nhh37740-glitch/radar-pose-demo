(() => {
  "use strict";

  const config = window.DEMO_CONFIG;
  const status = document.getElementById("status");
  const radarImage = document.getElementById("radar-image");
  const stereoImage = document.getElementById("stereo-image");

  const F = Object.freeze({
    frameIndex: 0,
    timestamp: 1,
    gtNorth: 2,
    gtEast: 3,
    gtDown: 4,
    gtYaw: 5,
    radarPrimaryNorth: 6,
    radarPrimaryEast: 7,
    radarPrimaryDown: 8,
    radarPrimaryYaw: 9,
    temporalNorth: 10,
    temporalEast: 11,
    temporalDown: 12,
    temporalYaw: 13,
    singleFrameNorth: 14,
    singleFrameEast: 15,
    singleFrameDown: 16,
    singleFrameYaw: 17,
    voPrimaryNorth: 18,
    voPrimaryEast: 19,
    voPrimaryDown: 20,
    voPrimaryYaw: 21,
    pureVONorth: 22,
    pureVOEast: 23,
    pureVODown: 24,
    pureVOYaw: 25,
    pureRadarNorth: 26,
    pureRadarEast: 27,
    pureRadarDown: 28,
    pureRadarYaw: 29,
    voPrimaryModelAccepted: 30,
    stereoTimestamp: 31,
  });

  const colors = Object.freeze({
    grid: "rgba(142, 180, 201, 0.13)",
    axis: "rgba(172, 204, 220, 0.48)",
    route: "rgba(167, 199, 214, 0.19)",
    gt: "#51d8ee",
    radarPrimary: "#ff815f",
    temporal: "#b795ff",
    singleFrame: "#57e39b",
    voPrimary: "#ffd166",
    pureVO: "#f2f8fb",
    pureRadar: "#ff5f9e",
    text: "#b6cbd6",
  });

  const methodSpecs = Object.freeze([
    {
      key: "gt",
      canvasId: "gt-map",
      north: F.gtNorth,
      east: F.gtEast,
      down: F.gtDown,
      yaw: F.gtYaw,
      color: colors.gt,
    },
    {
      key: "radar-primary",
      canvasId: "radar-primary-map",
      north: F.radarPrimaryNorth,
      east: F.radarPrimaryEast,
      down: F.radarPrimaryDown,
      yaw: F.radarPrimaryYaw,
      color: colors.radarPrimary,
    },
    {
      key: "temporal",
      canvasId: "temporal-map",
      north: F.temporalNorth,
      east: F.temporalEast,
      down: F.temporalDown,
      yaw: F.temporalYaw,
      color: colors.temporal,
    },
    {
      key: "single-frame",
      canvasId: "single-frame-map",
      north: F.singleFrameNorth,
      east: F.singleFrameEast,
      down: F.singleFrameDown,
      yaw: F.singleFrameYaw,
      color: colors.singleFrame,
    },
    {
      key: "vo-primary",
      canvasId: "vo-primary-map",
      north: F.voPrimaryNorth,
      east: F.voPrimaryEast,
      down: F.voPrimaryDown,
      yaw: F.voPrimaryYaw,
      color: colors.voPrimary,
    },
    {
      key: "pure-vo",
      canvasId: "pure-vo-map",
      north: F.pureVONorth,
      east: F.pureVOEast,
      down: F.pureVODown,
      yaw: F.pureVOYaw,
      color: colors.pureVO,
    },
    {
      key: "pure-radar",
      canvasId: "pure-radar-map",
      north: F.pureRadarNorth,
      east: F.pureRadarEast,
      down: F.pureRadarDown,
      yaw: F.pureRadarYaw,
      color: colors.pureRadar,
    },
  ].map((spec) => {
    const canvas = document.getElementById(spec.canvasId);
    const globalCanvas = document.getElementById(`${spec.key}-global-map`);
    return {
      ...spec,
      canvas,
      context: canvas.getContext("2d"),
      globalCanvas,
      globalContext: globalCanvas.getContext("2d"),
    };
  }));

  const MAX_CACHED_IMAGES = 32;
  const FRAME_STEP = Math.max(1, Math.floor(Number(config?.frameStep) || 10));
  const LOAD_TIMEOUT_MS = Math.max(1000, Number(config?.loadTimeoutMs) || 15000);
  let clip = [];
  let cameraCenters = [];
  let localBounds = null;
  let globalBounds = null;
  let metadata = null;
  let overviewRows = [];
  let fullManifest = null;
  let usingFullData = false;
  let pageStart = 0;
  let totalFrames = 0;
  let transitioning = false;
  let requestVersion = 0;
  let ready = false;
  let loadingMessage = "Loading saved pose records…";
  let dataError = "";
  let retryFrame = 0;
  const notice = document.getElementById("data-notice");
  const seek = document.getElementById("frame-seek");
  const playbackToggle = document.getElementById("playback-toggle");
  let currentIndex = 0;
  let paused = false;
  let lastStep = 0;
  const radarImageCache = new Map();
  const stereoImageCache = new Map();
  const radarImageInflight = new Map();
  const stereoImageInflight = new Map();

  function fail(message) {
    paused = true;
    dataError = message;
    loadingMessage = "";
    updatePlaybackState();
    console.error(message);
  }

  function updatePlaybackState() {
    playbackToggle.textContent = paused ? "Resume" : "Pause";
    playbackToggle.setAttribute("aria-pressed", String(paused));
    status.classList.toggle("error", Boolean(dataError));
    const position = `saved pose replay ${pageStart + currentIndex + 1} / ${totalFrames}`;
    status.textContent = dataError || (loadingMessage
      ? `${paused ? "Paused · " : ""}${loadingMessage}`
      : `${paused ? "Paused" : "Recorded"} · ${position} · ${FRAME_STEP}-frame steps`);
  }

  function ensureTrailingSlash(path) {
    return path.endsWith("/") ? path : `${path}/`;
  }

  function radarImageUrl(timestamp) {
    const directory = usingFullData ? config.fullRadarImageDirectory : config.radarImageDirectory;
    return `${ensureTrailingSlash(directory)}${timestamp}.jpg`;
  }

  function stereoImageUrl(timestamp) {
    const directory = usingFullData ? config.fullStereoImageDirectory : config.stereoImageDirectory;
    return `${ensureTrailingSlash(directory)}${timestamp}.jpg`;
  }

  async function fetchJson(path) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), LOAD_TIMEOUT_MS);
    try {
      const response = await fetch(path, { cache: "default", signal: controller.signal });
      if (!response.ok) throw new Error(`Could not load recorded data: ${path} (HTTP ${response.status})`);
      return await response.json();
    } finally {
      clearTimeout(timer);
    }
  }

  function loadPoseScript(path) {
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      const timer = setTimeout(() => {
        script.remove();
        reject(new Error(`Timed out loading pose data: ${path}`));
      }, LOAD_TIMEOUT_MS);
      script.src = path;
      script.onload = () => { clearTimeout(timer); resolve(); };
      script.onerror = () => { clearTimeout(timer); reject(new Error(`Could not load pose data: ${path}`)); };
      document.head.appendChild(script);
    });
  }

  function computeLocalBounds(center) {
    const span = Number(config.mapSpanMetres || 200);
    if (!Number.isFinite(span) || span <= 0) {
      throw new Error(`Invalid mapSpanMetres: ${config.mapSpanMetres}`);
    }
    const half = span / 2;
    const centerNorth = center.north;
    const centerEast = center.east;
    return {
      minNorth: centerNorth - half,
      maxNorth: centerNorth + half,
      minEast: centerEast - half,
      maxEast: centerEast + half,
    };
  }

  function computeCameraCenters(rows) {
    const smoothing = 0.12;
    let north = rows[0][F.gtNorth];
    let east = rows[0][F.gtEast];
    return rows.map((row) => {
      north += (row[F.gtNorth] - north) * smoothing;
      east += (row[F.gtEast] - east) * smoothing;
      return { north, east };
    });
  }

  function computeGlobalBounds(rows) {
    let minNorth = Infinity;
    let maxNorth = -Infinity;
    let minEast = Infinity;
    let maxEast = -Infinity;
    rows.forEach((row) => {
      minNorth = Math.min(minNorth, row[F.gtNorth]);
      maxNorth = Math.max(maxNorth, row[F.gtNorth]);
      minEast = Math.min(minEast, row[F.gtEast]);
      maxEast = Math.max(maxEast, row[F.gtEast]);
    });
    const centerNorth = (minNorth + maxNorth) / 2;
    const centerEast = (minEast + maxEast) / 2;
    const span = Math.max(maxNorth - minNorth, maxEast - minEast, 10) * 1.12;
    return {
      minNorth: centerNorth - span / 2,
      maxNorth: centerNorth + span / 2,
      minEast: centerEast - span / 2,
      maxEast: centerEast + span / 2,
    };
  }

  function isInside(bounds, north, east) {
    return north >= bounds.minNorth && north <= bounds.maxNorth &&
      east >= bounds.minEast && east <= bounds.maxEast;
  }

  function toCanvas(north, east, canvas, bounds, pad) {
    const width = canvas.width - pad * 2;
    const height = canvas.height - pad * 2;
    const x = pad + ((east - bounds.minEast) / (bounds.maxEast - bounds.minEast)) * width;
    const y = canvas.height - pad -
      ((north - bounds.minNorth) / (bounds.maxNorth - bounds.minNorth)) * height;
    return [x, y];
  }

  function drawGrid(context, canvas) {
    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = "#07121a";
    context.fillRect(0, 0, canvas.width, canvas.height);
    context.strokeStyle = colors.grid;
    context.lineWidth = 1;
    for (let index = 1; index < 8; index += 1) {
      const x = (canvas.width / 8) * index;
      const y = (canvas.height / 8) * index;
      context.beginPath();
      context.moveTo(x, 0);
      context.lineTo(x, canvas.height);
      context.stroke();
      context.beginPath();
      context.moveTo(0, y);
      context.lineTo(canvas.width, y);
      context.stroke();
    }
    context.fillStyle = colors.text;
    context.font = "700 18px Segoe UI, Arial";
    context.fillText("N", 24, 28);
    context.fillText("E", canvas.width - 30, canvas.height - 18);
    context.strokeStyle = colors.axis;
    context.lineWidth = 2;
    context.beginPath();
    context.moveTo(27, 55);
    context.lineTo(27, 34);
    context.lineTo(20, 43);
    context.moveTo(27, 34);
    context.lineTo(34, 43);
    context.stroke();
    context.beginPath();
    context.moveTo(canvas.width - 57, canvas.height - 23);
    context.lineTo(canvas.width - 34, canvas.height - 23);
    context.lineTo(canvas.width - 43, canvas.height - 30);
    context.moveTo(canvas.width - 34, canvas.height - 23);
    context.lineTo(canvas.width - 43, canvas.height - 16);
    context.stroke();
  }

  function drawPoints(spec, endIndex) {
    const drawSet = (limit, fill, radius) => {
      spec.context.fillStyle = fill;
      spec.context.beginPath();
      for (let index = 0; index < limit; index += 1) {
        const row = clip[index];
        if (!isInside(localBounds, row[spec.north], row[spec.east])) continue;
        const [x, y] = toCanvas(
          row[spec.north], row[spec.east], spec.canvas, localBounds, 42,
        );
        spec.context.moveTo(x + radius, y);
        spec.context.arc(x, y, radius, 0, Math.PI * 2);
      }
      spec.context.fill();
    };
    drawSet(clip.length, colors.route, 1.4);
    drawSet(endIndex + 1, spec.color, 2.2);
  }

  function drawOutOfRange(spec, row) {
    const [rawX, rawY] = toCanvas(
      row[spec.north], row[spec.east], spec.canvas, localBounds, 42,
    );
    const pad = 28;
    const x = Math.max(pad, Math.min(spec.canvas.width - pad, rawX));
    const y = Math.max(pad, Math.min(spec.canvas.height - pad, rawY));
    const angle = Math.atan2(rawY - spec.canvas.height / 2, rawX - spec.canvas.width / 2);
    spec.context.save();
    spec.context.translate(x, y);
    spec.context.rotate(angle);
    spec.context.fillStyle = spec.color;
    spec.context.beginPath();
    spec.context.moveTo(15, 0);
    spec.context.lineTo(-10, -10);
    spec.context.lineTo(-10, 10);
    spec.context.closePath();
    spec.context.fill();
    spec.context.restore();
  }

  function drawVehicleIcon(context, x, y, yawDeg, color, scale = 1) {
    const yaw = (yawDeg * Math.PI) / 180;
    const width = 24 * scale;
    const length = 42 * scale;
    const corner = 5 * scale;
    context.save();
    context.translate(x, y);
    context.rotate(yaw);

    // Shadow keeps the vehicle legible over dense trajectory points.
    context.shadowColor = "rgba(0, 0, 0, 0.75)";
    context.shadowBlur = 7 * scale;
    context.fillStyle = color;
    context.beginPath();
    context.roundRect(-width / 2, -length / 2, width, length, corner);
    context.fill();
    context.shadowBlur = 0;

    // Dark glass and roof panel give a recognizable top-down car silhouette.
    context.fillStyle = "rgba(7, 18, 26, 0.88)";
    context.beginPath();
    context.roundRect(
      -width * 0.34,
      -length * 0.24,
      width * 0.68,
      length * 0.46,
      3 * scale,
    );
    context.fill();
    context.strokeStyle = "rgba(255, 255, 255, 0.5)";
    context.lineWidth = 1.2 * scale;
    context.beginPath();
    context.moveTo(-width * 0.31, -length * 0.07);
    context.lineTo(width * 0.31, -length * 0.07);
    context.stroke();

    // Four wheels.
    context.fillStyle = "#020507";
    const wheelWidth = 4 * scale;
    const wheelHeight = 10 * scale;
    const wheelX = width / 2 + wheelWidth * 0.15;
    const wheelY = length * 0.22;
    for (const side of [-1, 1]) {
      for (const axle of [-1, 1]) {
        context.fillRect(
          side * wheelX - wheelWidth / 2,
          axle * wheelY - wheelHeight / 2,
          wheelWidth,
          wheelHeight,
        );
      }
    }

    // Headlights mark the front, which points along the model yaw.
    context.fillStyle = "#fff2a8";
    for (const side of [-1, 1]) {
      context.beginPath();
      context.arc(
        side * width * 0.29,
        -length * 0.43,
        2.2 * scale,
        0,
        Math.PI * 2,
      );
      context.fill();
    }

    context.strokeStyle = "rgba(255, 255, 255, 0.88)";
    context.lineWidth = 1.4 * scale;
    context.beginPath();
    context.roundRect(-width / 2, -length / 2, width, length, corner);
    context.stroke();
    context.restore();
  }

  function drawPose(spec, row, index) {
    drawGrid(spec.context, spec.canvas);
    drawPoints(spec, index);
    if (!isInside(localBounds, row[spec.north], row[spec.east])) {
      drawOutOfRange(spec, row);
      return;
    }
    const [x, y] = toCanvas(
      row[spec.north], row[spec.east], spec.canvas, localBounds, 42,
    );
    drawVehicleIcon(spec.context, x, y, row[spec.yaw], spec.color);
  }

  function drawGlobalMap(spec, row, endIndex) {
    const canvas = spec.globalCanvas;
    const context = spec.globalContext;
    context.clearRect(0, 0, canvas.width, canvas.height);
    context.fillStyle = "#07121a";
    context.fillRect(0, 0, canvas.width, canvas.height);

    const drawTrack = (northField, eastField, color, alpha) => {
      context.save();
      context.globalAlpha = alpha;
      context.fillStyle = color;
      context.beginPath();
      for (const routeRow of overviewRows) {
        if (routeRow[F.frameIndex] > pageStart + endIndex) break;
        const [x, y] = toCanvas(
          routeRow[northField], routeRow[eastField], canvas, globalBounds, 15,
        );
        context.moveTo(x + 1.25, y);
        context.arc(x, y, 1.25, 0, Math.PI * 2);
      }
      context.fill();
      context.restore();
    };
    drawTrack(F.gtNorth, F.gtEast, colors.gt, 0.42);
    if (spec.key !== "gt") {
      drawTrack(spec.north, spec.east, spec.color, 0.76);
    }

    const drawCurrentMarker = (northField, eastField, color, radius) => {
      const [rawX, rawY] = toCanvas(
        row[northField], row[eastField], canvas, globalBounds, 15,
      );
      const x = Math.max(8, Math.min(canvas.width - 8, rawX));
      const y = Math.max(8, Math.min(canvas.height - 8, rawY));
      context.fillStyle = color;
      context.beginPath();
      context.arc(x, y, radius, 0, Math.PI * 2);
      context.fill();
    };
    drawCurrentMarker(F.gtNorth, F.gtEast, colors.gt, 4.5);
    if (spec.key !== "gt") {
      drawCurrentMarker(spec.north, spec.east, spec.color, 4);
    }

    context.fillStyle = colors.text;
    context.font = "700 13px Segoe UI, Arial";
    context.fillText("N ↑", canvas.width - 34, 16);
  }

  function formatMetres(value) {
    return `${Number(value).toFixed(1)} m`;
  }

  function formatDegrees(value) {
    return `${Number(value).toFixed(1)}°`;
  }

  function setText(id, value) {
    document.getElementById(id).textContent = value;
  }

  function updateValues(row) {
    methodSpecs.forEach((spec) => {
      setText(`${spec.key}-north`, formatMetres(row[spec.north]));
      setText(`${spec.key}-east`, formatMetres(row[spec.east]));
      setText(`${spec.key}-down`, formatMetres(row[spec.down]));
      setText(`${spec.key}-yaw`, formatDegrees(row[spec.yaw]));
    });
  }

  function updateMethodStates(row) {
    const voBadge = document.getElementById("vo-primary-state");
    const modelAccepted = Boolean(row[F.voPrimaryModelAccepted]);
    voBadge.textContent = modelAccepted ? "SAVED MODEL" : "SAVED VO";
    voBadge.classList.toggle("model", modelAccepted);
  }

  function render(index) {
    const row = clip[index];
    localBounds = computeLocalBounds(cameraCenters[index]);
    const cachedRadar = radarImageCache.get(row[F.timestamp]);
    const cachedStereo = stereoImageCache.get(row[F.stereoTimestamp]);
    displaySensorImage(radarImage, cachedRadar, "radar");
    displaySensorImage(stereoImage, cachedStereo, "stereo");
    setText("frame-label", `Frame ${row[F.frameIndex]}`);
    setText("timestamp-label", `Timestamp ${row[F.timestamp]}`);
    updateValues(row);
    methodSpecs.forEach((spec) => drawPose(spec, row, index));
    methodSpecs.forEach((spec) => drawGlobalMap(spec, row, index));
    updateMethodStates(row);
    const absoluteIndex = pageStart + index;
    seek.value = String(absoluteIndex);
    setText("seek-label", `Frame ${absoluteIndex} / ${totalFrames - 1}`);
    updatePlaybackState();
  }

  function displaySensorImage(element, loaded, label) {
    const message = document.getElementById(`${label}-image-status`);
    element.hidden = !loaded;
    if (loaded) element.src = loaded.src;
    else element.removeAttribute("src");
    message.hidden = Boolean(loaded);
    message.textContent = loaded ? "" : `${label === "radar" ? "Radar" : "Stereo"} image unavailable for this frame. Use playback or seek to continue.`;
  }

  function loadCachedImage(cache, inflight, key, url, label) {
    if (cache.has(key)) {
      return Promise.resolve(cache.get(key));
    }
    if (inflight.has(key)) return inflight.get(key);
    const promise = new Promise((resolve, reject) => {
      const image = new Image();
      let settled = false;
      const finish = (error) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        image.onload = null;
        image.onerror = null;
        inflight.delete(key);
        if (error) reject(error);
        else resolve(image);
      };
      const timer = setTimeout(() => {
        finish(new Error(`Timed out loading ${label} image: ${url}`));
        image.src = "";
      }, LOAD_TIMEOUT_MS);
      image.onload = () => {
        cache.set(key, image);
        while (cache.size > MAX_CACHED_IMAGES) {
          const oldestTimestamp = cache.keys().next().value;
          cache.delete(oldestTimestamp);
        }
        finish();
      };
      image.onerror = () => {
        finish(new Error(`Missing ${label} image: ${url}`));
      };
      image.src = url;
    });
    inflight.set(key, promise);
    return promise;
  }

  function loadRadarImage(row) {
    const timestamp = row[F.timestamp];
    return loadCachedImage(
      radarImageCache,
      radarImageInflight,
      timestamp,
      radarImageUrl(timestamp),
      "radar",
    );
  }

  function loadStereoImage(row) {
    const timestamp = row[F.stereoTimestamp];
    return loadCachedImage(
      stereoImageCache,
      stereoImageInflight,
      timestamp,
      stereoImageUrl(timestamp),
      "stereo",
    );
  }

  function validateManifest(manifest) {
    const count = Number(manifest?.metadata?.sampleCount);
    if (!Number.isInteger(count) || count < 1 || !Number.isInteger(manifest.pageSize) ||
        manifest.pageSize < 1 || !Array.isArray(manifest.pages) ||
        !Array.isArray(manifest.overviewRows) || manifest.overviewRows.length === 0 ||
        !manifest.globalBounds || manifest.formatVersion !== 1) {
      throw new Error("Full recording manifest is invalid");
    }
    let next = 0;
    for (const [index, page] of manifest.pages.entries()) {
      if (page.startFrame !== next || page.startFrame !== index * manifest.pageSize ||
          !Number.isInteger(page.count) || page.count < 1 || page.count > manifest.pageSize ||
          (index < manifest.pages.length - 1 && page.count !== manifest.pageSize) ||
          !/^chunks\/page-\d{5}\.json$/.test(page.file)) {
        throw new Error("Full recording page index is invalid");
      }
      next += page.count;
    }
    if (next !== count || manifest.overviewRows[0][F.frameIndex] !== 0 ||
        manifest.overviewRows.at(-1)[F.frameIndex] !== count - 1) {
      throw new Error("Full recording frame count is inconsistent");
    }
  }

  async function loadPage(pageNumber, absoluteIndex) {
    const page = fullManifest.pages[pageNumber];
    if (!page || absoluteIndex < page.startFrame || absoluteIndex >= page.startFrame + page.count) {
      throw new Error(`Frame ${absoluteIndex} is outside the recorded sequence`);
    }
    const data = await fetchJson(`./full/${page.file}`);
    if (data.startFrame !== page.startFrame || !Array.isArray(data.frames) ||
        data.frames.length !== page.count || data.frames.some((row, offset) =>
          !Array.isArray(row) || row.length !== 32 || row[F.frameIndex] !== page.startFrame + offset)) {
      throw new Error(`Recorded frame page ${pageNumber} is missing or invalid`);
    }
    return { frames: data.frames, start: page.startFrame };
  }

  async function seekFrame(absoluteIndex) {
    if (!ready || !Number.isInteger(absoluteIndex) || absoluteIndex < 0 || absoluteIndex >= totalFrames) return;
    const version = ++requestVersion;
    retryFrame = absoluteIndex;
    transitioning = true;
    dataError = "";
    loadingMessage = `Loading recorded frame ${absoluteIndex}…`;
    updatePlaybackState();
    try {
      let nextClip = clip;
      let nextStart = pageStart;
      if (usingFullData && (absoluteIndex < pageStart || absoluteIndex >= pageStart + clip.length)) {
        const pageNumber = Math.floor(absoluteIndex / fullManifest.pageSize);
        const page = await loadPage(pageNumber, absoluteIndex);
        if (version !== requestVersion) return;
        nextClip = page.frames;
        nextStart = page.start;
      }
      const nextIndex = absoluteIndex - nextStart;
      const row = nextClip[nextIndex];
      const images = await Promise.allSettled([loadRadarImage(row), loadStereoImage(row)]);
      if (version !== requestVersion) return;
      images.forEach((result) => {
        if (result.status === "rejected") console.warn(result.reason.message);
      });
      clip = nextClip;
      pageStart = nextStart;
      currentIndex = nextIndex;
      cameraCenters = computeCameraCenters(clip);
      loadingMessage = "";
      render(currentIndex);
      lastStep = performance.now();
    } catch (error) {
      if (version === requestVersion) {
        if (clip.length) seek.value = String(pageStart + currentIndex);
        fail(`${error.message}. Resume to retry, or seek another frame.`);
      }
    } finally {
      if (version === requestVersion) transitioning = false;
    }
  }

  function animate(timestamp) {
    if (ready && !paused && !transitioning && !dataError && timestamp - lastStep >= config.intervalMs) {
      const absoluteIndex = pageStart + currentIndex;
      const next = absoluteIndex === totalFrames - 1 ? 0 : Math.min(absoluteIndex + FRAME_STEP, totalFrames - 1);
      lastStep = timestamp;
      void seekFrame(next);
    }
    window.requestAnimationFrame(animate);
  }

  function togglePlayback() {
    paused = !paused;
    if (paused && ready && transitioning && clip.length > 0) {
      // Keep the displayed frame frozen; late image/page responses only populate caches.
      requestVersion += 1;
      transitioning = false;
      loadingMessage = "";
      seek.value = String(pageStart + currentIndex);
    }
    if (!paused) {
      lastStep = performance.now();
      if (dataError) {
        dataError = "";
        if (ready) void seekFrame(retryFrame);
        else void start().catch((error) => fail(error.message));
      }
    }
    updatePlaybackState();
  }

  function bindControls() {
    playbackToggle.disabled = false;
    playbackToggle.addEventListener("click", togglePlayback);
    window.addEventListener("keydown", (event) => {
      if (event.code === "Space") {
        event.preventDefault();
        if (!event.repeat) togglePlayback();
      } else if (event.key.toLowerCase() === "r") {
        void seekFrame(0);
      }
    });
    seek.disabled = true;
    seek.addEventListener("change", () => {
      paused = true;
      updatePlaybackState();
      void seekFrame(Number(seek.value));
    });
  }

  async function loadExcerptFallback() {
    await loadPoseScript(config.poseDataScript);
    const data = window.RADAR_POSE_METHOD_DATA;
    if (!data || !Array.isArray(data.frames)) {
      throw new Error("Full recording and saved excerpt are both unavailable");
    }
    metadata = data.metadata;
    const startIndex = Number(config.startFrame);
    if (!Number.isInteger(startIndex) || startIndex < 0) {
      throw new Error(`Invalid startFrame: ${config.startFrame}`);
    }
    const requestedFrameCount = Number(config.frameCount);
    const frameCount = requestedFrameCount > 0 ? requestedFrameCount : data.frames.length - startIndex;
    clip = data.frames.slice(startIndex, startIndex + frameCount);
    if (clip.length !== frameCount || clip.length === 0 ||
        clip.some((row) => !Array.isArray(row) || row.length < 32)) {
      throw new Error("Saved excerpt is missing or invalid");
    }
    totalFrames = clip.length;
    pageStart = 0;
    cameraCenters = computeCameraCenters(clip);
    globalBounds = computeGlobalBounds(clip);
    overviewRows = clip;
    notice.textContent = `Full recording data is unavailable. Showing the ${totalFrames}-frame real excerpt only.`;
    notice.classList.add("error");
  }

  async function start() {
    if (!config) throw new Error("DEMO_CONFIG is missing");
    loadingMessage = "Loading saved pose records…";
    updatePlaybackState();
    let manifest;
    try {
      manifest = await fetchJson(config.fullDataManifest);
    } catch (error) {
      await loadExcerptFallback();
    }
    if (manifest) {
      validateManifest(manifest);
      fullManifest = manifest;
      usingFullData = true;
      metadata = manifest.metadata;
      totalFrames = metadata.sampleCount;
      globalBounds = manifest.globalBounds;
      overviewRows = manifest.overviewRows;
      notice.textContent = `Complete real recording: ${totalFrames} paired radar and stereo frames · ${metadata.recordedDurationSeconds.toFixed(2)} seconds of capture · saved pose estimates.`;
    }
    seek.max = String(totalFrames - 1);
    seek.disabled = false;
    ready = true;
    await seekFrame(0);
  }

  bindControls();
  window.requestAnimationFrame(animate);
  start().catch((error) => fail(error.message));
})();
