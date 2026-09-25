// server.js
const express = require("express");
const cors = require("cors");
const fs = require("fs");
const net = require("net");
const path = require("path");
const { spawn, execFile } = require("child_process");
const readline = require("readline"); // add here

// -------------------- Logging --------------------
// One line per event: [YYYY-MM-DD HH:MM:SS.mmm][LEVEL][COMPONENT] message
function logTimestamp() {
  return new Date().toISOString().replace("T", " ").replace("Z", "");
}
function log(level, component, message) {
  const line = `[${logTimestamp()}][${level}][${component}] ${message}`;
  if (level === "ERROR") console.error(line);
  else console.log(line);
}
const logInfo = (component, message) => log("INFO", component, message);
const logWarn = (component, message) => log("WARN", component, message);
const logError = (component, message) => log("ERROR", component, message);

const app = express();
app.use(cors({ origin: "*" }));
app.use(express.json({ limit: "200mb" }));

// -------------------- Paths --------------------
// The server lives at <repo>/system/Server; everything is derived from the
// repo root and can be overridden with the same VS4D_* env vars the Python
// side (vstream/config.py) uses.
const REPO_ROOT = path.resolve(__dirname, "..", "..");
const FILES_ROOT = process.env.VS4D_FILES_ROOT || path.join(REPO_ROOT, "files");

// The packaged media cache under FILES_ROOT/media is keyed by representation
// id -- resolution, CRF and geometry QP -- and NOT by codec. So an HEVC and an
// H.264 corpus produce byte-different files at identical paths, and pointing
// VS4D_COMPRESSED_ROOT at a different codec while reusing a populated cache
// serves the OLD codec's bytes under the new corpus's manifest. Nothing errors:
// the ladder's predicted bitrates come from the new models while the client
// downloads the old encodes, so every rate the run reports is wrong by the
// difference between two codecs, and a browser that can decode one but not the
// other appears to succeed or fail at random.
//
// So the cache records which corpus filled it and refuses to be reused by
// another. Use a separate VS4D_FILES_ROOT per corpus.
const COMPRESSED_ROOT = process.env.VS4D_COMPRESSED_ROOT || "";
function enforceCorpusStamp() {
  const stampPath = path.join(FILES_ROOT, "media", ".corpus");
  const mediaDir = path.join(FILES_ROOT, "media");
  const corpus = COMPRESSED_ROOT
    ? path.resolve(COMPRESSED_ROOT) : "(config default)";
  let existing = null;
  try { existing = fs.readFileSync(stampPath, "utf8").trim(); } catch (_) {}

  const populated = (() => {
    try { return fs.readdirSync(mediaDir).some(n => !n.startsWith(".")); }
    catch (_) { return false; }
  })();
  const advice = "Point VS4D_FILES_ROOT at a directory of its own for this "
    + `corpus, or delete ${mediaDir}.`;

  if (populated && existing === null) {
    // Unknown provenance is as dangerous as a known mismatch, and must not be
    // resolved by assuming it matches: that is how the stamp would certify a
    // cache it never checked.
    throw new Error(
      `${mediaDir} already holds packaged media but carries no .corpus stamp, `
      + `so which corpus encoded it cannot be determined. ${advice}`);
  }
  if (populated && existing !== corpus) {
    throw new Error(
      `${mediaDir} was packaged from ${existing} but VS4D_COMPRESSED_ROOT is `
      + `now ${corpus}. Representation ids carry no codec, so reusing this `
      + `cache would serve the old corpus's encodes under the new corpus's `
      + `manifest. ${advice}`);
  }
  try {
    fs.mkdirSync(mediaDir, { recursive: true });
    fs.writeFileSync(stampPath, corpus + "\n");
  } catch (error) {
    logInfo("FILES", `could not stamp the media cache: ${error.message}`);
  }
}

// Reproducibility without endpoint or identity leakage. This captures all
// experiment knobs (including ladder QP/texture caps) while excluding values
// that can contain an IP, path, credential, or machine-specific endpoint.
const PRIVATE_CONFIG_KEY = /(host|url|token|secret|password|key|root|path|dir|(^|_)ip($|_)|ipaddress)/i;
const isPrivateConfigKey = key => PRIVATE_CONFIG_KEY.test(key) || /Ip$/.test(key);
function studySafeEnvironment() {
  return Object.fromEntries(Object.entries(process.env)
    .filter(([key]) => key.startsWith("VS4D_") && !isPrivateConfigKey(key))
    .sort(([left], [right]) => left.localeCompare(right)));
}
function privacySafeObject(value) {
  if (Array.isArray(value)) return value.map(privacySafeObject);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(Object.entries(value)
    .filter(([key]) => !isPrivateConfigKey(key) && !/controller/i.test(key))
    .map(([key, item]) => [key, privacySafeObject(item)]));
}
const redactIpAddresses = value => String(value ?? "")
  .replace(/(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])/g, "[ip-redacted]");

app.use("/files", express.static(FILES_ROOT));

// Browser client. Serves system/WebClient/dist, which `node build.js` in that
// directory produces; the page then talks to this same server through the
// ordinary /api routes, exactly as the desktop and Quest clients do.
//
// no-store on the bundle: during development an aggressively cached main.js is
// indistinguishable from a code change that "did nothing".
const SYSTEM_ROOT_CLIENT = path.join(REPO_ROOT, "system/Client");
const WEB_CLIENT_DIST = path.join(REPO_ROOT, "system/WebClient/dist");
app.use("/web", express.static(WEB_CLIENT_DIST, {
  setHeaders: res => res.set("Cache-Control", "no-store")
}));
// Vega's exported VGS splat frames for the browser viewer. A separate root
// from /files because these are a baseline's offline-evaluation assets, not
// ladder media; produce them with orbitvega.export_quest.
// NeVo's pre-rendered ReRF/NeVo/reference frames for the browser viewer.
// Produced by orbitnevo/render_frames.py; NeVo cannot be rendered client-side,
// so the page plays these images.
const NEVO_ASSETS_ROOT = process.env.VS4D_NEVO_WEB_ROOT
  || path.join(process.env.HOME || REPO_ROOT, "nevo_output");
app.use("/nevo-assets", express.static(NEVO_ASSETS_ROOT, {
  setHeaders: res => res.set("Cache-Control", "no-store")
}));

// The prepared ViVo tile corpus, shared by the ViVo and NAVA servers. Only
// read here to report whether those baselines *could* run; the server process
// is started by hand.
const VIVO_TILES_ROOT = process.env.VS4D_VIVO_TILES_ROOT
  || "/media/frozzzen/DataDrive/ORBIT_vivo_tiles";

const VEGA_ASSETS_ROOT = process.env.VS4D_VEGA_WEB_ROOT
  || path.join(REPO_ROOT, "results/vega-web");
app.use("/vega-assets", express.static(VEGA_ASSETS_ROOT, {
  setHeaders: res => res.set("Cache-Control", "no-store")
}));

app.get("/web", (req, res) => {
  if (!fs.existsSync(path.join(WEB_CLIENT_DIST, "index.html"))) {
    return res.status(503).type("text/plain").send(
      "Browser client is not built.\n\n"
      + "  cd system/WebClient && npm install && node build.js\n");
  }
  res.sendFile(path.join(WEB_CLIENT_DIST, "index.html"));
});

// A bulk, allocation-bounded response gives the Quest a stable end-to-end
// HTTP capacity sample. Short media files otherwise measure request/connection
// overhead more than link capacity.
const BANDWIDTH_PROBE_CHUNK = Buffer.alloc(1024 * 1024, 0x5a);
app.get("/api/bandwidth-probe", async (req, res) => {
  const requested = Number.parseInt(req.query.bytes, 10);
  const bytes = Math.max(1, Math.min(64 * 1000 * 1000,
    Number.isFinite(requested) ? requested : 32 * 1000 * 1000));
  res.set({
    "Content-Type": "application/octet-stream",
    "Content-Length": String(bytes),
    "Cache-Control": "no-store, no-transform",
  });
  let remaining = bytes;
  while (remaining > 0 && !res.destroyed)
  {
    const chunk = remaining >= BANDWIDTH_PROBE_CHUNK.length
      ? BANDWIDTH_PROBE_CHUNK : BANDWIDTH_PROBE_CHUNK.subarray(0, remaining);
    remaining -= chunk.length;
    if (!res.write(chunk)) await new Promise(resolve => res.once("drain", resolve));
  }
  if (!res.destroyed) res.end();
});

// Stream one object's Draco frames in a length-prefixed container. This keeps
// the exact compressed media bytes but avoids sixty small HTTP transactions.
// The headset's decoded-geometry cache decides whether a segment costs a
// 60-frame Draco decode or a disk read, and which storage root it resolved to
// is invisible from here otherwise. Reporting it means a run's cache state is
// on the server console instead of only in a volatile adb ring buffer.
app.post("/api/geometry-cache", (req, res) => {
  const b = req.body || {};
  const used = Number.isFinite(b.cachedBytes) && Number.isFinite(b.maxBytes)
    ? ` used=${(b.cachedBytes / 1e9).toFixed(2)}/${(b.maxBytes / 1e9).toFixed(1)}GB`
    : "";
  const root = b.root ? ` root=${b.root}` : "";
  const entries = Number.isFinite(b.entryCount) ? ` entries=${b.entryCount}` : "";
  if (b.event === "warmup-progress" || b.event === "warmup-finished") {
    const finished = b.event === "warmup-finished";
    logInfo("CACHE", `${finished ? "warm-up finished" : "warm-up"}`
      + ` ${b.done ?? 0}/${b.total ?? 0}: cached=${b.cached ?? 0}`
      + ` decoded=${b.decoded ?? 0} failed=${b.failed ?? 0}${entries}${used}`
      // Repeat the root on the closing line: when a warm-up ends having
      // written nothing, where it was writing is the whole diagnosis.
      + (finished ? root : "")
      + (b.error ? ` error=${b.error}` : ""));
  } else {
    logInfo("CACHE", `${b.event || "report"}:${root}${entries}${used}`
      + (b.error ? ` error=${b.error}` : ""));
  }
  return res.json({ status: "ok" });
});

// Enumerates every (object, QP, segment start) the headset can be asked to
// play, so it can decode them all once instead of paying a 60-frame Draco
// decode whenever the ladder picks a QP it has not seen. Written by
// scripts/warm_geometry_links.py; regenerate that after changing the object
// catalogue or the QP sweep.
app.get("/api/geometry-warmup-plan", (req, res) => {
  const planFile = path.join(FILES_ROOT, "geometry-warmup-plan.json");
  if (!fs.existsSync(planFile)) {
    return res.status(404).json({
      error: "no geometry warm-up plan published",
      hint: "run: python -m scripts.warm_geometry_links",
    });
  }
  try {
    const plan = JSON.parse(fs.readFileSync(planFile, "utf8"));
    logInfo("WARMUP", `Sent warm-up plan: ${plan.entries?.length ?? 0} entries`);
    return res.json(plan);
  } catch (error) {
    return res.status(500).json({ error: `unreadable warm-up plan: ${error.message}` });
  }
});

app.get("/api/geometry-bundle", async (req, res) => {
  try {
    const pattern = String(req.query.pattern || "");
    const start = Number.parseInt(req.query.start, 10);
    const count = Math.max(1, Math.min(600, Number.parseInt(req.query.count, 10)));
    if (!pattern.startsWith("/files/") || !pattern.includes("%04d")
        || !Number.isFinite(start) || !Number.isFinite(count)) {
      return res.status(400).json({ error: "invalid geometry bundle request" });
    }
    const rootPrefix = path.resolve(FILES_ROOT) + path.sep;
    const files = [];
    for (let index = 0; index < count; index++) {
      const frame = String(start + index).padStart(4, "0");
      const relative = pattern.replace("%04d", frame).substring("/files/".length);
      const filename = path.resolve(FILES_ROOT, relative);
      if (!filename.startsWith(rootPrefix))
        return res.status(400).json({ error: "geometry path escapes files root" });
      const stat = fs.statSync(filename);
      if (!stat.isFile() || stat.size > 0xffffffff)
        return res.status(404).json({ error: `invalid geometry frame ${frame}` });
      files.push({ filename, size: stat.size });
    }
    const headerBytes = 12 + files.length * 4;
    const contentLength = headerBytes + files.reduce((sum, file) => sum + file.size, 0);
    res.set({
      "Content-Type": "application/x-vs4d-geometry-bundle",
      "Content-Length": String(contentLength),
      "Cache-Control": "no-store, no-transform",
    });
    const header = Buffer.alloc(headerBytes);
    header.write("V4DB", 0, "ascii");
    header.writeUInt32LE(1, 4);
    header.writeUInt32LE(files.length, 8);
    files.forEach((file, index) => header.writeUInt32LE(file.size, 12 + index * 4));
    res.write(header);
    for (const file of files) {
      if (res.destroyed) return;
      await new Promise((resolve, reject) => {
        const input = fs.createReadStream(file.filename);
        input.once("error", reject);
        input.once("end", resolve);
        input.pipe(res, { end: false });
      });
    }
    if (!res.destroyed) res.end();
  } catch (error) {
    if (!res.headersSent) res.status(404).json({ error: error.message });
    else res.destroy(error);
  }
});

const PORT = process.env.PORT || 3000;
const HOST = process.env.HOST || "127.0.0.1";

function numberFromEnv(name, fallback) {
  const value = Number(process.env[name]);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function integerFromEnv(name, fallback) {
  const value = Number.parseInt(process.env[name], 10);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function booleanSetting(value, fallback, name) {
  if (value === undefined || value === null || value === "") return fallback;
  if (typeof value === "boolean") return value;
  const normalized = String(value).trim().toLowerCase();
  if (["1", "true", "yes", "on"].includes(normalized)) return true;
  if (["0", "false", "no", "off"].includes(normalized)) return false;
  throw new Error(`${name} must be true or false`);
}

// Must match vstream/config.py FRAMES_PER_SEG / FPS.
const SEGMENT_DURATION = numberFromEnv("VS4D_SEGMENT_DURATION", 2.0);
const FRAMES_PER_SEGMENT = integerFromEnv("VS4D_FRAMES_PER_SEGMENT", 60);
const SEGMENT_INTERVAL = integerFromEnv(
  "VS4D_SEGMENT_INTERVAL_MS",
  Math.round(SEGMENT_DURATION * 1000)
);
// The default user-study window is 20 seconds at two seconds per segment.
const TOTAL_SEGMENTS = integerFromEnv("VS4D_TOTAL_SEGMENTS", 10);
const UPDATE_INTERVAL_SEGMENTS = integerFromEnv("VS4D_UPDATE_INTERVAL_SEGMENTS", 1);
const STREAM_CONFIG = {
  segmentDuration: SEGMENT_DURATION,
  framesPerSegment: FRAMES_PER_SEGMENT,
  segmentIntervalMs: SEGMENT_INTERVAL,
  totalSegments: TOTAL_SEGMENTS,
  updateIntervalSegments: UPDATE_INTERVAL_SEGMENTS,
};

// Quest selects its pipeline from this server-owned launch profile whenever A
// is pressed. The file is read per request so switching experiments does not
// require an APK rebuild, adb, or even a Node restart. Environment variables
// take precedence for scripted runs.
const QUEST_LAUNCH_OVERRIDE = String(process.env.VS4D_QUEST_LAUNCH || "").trim();
const QUEST_LAUNCH_FILE = QUEST_LAUNCH_OVERRIDE
  ? path.resolve(QUEST_LAUNCH_OVERRIDE)
  : path.join(__dirname, "quest-launch.json");
// Vega and NeVo are explicit 30-frame offline-only paths. They are deliberately
// absent from scripts/user_study.py's condition set.
const QUEST_PIPELINES = new Set(["mesh", "metastream", "deltastream", "vivo", "nava", "livo", "vega", "nevo"]);

function questLaunchProfile(requestHost) {
  let file = {};
  if (QUEST_LAUNCH_OVERRIDE && !fs.existsSync(QUEST_LAUNCH_FILE)) {
    throw new Error(
      `VS4D_QUEST_LAUNCH does not exist: ${QUEST_LAUNCH_FILE}`);
  }
  if (fs.existsSync(QUEST_LAUNCH_FILE)) {
    file = JSON.parse(fs.readFileSync(QUEST_LAUNCH_FILE, "utf8"));
    if (!file || Array.isArray(file) || typeof file !== "object")
      throw new Error("quest launch profile must be a JSON object");
  }
  const pipeline = String(process.env.VS4D_QUEST_PIPELINE || file.pipeline || "mesh")
    .trim().toLowerCase();
  if (!QUEST_PIPELINES.has(pipeline))
    throw new Error(`unsupported Quest pipeline '${pipeline}'`);
  const baselinePort = Number.parseInt(
    process.env.VS4D_BASELINE_PORT ?? file.baselinePort ?? 12345, 10);
  if (!Number.isInteger(baselinePort) || baselinePort <= 0 || baselinePort > 65535)
    throw new Error("baselinePort must be an integer from 1 through 65535");
  const abr = Number(process.env.VS4D_BASELINE_ABR_BANDWIDTH_MBPS
    ?? file.baselineAbrBandwidthMbps ?? 100);
  if (!Number.isFinite(abr) || abr <= 0)
    throw new Error("baselineAbrBandwidthMbps must be positive");
  const connectionTimeout = Number(file.baselineConnectionTimeoutSeconds ?? 10);
  if (!Number.isFinite(connectionTimeout) || connectionTimeout <= 0)
    throw new Error("baselineConnectionTimeoutSeconds must be positive");
  const splatSize = Number(file.pointSplatSize ?? 0.006);
  if (!Number.isFinite(splatSize) || splatSize <= 0)
    throw new Error("pointSplatSize must be positive");
  const receiveBytes = Number.parseInt(
    file.maxBaselineReceiveBufferBytes ?? 268435456, 10);
  if (!Number.isInteger(receiveBytes) || receiveBytes < 1024 * 1024)
    throw new Error("maxBaselineReceiveBufferBytes must be at least 1048576");
  const fullBandwidthMode = booleanSetting(
    process.env.VS4D_FULL_BANDWIDTH_MODE ?? file.fullBandwidthMode,
    true, "fullBandwidthMode");
  const groundTruthBandwidthMode = booleanSetting(
    process.env.VS4D_GROUND_TRUTH_BANDWIDTH_MODE
      ?? file.groundTruthBandwidthMode,
    false, "groundTruthBandwidthMode");
  if (groundTruthBandwidthMode && pipeline !== "mesh")
    throw new Error("groundTruthBandwidthMode is only supported by pipeline=mesh");
  const ablationVariant = String(file.ablationVariant || "").trim();
  if (ablationVariant && pipeline !== "mesh")
    throw new Error("ablationVariant is only supported by pipeline=mesh");
  const geometryTextureAdaptationEnabled = booleanSetting(
    file.geometryTextureAdaptationEnabled, true,
    "geometryTextureAdaptationEnabled");
  const globalAllocationEnabled = booleanSetting(
    file.globalAllocationEnabled, true, "globalAllocationEnabled");
  const objectSchedulingEnabled = booleanSetting(
    file.objectSchedulingEnabled, true, "objectSchedulingEnabled");
  const fastSwitchingEnabled = booleanSetting(
    file.fastSwitchingEnabled, true, "fastSwitchingEnabled");
  const frameBufferEnabled = booleanSetting(
    file.frameBufferEnabled, true, "frameBufferEnabled");
  const viewportPredictionEnabled = booleanSetting(
    file.viewportPredictionEnabled, true, "viewportPredictionEnabled");
  const viewportTraceEnabled = booleanSetting(
    file.viewportTraceEnabled, true, "viewportTraceEnabled");
  const orbitViewCullingEnabled = booleanSetting(
    file.orbitViewCullingEnabled, true, "orbitViewCullingEnabled");
  const offlineBenchmarkMode = booleanSetting(
    file.offlineBenchmarkMode, false, "offlineBenchmarkMode");
  const visualBaselineStreamingMode = booleanSetting(
    file.visualBaselineStreamingMode, false, "visualBaselineStreamingMode");
  if (visualBaselineStreamingMode && pipeline !== "vega" && pipeline !== "nevo")
    throw new Error("visualBaselineStreamingMode requires pipeline=vega or pipeline=nevo");
  if ((pipeline === "vega" || pipeline === "nevo") && !offlineBenchmarkMode)
    throw new Error(`the ${pipeline} Quest pipeline is restricted to offlineBenchmarkMode=true`);
  const sceneObjects = Array.isArray(file.sceneObjects)
    ? file.sceneObjects.map(value => String(value).trim()).filter(Boolean) : [];
  if (new Set(sceneObjects).size !== sceneObjects.length)
    throw new Error("sceneObjects must not contain duplicates");
  const viewpointNumber = (name, fallback, allowZero = false) => {
    const value = Number(file[name] ?? fallback);
    if (!Number.isFinite(value) || (allowZero ? value < 0 : value <= 0))
      throw new Error(`${name} must be ${allowZero ? "non-negative" : "positive"}`);
    return value;
  };
  return {
    pipeline,
    // The host from the Quest's HTTP request is the Node machine address it
    // can actually reach, making an explicit baselineHost optional.
    baselineHost: String(process.env.VS4D_BASELINE_HOST
      || file.baselineHost || requestHost || "").trim(),
    baselinePort,
    baselineDatasetManifest: String(file.baselineDatasetManifest || ""),
    baselineTileCatalog: String(file.baselineTileCatalog || ""),
    baselineConnectionTimeoutSeconds: connectionTimeout,
    baselineAbrBandwidthMbps: abr,
    pointSplatSize: splatSize,
    maxBaselineReceiveBufferBytes: receiveBytes,
    fullBandwidthMode,
    groundTruthBandwidthMode,
    ablationVariant,
    geometryTextureAdaptationEnabled,
    globalAllocationEnabled,
    objectSchedulingEnabled,
    fastSwitchingEnabled,
    frameBufferEnabled,
    viewportPredictionEnabled,
    viewportHistoryWindowSec: viewpointNumber("viewportHistoryWindowSec", 0.5),
    viewportPredictionWindowSec: viewpointNumber(
      "viewportPredictionWindowSec", 0.5, true),
    viewportSampleRateHz: viewpointNumber("viewportSampleRateHz", 36),
    viewportTraceEnabled,
    orbitViewCullingEnabled,
    orbitCullMaxDistanceMetres: viewpointNumber("orbitCullMaxDistanceMetres", 20),
    orbitCullFovMarginDegrees: viewpointNumber(
      "orbitCullFovMarginDegrees", 5, true),
    scene: String(file.scene || "").trim(),
    sceneObjects,
    skybox: String(file.skybox || "").trim(),
    studyTrial: String(file.studyTrial || "").trim(),
    studyMethod: String(file.studyMethod || "").trim(),
    participantId: String(file.participantId || "").trim(),
    studyTrialIndex: Number(file.studyTrialIndex || 0),
    studyTotalTrials: Number(file.studyTotalTrials || 0),
    studyProtocolVersion: String(file.studyProtocolVersion || "").trim(),
    plannedDurationSeconds: Number(file.plannedDurationSeconds || 0),
    studyReady: booleanSetting(file.studyReady, true, "studyReady"),
    offlineBenchmarkMode,
    visualBaselineStreamingMode,
    offlineTrajectoryBroadcastId: String(
      file.offlineTrajectoryBroadcastId || "").trim(),
    source: fs.existsSync(QUEST_LAUNCH_FILE)
      ? path.basename(QUEST_LAUNCH_FILE) : "server defaults",
  };
}

function getManifestPath(segId) {
  return path.resolve(
    path.join(FILES_ROOT, `t_${String(segId).padStart(2, "0")}`),
    "menu.json"
  );
}
function getStatePath(broadcastId) {
  const id = broadcastId || "default";
  const dir = path.join(STATE_DIR);
  ensureDir(dir);
  return dir; // directory, not a file
}


function ensureManifestDir(segId) {
  const outPath = getManifestPath(segId);
  const dir = path.dirname(outPath);
  if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
  return outPath;
}


const SERVER_VIEWPOINTS_DIR = path.resolve(__dirname, "server_viewpoints");
const SERVER_SELECTIONS_DIR = path.resolve(__dirname, "server_selections");
const SERVER_RESULTS_DIR = path.resolve(__dirname, "server_results"); // NEW
const VIEWPORT_PLOT_SCRIPT = path.join(
  REPO_ROOT, "system", "QuestClient", "Tools", "plot_viewport_traces.py"
);
const STATE_DIR = FILES_ROOT;

// Run-scoped evaluation plots and CSVs are intentionally separate from the
// media tree. This mount makes the generated PNGs viewable without exposing
// arbitrary host paths returned by the debugging API.
app.use("/server-results", express.static(SERVER_RESULTS_DIR));

// All per-run artifacts (metrics, bitrate counts, QoE, download telemetry)
// live in server_results/<broadcastId>/ so each run stays self-contained.
function broadcastResultsDir(broadcastId) {
  const id = broadcastId || currentBroadcastId || "no-broadcast";
  const dir = path.join(SERVER_RESULTS_DIR, id);
  ensureDir(dir);
  return dir;
}
function broadcastArtifactDir(broadcastId, name) {
  const dir = path.join(broadcastResultsDir(broadcastId), name);
  ensureDir(dir);
  return dir;
}
function intervalsFilePath(broadcastId) {
  return path.join(broadcastResultsDir(broadcastId), "bitrate_intervals.jsonl");
}

ensureDir(STATE_DIR);

const PYTHON_BIN = process.env.PYTHON_BIN || "python";
// Importing matplotlib and redrawing three figures is intentionally kept out
// of the request path, but doing it for every two-second Quest trace upload can
// still keep a CPU busy continuously alongside the ladder process. Ten seconds
// keeps the dashboard live without making evaluation compete with streaming.
const VIEWPORT_PLOT_INTERVAL_MS = integerFromEnv(
  "VS4D_VIEWPORT_PLOT_INTERVAL_MS", 10000
);

// -------------------- State --------------------
const broadcasts = new Map();
const groundTruthBandwidthByBroadcast = new Map();
const selectionLog = [];
const viewportPlotJobs = new Map();

// Keep a startup/default menu available as before. Starting a broadcast resets
// this to -1, and that broadcast's first viewpoint force-regenerates segment 0
// before the client fetches it, so stale/uniform weights are never consumed by
// an active run.
let latestManifestSegId = fs.existsSync(getManifestPath(0)) ? 0 : -1;
let ladderBusy = false;
let pendingSegId = null;
let manifestGeneration = 0;
const manifestOwners = new Map();

// NEW: Track current broadcast for results naming
let currentBroadcastId = null;
let currentTraceStartTime = null;
let currentScene = "";
let currentSceneObjects = [];
let currentSkybox = "";
let lastQuestLaunchLogKey = "";

// Latest bandwidth estimate reported by the client (Mbps); forwarded to the
// ladder service as the cap on expected served bitrate (C_cap).
let latestClientBandwidthMbps = null;

// -------------------- Ensure dirs --------------------
function ensureDir(p) {
  if (!fs.existsSync(p)) fs.mkdirSync(p, { recursive: true });
}
ensureDir(SERVER_VIEWPOINTS_DIR);
ensureDir(SERVER_SELECTIONS_DIR);
ensureDir(SERVER_RESULTS_DIR); // NEW
ensureDir(path.dirname(getManifestPath(0)));

function viewportTracePath(broadcastId) {
  return path.join(broadcastResultsDir(broadcastId), "viewport_trace.csv");
}

function viewportEvaluationDir(broadcastId) {
  return path.join(broadcastResultsDir(broadcastId), "viewport_evaluation");
}

// Regenerate at most one plot set per broadcast at a time. Uploads arriving
// during a render mark it dirty and cause exactly one follow-up render.
function scheduleViewportPlots(broadcastId) {
  let state = viewportPlotJobs.get(broadcastId);
  if (!state) {
    state = {
      running: false, dirty: false, timer: null, lastError: null, lastStartedAt: 0,
    };
    viewportPlotJobs.set(broadcastId, state);
  }
  state.dirty = true;
  if (state.running || state.timer) return;
  const earliestStart = state.lastStartedAt + VIEWPORT_PLOT_INTERVAL_MS;
  const delay = Math.max(750, earliestStart - Date.now());
  state.timer = setTimeout(() => {
    state.timer = null;
    state.running = true;
    state.dirty = false;
    state.lastError = null;
    state.lastStartedAt = Date.now();
    const trace = viewportTracePath(broadcastId);
    const output = viewportEvaluationDir(broadcastId);
    ensureDir(output);
    const summary = path.join(output, "viewport_metrics.json");
    const child = spawn(PYTHON_BIN, [
      VIEWPORT_PLOT_SCRIPT,
      trace,
      "--out", output,
      "--streaming-only",
      "--summary-json", summary,
    ], {
      cwd: REPO_ROOT,
      env: { ...process.env, MPLCONFIGDIR: path.join(output, ".matplotlib") },
    });
    let diagnostics = "";
    const collect = chunk => {
      diagnostics = (diagnostics + chunk.toString()).slice(-8000);
    };
    child.stdout.on("data", collect);
    child.stderr.on("data", collect);
    child.on("error", error => {
      state.lastError = error.message;
      logWarn("VIEWPORT", `Plot launch failed for ${broadcastId}: ${error.message}`);
    });
    child.on("close", code => {
      state.running = false;
      if (code === 0) {
        logInfo("VIEWPORT", `Evaluation plots updated: /viewport-evaluation/${broadcastId}`);
      } else {
        state.lastError = diagnostics.trim() || `plotter exited ${code}`;
        logWarn("VIEWPORT", `Plot update failed for ${broadcastId}: ${state.lastError}`);
      }
      if (state.dirty) scheduleViewportPlots(broadcastId);
    });
  }, delay);
}

// -------------------- Manifest helpers --------------------
function atomicWriteJson(outPath, obj) {
  const tmp = outPath + ".tmp";
  fs.writeFileSync(tmp, JSON.stringify(obj, null, 2));
  fs.renameSync(tmp, outPath);
}

function loadManifest(segId = latestManifestSegId) {
  const mp = getManifestPath(segId);
  if (!fs.existsSync(mp)) return null;
  const content = fs.readFileSync(mp, "utf-8");
  return JSON.parse(content);
}
/////////////////////

let ladderProc = null;
let rl = null;
let pending = [];

function startLadderService() {
  if (ladderProc) return;

  ladderProc = spawn(PYTHON_BIN, ["-m", "vstream.ladder.ladder_service"], {
    cwd: REPO_ROOT,
    env: process.env,
    stdio: ["pipe", "pipe", "pipe"],
  });

  rl = readline.createInterface({ input: ladderProc.stdout });

  rl.on("line", (line) => {
    const s = line.trim();

    // Ignore non-JSON lines (Python logs)
    if (!s.startsWith("{")) {
      logInfo("LADDER-PY", s);
      return;
    }

    const item = pending.shift();
    if (!item) return;

    try {
      const resp = JSON.parse(s);
      item.resolve(resp);
    } catch (e) {
      item.reject(new Error(`Bad JSON from ladder service: ${e.message}\nLine: ${line}`));
    }
  });


  logInfo("LADDER", `Started ladder service: ${PYTHON_BIN} -m vstream.ladder.ladder_service (pid ${ladderProc.pid})`);

  ladderProc.stderr.on("data", (d) => {
    // keep it for debugging
    const text = d.toString().trimEnd();
    if (text) logWarn("LADDER-PY", text);
  });

  ladderProc.on("exit", (code) => {
    logError("LADDER", `Ladder service exited with code ${code}; ${pending.length} pending request(s) failed`);
    ladderProc = null;
    if (rl) rl.close();
    rl = null;
    // fail all pending requests
    while (pending.length) pending.shift().reject(new Error("ladder_service died"));
  });
}

// -------------------- Python runner --------------------
// viewSegId names the viewpoint file that must weight this solve. It cannot be
// derived from segId: the per-segment path below solves segId+1 from the pose it
// just wrote at segId, while the viewpoint bootstrap solves segId from the pose
// at segId. The service used to guess `segment_{segId}.json`, which - because
// SERVER_VIEWPOINTS_DIR is shared by every broadcast and never cleared - always
// resolved to a leftover file from an earlier run. Pass null when no client pose
// exists yet (the startup default manifest) and the ladder weights uniformly.
function runLadderPython(segId, broadcastId, viewSegId, sceneObjects) {
  startLadderService();

  return new Promise((resolve, reject) => {
    const lastSegId = Math.max(0, latestManifestSegId);
    logInfo("LADDER", `Requesting ladder: seg=${segId} last_seg=${lastSegId} `
      + `view_seg=${viewSegId ?? "none"} broadcast=${broadcastId || "default"}`);

    // Object catalogs (mesh/texture patterns, start frames) are NOT sent:
    // the ladder service defaults to vstream/config.py, the single source
    // of truth for the active object set.
    const req = {
      current_seg_id: segId,
      last_seg_id: lastSegId,
      view_dir: SERVER_VIEWPOINTS_DIR,
      requests_path: intervalsFilePath(broadcastId),
      state_path: getStatePath(broadcastId),
      view_seg_id: Number.isFinite(viewSegId) ? viewSegId : null,
      files_root: FILES_ROOT, // optional
      // client's bandwidth estimate caps the ladder's expected served bitrate
      bandwidth_mbps: latestClientBandwidthMbps,
      objects: Array.isArray(sceneObjects) && sceneObjects.length
        ? sceneObjects : undefined,
    };

    pending.push({ resolve, reject });

    ladderProc.stdin.write(JSON.stringify(req) + "\n");
  });
}


// -------------------- Ladder update queue --------------------
let pendingUpdate = null;
let ladderDrainPromise = null;

function requestLadderUpdate(segId, broadcastId, reason = "", force = false,
                             viewSegId = null) {
  // viewSegId travels with the update so that coalescing keeps the pose and the
  // segment consistent: collapsing to the newest segId also keeps that request's
  // pose, which is the freshest one on disk.
  const update = {
    segId, broadcastId, reason, force, viewSegId,
    generation: manifestGeneration,
    sceneObjects: [...currentSceneObjects],
  };
  // Coalesce ordinary updates to the newest segment. A forced viewpoint
  // bootstrap wins because it establishes the state for a new broadcast.
  if (!pendingUpdate || force || (!pendingUpdate.force && segId >= pendingUpdate.segId)) {
    pendingUpdate = update;
    pendingSegId = segId;
  }
  if (!ladderDrainPromise) {
    ladderDrainPromise = drainLadderUpdates().finally(() => { ladderDrainPromise = null; });
  }
  return ladderDrainPromise;
}

async function drainLadderUpdates() {
  ladderBusy = true;
  try {
    while (pendingUpdate !== null) {
      const update = pendingUpdate;
      const seg = update.segId;
      pendingUpdate = null;
      pendingSegId = null;

      // A faster later run may already cover an ordinary update. The first
      // viewpoint of a broadcast deliberately replaces segment 0 on disk.
      if (!update.force && seg <= latestManifestSegId) continue;

      try {
        logInfo("LADDER", `Generating ladder for seg=${seg}${update.reason ? ` (${update.reason})` : ""}`);

        const resp = await runLadderPython(
          seg, update.broadcastId, update.viewSegId, update.sceneObjects);

        if (resp.status === "error") {
          throw new Error(resp.error + (resp.traceback ? `\n${resp.traceback}` : ""));
        }

        // Python should return the path to the generated manifest/menu.json
        const manifestPath = resp.mpd_path || resp.manifestPath || resp.manifest_path;
        if (!manifestPath) {
          throw new Error(`ladder service returned no mpd_path. resp=${JSON.stringify(resp)}`);
        }

        // (optional) sanity check file exists
        if (!fs.existsSync(manifestPath)) {
          throw new Error(`Manifest not found at ${manifestPath}`);
        }

        // A solve from the preceding user-study trial may finish after the next
        // broadcast has reset the manifest clock. Its Python output can remain
        // on disk for diagnostics/cache reuse, but must never become eligible
        // as this broadcast's temporal revision.
        if (update.generation !== manifestGeneration) {
          logWarn("LADDER", `Discarded stale-broadcast manifest seg=${seg}`);
          continue;
        }

        latestManifestSegId = seg;
        manifestOwners.set(seg, update.broadcastId || null);
        if (update.broadcastId && broadcasts.has(update.broadcastId)) {
          fs.appendFileSync(
            path.join(broadcastResultsDir(update.broadcastId), "manifest_events.jsonl"),
            JSON.stringify({
              event: "published", segmentId: seg, reason: update.reason || null,
              elapsedSeconds: resp.elapsed_sec ?? null,
              timingsMs: resp.timings_ms ?? null,
              publishedAt: new Date().toISOString(),
            }) + "\n"
          );
        }
        logInfo(
          "LADDER",
          `Manifest ready: seg=${seg} path=${manifestPath}` +
            (resp.elapsed_sec != null ? ` elapsed=${resp.elapsed_sec.toFixed(3)}s` : "")
        );
        if (resp.elapsed_sec != null && resp.elapsed_sec > SEGMENT_DURATION) {
          logWarn("LADDER", `Ladder generation slower than one segment: ${resp.elapsed_sec.toFixed(3)}s > ${SEGMENT_DURATION}s`);
        }

        // (optional) if you want to warm-load it / validate JSON:
        // const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf-8"));

      } catch (err) {
        logError("LADDER", `Ladder generation failed for seg=${seg}: ${err.message}`);
        if (update.broadcastId && broadcasts.has(update.broadcastId)) {
          fs.appendFileSync(
            path.join(broadcastResultsDir(update.broadcastId), "manifest_events.jsonl"),
            JSON.stringify({
              event: "failed", segmentId: seg, reason: update.reason || null,
              error: err.message, failedAt: new Date().toISOString(),
            }) + "\n"
          );
        }
      }
    }
  } finally {
    ladderBusy = false;
  }
}


// -------------------- Routes --------------------

app.get("/api/manifest", async (req, res) => {
  const segQ = req.query.seg;
  const seg =
    segQ !== undefined && segQ !== null && segQ !== ""
      ? parseInt(segQ, 10)
      : latestManifestSegId;

  if (!Number.isInteger(seg) || seg < 0)
    return res.status(400).json({ error: "invalid manifest segment", seg: segQ });

  // A numbered request is a temporal-content contract, not a request for the
  // latest quality menu. If the normal one-segment-ahead update is still being
  // solved, join that drain; if it was lost/failed, demand the exact revision.
  // Returning the latest revision here made logical segment N silently replay
  // an older source segment whenever ladder generation exceeded two seconds.
  // Files from a previous broadcast remain on disk for diagnostics. Only a
  // revision published in the current broadcast (tracked by the resettable
  // latestManifestSegId) is eligible for delivery.
  const broadcastId = String(req.query.broadcastId || "");
  const ownedByRequest = segQ === undefined || !broadcastId
    || manifestOwners.get(seg) === broadcastId;
  let manifest = seg <= latestManifestSegId && ownedByRequest ? loadManifest(seg) : null;
  if (!manifest && segQ !== undefined && broadcasts.has(broadcastId)) {
    try {
      await requestLadderUpdate(seg, broadcastId, "exact manifest demand", false,
        Math.max(0, seg - 1));
    } catch (error) {
      logError("MANIFEST", `Exact manifest generation failed for seg=${seg}: ${error.message}`);
    }
    manifest = seg <= latestManifestSegId && manifestOwners.get(seg) === broadcastId
      ? loadManifest(seg) : null;
  }
  if (!manifest) {
    logWarn("MANIFEST", `Manifest not found for seg=${seg}`);
    return res.status(503).json({ error: "Manifest not ready", seg });
  }

  const actual = Number(manifest?.segment?.t);
  if (segQ !== undefined && actual !== seg) {
    logError("MANIFEST", `Refusing mismatched manifest: requested=${seg} actual=${actual}`);
    return res.status(409).json({ error: "Manifest temporal mismatch", requested: seg, actual });
  }

  if (broadcastId && broadcasts.has(broadcastId)) {
    const archive = path.join(
      broadcastArtifactDir(broadcastId, "manifests"),
      `segment_${String(seg).padStart(4, "0")}.json`
    );
    if (!fs.existsSync(archive)) atomicWriteJson(archive, manifest);
  }

  logInfo("MANIFEST", `Sent manifest seg=${seg} objects=${Object.keys(manifest.objects || {}).length}`);
  res.json(manifest);
});

// Viewpoint index for the browser client's simulated mode.
//
// Interactive clients send their own live pose, but a simulated run replays
// canned poses and the browser has no filesystem to read them from. These are
// the same Open3D PinholeCameraParameters files the desktop client loads from
// system/Client/viewpoints, served as one document so the page needs a single
// request. `?dir=` selects a sibling set (the directory holds several).
// Is something listening? Used to tell "the corpus exists" from "a server is
// actually up", which need opposite responses from whoever is reading the
// chooser. Short timeout: this runs inline in a request handler.
function portIsOpen(port, host = "127.0.0.1", timeoutMs = 250) {
  return new Promise(resolve => {
    const socket = new net.Socket();
    const done = (value) => {
      socket.destroy();
      resolve(value);
    };
    socket.setTimeout(timeoutMs);
    socket.once("connect", () => done(true));
    socket.once("timeout", () => done(false));
    socket.once("error", () => done(false));
    socket.connect(port, host);
  });
}

// MetaStream, DeltaStream and LiVo are deliberately absent from the demo.
// They index the real capture rig while serving (`obj.cameras[camera_index]`)
// and read the source RGB-D frames, so the prepared tiles cannot substitute
// and the absent corpus is missing data rather than missing metadata. Listing
// them as permanently unavailable was noise in a chooser whose job is to say
// what you can look at.
//
// The point-cloud baselines that can run from the prepared tiles, and the
// ports scripts/serve_pointcloud_baseline.sh gives each by default. Both can
// be up at once, which is the point: switching baseline is then a click.
const POINTCLOUD_BASELINES = (process.env.VS4D_POINTCLOUD_PORTS
  || "vivo:8790:12345,nava:8791:12346").split(",").map(entry => {
    const [id, bridgePort, serverPort] = entry.split(":");
    return { id, bridgePort: Number(bridgePort), serverPort: Number(serverPort) };
  });

// The shaped link rate, read from tc.
//
// Without this the demo cannot show what it exists to show. Adaptation is a
// response to a changing link, and on an unshaped LAN all three adaptive
// systems sit on one operating point forever -- NAVA held quality level 5 for
// thirteen consecutive segments here. Reporting the rate the kernel is
// actually enforcing, beside each client's own estimate, is what makes a
// representation switch legible as cause and effect rather than noise.
//
// Read-only and unprivileged: `tc qdisc show` needs no root, only installing
// rules does.
const SHAPED_INTERFACE = process.env.VS4D_SHAPED_INTERFACE || "eth0";

// Start (or restart) a point-cloud baseline with a chosen object set.
//
// A baseline fixes its scene at startup -- `--objects` on the Python server --
// so unlike every other system here, choosing objects means replacing the
// process. That is why this exists rather than a query parameter.
//
// This endpoint spawns processes and the server listens on 0.0.0.0, so the
// input is constrained hard rather than trusted:
//   * the baseline id must be one of the two configured ones, never a module
//     name from the request;
//   * every object name must appear in the tile catalogue, so a name cannot
//     reach the command line unless the corpus already contains it;
//   * the child is spawned with an argv array and no shell, so nothing in the
//     request is interpretable as syntax.
const runningPointcloud = new Map();   // id -> objects[] currently served
const pointcloudChildren = new Map();  // id -> ChildProcess

function stopPointcloud(id) {
  const child = pointcloudChildren.get(id);
  if (!child) return;
  pointcloudChildren.delete(id);
  runningPointcloud.delete(id);
  // The supervisor script traps TERM and takes its bridge and server with it.
  try { process.kill(-child.pid, "SIGTERM"); }
  catch (_) { try { child.kill("SIGTERM"); } catch (_) { /* already gone */ } }
}

app.post("/api/pointcloud/start", async (req, res) => {
  const id = String(req.body?.id || "");
  const entry = POINTCLOUD_BASELINES.find(value => value.id === id);
  if (!entry) {
    return res.status(400).json({ error: `unknown baseline ${JSON.stringify(id)}` });
  }
  const catalogPath = path.join(VIVO_TILES_ROOT, "catalog.json");
  let known;
  try {
    known = new Set((JSON.parse(fs.readFileSync(catalogPath, "utf8")).objects || [])
      .map(value => String(value.name)));
  } catch (error) {
    return res.status(409).json({
      error: `no tile catalogue at ${catalogPath}: ${error.message}` });
  }
  const requested = Array.isArray(req.body?.objects)
    ? req.body.objects.map(value => String(value).trim()).filter(Boolean) : [];
  if (!requested.length) {
    return res.status(400).json({ error: "choose at least one object" });
  }
  const unknown = requested.filter(name => !known.has(name));
  if (unknown.length) {
    return res.status(400).json({
      error: `not in the tile catalogue: ${unknown.join(", ")}` });
  }

  stopPointcloud(id);
  const script = path.join(REPO_ROOT, "scripts/serve_pointcloud_baseline.sh");
  const child = spawn("bash", [script, id, requested.join(",")], {
    cwd: REPO_ROOT,
    detached: true,                      // its own group, so one kill stops all
    stdio: ["ignore", "pipe", "pipe"],
    env: { ...process.env, PYTHON_BIN: PYTHON_BIN }
  });
  child.stdout.on("data", chunk => logInfo(id.toUpperCase(), String(chunk).trim()));
  child.stderr.on("data", chunk => logInfo(id.toUpperCase(), String(chunk).trim()));
  child.on("exit", code => {
    if (pointcloudChildren.get(id) === child) {
      pointcloudChildren.delete(id);
      runningPointcloud.delete(id);
    }
    logInfo(id.toUpperCase(), `supervisor exited (${code})`);
  });
  pointcloudChildren.set(id, child);
  runningPointcloud.set(id, requested);

  // Report only once it is actually accepting connections; the page navigates
  // on this response, and arriving before the bridge is up reads as a failure.
  const deadline = Date.now() + 40000;
  while (Date.now() < deadline) {
    if (!pointcloudChildren.has(id)) {
      return res.status(500).json({ error: `${id} supervisor exited during startup` });
    }
    // BOTH ports, not just the bridge. The bridge listens as soon as the
    // supervisor starts it and stays up even when the baseline behind it has
    // crashed and is being restarted in a loop, so probing it alone reports
    // "serving" over a dead baseline -- observed when the spawned process had
    // the wrong interpreter.
    if (await portIsOpen(entry.bridgePort)
        && await portIsOpen(entry.serverPort)) {
      logInfo(id.toUpperCase(), `serving ${requested.join(", ")}`);
      return res.json({
        id, objects: requested, bridgePort: entry.bridgePort,
        bridge: `ws://${req.hostname}:${entry.bridgePort}`
      });
    }
    await new Promise(resolve => setTimeout(resolve, 500));
  }
  const bridgeUp = await portIsOpen(entry.bridgePort);
  stopPointcloud(id);
  res.status(504).json({
    error: bridgeUp
      // Naming the port that failed matters: a live bridge with a dead
      // baseline means the Python side could not start (wrong interpreter,
      // unreadable corpus), which is a different fix from a dead bridge.
      ? `${id}: the bridge is up on ${entry.bridgePort} but the baseline `
        + `server never listened on ${entry.serverPort} — check the corpus and `
        + "PYTHON_BIN in the server log"
      : `${id} did not start listening on ${entry.bridgePort} within 40s` });
});

// Leaving orphaned Python servers and bridges behind would hold their ports
// and silently serve a stale scene to the next run.
for (const signal of ["SIGINT", "SIGTERM"]) {
  process.on(signal, () => {
    for (const id of [...pointcloudChildren.keys()]) stopPointcloud(id);
    process.exit(0);
  });
}

app.get("/api/shaping", (req, res) => {
  execFile("tc", ["qdisc", "show", "dev", SHAPED_INTERFACE],
    { timeout: 2000 }, (error, stdout) => {
      res.set("Cache-Control", "no-store");
      if (error) {
        return res.json({
          interface: SHAPED_INTERFACE, shaped: false,
          detail: `could not read tc on ${SHAPED_INTERFACE}: ${error.message}`
        });
      }
      // The trace player installs a root TBF; anything else means unshaped.
      const root = stdout.split(/\n(?=qdisc )/)
        .find(block => /^qdisc tbf \S+ root\b/.test(block.trim()));
      if (!root) {
        return res.json({
          interface: SHAPED_INTERFACE, shaped: false,
          detail: "no root TBF installed — the link is unshaped, so the "
            + "adaptive systems will hold one operating point. Install a trace "
            + "with scripts/shape_web_demo.sh"
        });
      }
      // `rate 12500Kbit`, `rate 1Gbit`, `rate 950Mbit`
      const match = /\brate (\d+(?:\.\d+)?)([KMG])?bit\b/.exec(root);
      const unit = { K: 1e-3, M: 1, G: 1e3 };
      const mbps = match
        ? Number(match[1]) * (unit[match[2]] ?? 1e-6)
        : null;
      res.json({
        interface: SHAPED_INTERFACE, shaped: true,
        rateMbps: mbps === null ? null : Number(mbps.toFixed(2)),
        detail: root.trim().split("\n")[0]
      });
    });
});

// What the comparison pages can actually run right now.
//
// Every baseline needs assets or a process that may simply not be there: Vega
// needs an export, NeVo needs pre-rendered frames, the point-cloud baselines
// need a Python server plus a WebSocket bridge. Without this the pages fail at
// fetch time with a 404, which reads as a bug in the viewer rather than as
// missing input. The chooser asks here first and says which is which.
//
// The mesh entry also carries the per-object ladder cost, because the scene
// size sets an irreducible bitrate floor (the ladder must publish at least one
// representation per object) and that floor, not the ABR, is what decides
// whether a link can carry the scene.
app.get("/api/systems", async (req, res) => {
  const dirHasAny = (dir, predicate) => {
    try { return fs.readdirSync(dir).some(predicate); } catch (_) { return false; }
  };

  // The catalog comes from the packaged media tree, not from the last
  // manifest. A manifest is a run artifact: after a run restricted to three
  // objects it names only those three, and a picker built from it could then
  // never offer the other six back. Costs still come from the manifest, so an
  // object outside the current one is selectable with its cost reported as
  // unknown rather than as zero.
  const manifest = loadManifest();
  const published = manifest?.objects || {};
  const packaged = (() => {
    try {
      return fs.readdirSync(path.join(FILES_ROOT, "media"), { withFileTypes: true })
        .filter(entry => entry.isDirectory()).map(entry => entry.name);
    } catch (_) { return []; }
  })();
  const names = [...new Set([...packaged, ...Object.keys(published)])].sort();

  const objects = names.map(name => {
    const entry = published[name];
    const reps = Array.isArray(entry?.representations) ? entry.representations : [];
    const rates = reps
      .map(rep => Number(rep?.predicted?.bitrate_mbps))
      .filter(Number.isFinite);
    return {
      name,
      inManifest: Boolean(entry),
      weight: entry ? Number(entry.weight) || 0 : null,
      representations: reps.length,
      floorMbps: rates.length ? Math.min(...rates) : null,
      ceilingMbps: rates.length ? Math.max(...rates) : null
    };
  });
  const priced = objects.filter(o => o.floorMbps !== null);
  const floorMbps = priced.reduce((sum, o) => sum + o.floorMbps, 0);

  // Vega's own catalogue, read for the same reason: so the chooser can offer
  // its objects instead of the page taking all of them by default.
  const vega = (() => {
    try {
      const catalog = JSON.parse(fs.readFileSync(
        path.join(VEGA_ASSETS_ROOT, "catalog.json"), "utf8"));
      const entries = (catalog.objects || []).map(entry => ({
        name: String(entry.name),
        bytes: (entry.frames || []).reduce(
          (sum, frame) => sum + (Number(frame.exportBytes) || 0), 0),
        points: Number(entry.frames?.[0]?.points) || 0
      }));
      return {
        objects: entries,
        bytes: entries.reduce((sum, entry) => sum + entry.bytes, 0)
      };
    } catch (_) { return { objects: [], bytes: 0 }; }
  })();

  // One entry per point-cloud baseline, each probed independently. Both can
  // be up at once on different ports, so choosing between ViVo and NAVA is a
  // click rather than a server restart.
  const haveTiles = fs.existsSync(path.join(VIVO_TILES_ROOT, "catalog.json"));
  // The tile catalogue's object list, so the chooser can offer a selection.
  // Unlike the other systems this is not a URL parameter: a baseline fixes its
  // object set at startup, so choosing one means restarting the process.
  const tileObjects = (() => {
    if (!haveTiles) return [];
    try {
      const catalog = JSON.parse(fs.readFileSync(
        path.join(VIVO_TILES_ROOT, "catalog.json"), "utf8"));
      return (catalog.objects || []).map(entry => ({
        name: String(entry.name),
        points: Math.max(0, ...(entry.sequence_tiles || [])
          .map(tile => Number(tile.max_point_count) || 0))
      }));
    } catch (_) { return []; }
  })();
  const NAMES = { vivo: "ViVo", nava: "NAVA" };
  const pointcloud = await Promise.all(POINTCLOUD_BASELINES.map(async entry => {
    const live = haveTiles && await portIsOpen(entry.bridgePort);
    return {
      id: entry.id,
      name: NAMES[entry.id] || entry.id,
      page: "/web/baseline.html",
      bridge: `ws://${req.hostname}:${entry.bridgePort}`,
      // These do adapt, but the decision is the Python server's, driven by the
      // client's pose and goodput feedback -- not the browser's. Conflating
      // that with our own client-side ABR would misrepresent the comparison.
      adaptive: false,
      adaptsServerSide: true,
      ready: live,
      objects: tileObjects,
      // What the running process was started with, so the chooser can tell a
      // restart is needed rather than silently connecting to the wrong scene.
      servingObjects: runningPointcloud.get(entry.id) || null,
      restartable: haveTiles,
      detail: !haveTiles
        ? `no tile corpus at ${VIVO_TILES_ROOT} (prepare it with `
          + "baselines.ViVo.orbitvivo.prepare)"
        : live
          ? `serving on port ${entry.bridgePort}`
          : `nothing listening on ${entry.bridgePort} — start it with `
            + `scripts/serve_pointcloud_baseline.sh ${entry.id}`
    };
  }));

  res.set("Cache-Control", "no-store");
  res.json({
    schemaVersion: 1,
    systems: [
      {
        id: "mesh",
        name: "Ours",
        page: "/web/",
        adaptive: true,
        ready: objects.length > 0,
        detail: objects.length
          ? `${objects.length} objects · ladder floor ${floorMbps.toFixed(0)} Mbps`
          : "no packaged media and no manifest — nothing to stream yet",
        objects,
        sceneFloorMbps: priced.length ? floorMbps : null
      },
      ...pointcloud,
      {
        id: "vega",
        name: "Vega",
        page: "/web/vega.html",
        adaptive: false,
        ready: vega.objects.length > 0,
        // Vega's page preloads whole clips, so per-object size is the number
        // that decides what is worth opening: the full nine-object export is
        // ~345 MB, which is over a minute of loading on a normal link.
        detail: vega.objects.length
          ? `${vega.objects.length} objects · ${(vega.bytes / 1e6).toFixed(0)} MB `
            + "total, preloaded per clip"
          : `no export at ${VEGA_ASSETS_ROOT} `
            + "(produce it with baselines.Vega.orbitvega.export_quest)",
        objects: vega.objects
      },
      {
        id: "nevo",
        name: "NeVo",
        page: "/web/nevo.html",
        adaptive: false,
        ready: dirHasAny(NEVO_ASSETS_ROOT, f => f.startsWith("g_")),
        detail: dirHasAny(NEVO_ASSETS_ROOT, f => f.startsWith("g_"))
          ? "pre-rendered comparison panels"
          : `no renders at ${NEVO_ASSETS_ROOT} `
            + "(produce them with orbitnevo/render_frames.py)"
      }
    ]
  });
});

app.get("/api/viewpoint-index", (req, res) => {
  const requested = String(req.query.dir || "");
  if (requested && !/^[A-Za-z0-9._-]+$/.test(requested)) {
    return res.status(400).json({ error: "invalid viewpoint directory" });
  }
  const root = path.join(SYSTEM_ROOT_CLIENT, "viewpoints");
  const directory = requested ? path.join(root, requested) : root;
  if (!path.resolve(directory).startsWith(path.resolve(root))) {
    return res.status(400).json({ error: "viewpoint path escapes the client dir" });
  }
  if (!fs.existsSync(directory)) {
    return res.status(404).json({ error: `no viewpoint directory ${directory}` });
  }
  try {
    const files = fs.readdirSync(directory)
      .filter(f => f.startsWith("view_") && f.endsWith(".json"))
      .sort();
    if (files.length === 0) {
      return res.status(404).json({ error: "no view_*.json files found" });
    }
    const viewpoints = files.map(filename => ({
      filename,
      data: JSON.parse(fs.readFileSync(path.join(directory, filename), "utf8"))
    }));
    res.set("Cache-Control", "no-store");
    res.json({ schemaVersion: 1, count: viewpoints.length, viewpoints });
  } catch (error) {
    res.status(409).json({ error: `could not read viewpoints: ${error.message}` });
  }
});

app.get("/api/config", (req, res) => {
  res.json(STREAM_CONFIG);
});

app.get("/api/quest-launch", (req, res) => {
  try {
    const profile = questLaunchProfile(req.hostname);
    const logKey = JSON.stringify(profile);
    if (logKey !== lastQuestLaunchLogKey) {
      lastQuestLaunchLogKey = logKey;
      logInfo("QUEST", `Launch profile: pipeline=${profile.pipeline}`
        + (profile.pipeline === "mesh" ? "" : ` mediaPort=${profile.baselinePort}`)
        + ` prediction=${profile.viewportPredictionEnabled}`
        + ` culling=${profile.orbitViewCullingEnabled}`
        + ` scene=${profile.scene || "default"}`
        + ` objects=${profile.sceneObjects.length}`
        + ` trial=${profile.studyTrial || "manual"}`
        + ` method=${profile.studyMethod || "manual"}`
        + ` ablation=${profile.ablationVariant || "none"}`
        + ` ready=${profile.studyReady}`
        + ` source=${profile.source}`);
    }
    res.set("Cache-Control", "no-store");
    res.json(profile);
  } catch (error) {
    logError("QUEST", `Invalid launch profile: ${error.message}`);
    res.status(500).json({ error: error.message });
  }
});

// Texture-decode knobs, read fresh on every request so editing the file takes
// effect on the next run with no server restart.
//
// These live here rather than only in the client because quest-client.json is a
// Unity Resource baked into the APK: changing it means a rebuild and a redeploy,
// and the headset is not the machine anyone is sitting at. The client applies
// whatever this returns just before it starts streaming, and logs what took
// effect. Omit a field, or delete the file, to leave the client's own value
// alone - note that 0 and -1 are meaningful values for these keys, so "absent"
// has to mean absent rather than zero.
const DECODE_TUNING_FILE =
  process.env.VS4D_DECODE_TUNING || path.join(__dirname, "decode-tuning.json");
// `benchmark` is a one-shot: non-zero makes the client sweep the decoder for
// capacity numbers and stop, instead of streaming. Left in the same file so a
// measurement run is selected the same way a tuning variant is.
const DECODE_TUNING_KEYS = [
  "maxVideoDecoders", "videoOperatingRate", "videoPriority", "benchmark",
];

app.get("/api/decode-tuning", (req, res) => {
  if (!fs.existsSync(DECODE_TUNING_FILE)) {
    logInfo("TUNING", `No ${path.basename(DECODE_TUNING_FILE)}; client keeps its own decode settings`);
    return res.json({});
  }
  try {
    const raw = JSON.parse(fs.readFileSync(DECODE_TUNING_FILE, "utf8"));
    // Whitelisted so a stray key cannot look like it was applied when the
    // client would have ignored it.
    const tuning = {};
    for (const key of DECODE_TUNING_KEYS) {
      if (Number.isInteger(raw[key])) tuning[key] = raw[key];
    }
    const ignored = Object.keys(raw).filter((k) => !(k in tuning));
    if (ignored.length > 0) {
      logWarn("TUNING", `Ignored non-integer or unknown keys: ${ignored.join(", ")}`);
    }
    logInfo("TUNING", `Sent decode tuning: ${JSON.stringify(tuning)}`);
    return res.json(tuning);
  } catch (error) {
    logError("TUNING", `Unreadable ${DECODE_TUNING_FILE}: ${error.message}`);
    // Not fatal: a typo here must not stop a run, it just means the client
    // keeps its baked settings.
    return res.json({});
  }
});

app.put("/api/manifest", (req, res) => {
  const segQ = req.query.seg;
  const seg =
    segQ !== undefined && segQ !== null && segQ !== ""
      ? parseInt(segQ, 10)
      : latestManifestSegId;

  const manifest = req.body;
  const mp = ensureManifestDir(seg);
  atomicWriteJson(mp, manifest);

  latestManifestSegId = seg;
  manifestOwners.set(seg, currentBroadcastId || null);

  logInfo("MANIFEST", `Manifest updated via PUT: seg=${seg} path=${mp}`);
  res.json({ status: "ok", message: "Manifest updated", seg, path: mp });
});

app.post("/api/manifest", (req, res) => {
  const segQ = req.query.seg;
  const seg =
    segQ !== undefined && segQ !== null && segQ !== ""
      ? parseInt(segQ, 10)
      : latestManifestSegId;

  const manifest = req.body;
  const mp = ensureManifestDir(seg);
  atomicWriteJson(mp, manifest);

  latestManifestSegId = seg;
  manifestOwners.set(seg, currentBroadcastId || null);

  logInfo("MANIFEST", `Manifest updated via POST: seg=${seg} path=${mp}`);
  res.json({ status: "ok", message: "Manifest updated", seg, path: mp });
});

// Human-readable broadcast id: <label-or-algorithm>_YYYYMMDD-HHMMSS[_n].
// The client may send { label, algorithm } in the body; label wins.
function makeBroadcastId(body) {
  const raw = (body && (body.label || body.algorithm)) || "broadcast";
  const name =
    String(raw).replace(/[^a-zA-Z0-9._-]+/g, "-").replace(/^-+|-+$/g, "") ||
    "broadcast";
  const d = new Date();
  const pad = (n) => String(n).padStart(2, "0");
  const stamp =
    `${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}` +
    `-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}`;
  let id = `${name}_${stamp}`;
  for (let n = 2; broadcasts.has(id); n++) id = `${name}_${stamp}_${n}`;
  return id;
}

app.post("/api/broadcast/start", (req, res) => {
  const requestedObjects = Array.isArray(req.body?.sceneObjects)
    ? req.body.sceneObjects.map(value => String(value).trim()).filter(Boolean) : [];
  if (new Set(requestedObjects).size !== requestedObjects.length)
    return res.status(400).json({ error: "sceneObjects must not contain duplicates" });
  const broadcastId = makeBroadcastId(req.body);
  currentScene = String(req.body?.scene || "").trim();
  currentSceneObjects = requestedObjects;
  currentSkybox = String(req.body?.skybox || "").trim();
  const studyTrial = String(req.body?.studyTrial || "").trim();
  const studyMethod = String(req.body?.studyMethod || "").trim();
  const participantId = String(req.body?.participantId || "").trim();
  if (participantId && !/^[a-zA-Z0-9._-]{1,64}$/.test(participantId))
    return res.status(400).json({ error: "participantId must be a pseudonymous label" });
  const studyTrialIndex = Number(req.body?.studyTrialIndex || 0);
  const studyTotalTrials = Number(req.body?.studyTotalTrials || 0);
  const plannedDurationSeconds = Number(req.body?.plannedDurationSeconds || 0);
  const studyProtocolVersion = String(req.body?.studyProtocolVersion || "").trim();
  const clientConfiguration = privacySafeObject(
    req.body?.clientConfiguration || {});
  if (studyProtocolVersion.startsWith("vs4d-mesh-ablation-")) {
    const variant = String(clientConfiguration.ablationVariant || "").trim();
    const policies = {
      full: [],
      "no-gt-adaptation": ["geometryTextureAdaptationEnabled"],
      "no-global-allocation": ["globalAllocationEnabled"],
      "no-object-scheduling": ["objectSchedulingEnabled"],
      "no-fast-switching": ["fastSwitchingEnabled"],
      "no-frame-buffer": ["frameBufferEnabled"],
    };
    const flags = [
      "geometryTextureAdaptationEnabled", "globalAllocationEnabled",
      "objectSchedulingEnabled", "fastSwitchingEnabled", "frameBufferEnabled",
    ];
    if (!Object.prototype.hasOwnProperty.call(policies, variant))
      return res.status(400).json({
        error: `ablation APK/config mismatch: unsupported variant '${variant}'`,
      });
    const disabled = new Set(policies[variant]);
    const mismatch = flags.filter(name =>
      typeof clientConfiguration[name] !== "boolean"
      || clientConfiguration[name] !== !disabled.has(name));
    if (mismatch.length)
      return res.status(400).json({
        error: "ablation APK/config mismatch; rebuild the Quest client: "
          + mismatch.join(", "),
      });
  }
  const groundTruthBandwidthMode = req.body?.pipeline === "mesh"
    && clientConfiguration.groundTruthBandwidthMode === true;
  const configuredSafetyFactor = Number(clientConfiguration.bandwidthSafetyFactor);
  const groundTruthBandwidthSafetyFactor = Number.isFinite(configuredSafetyFactor)
    ? Math.max(0.25, Math.min(1, configuredSafetyFactor)) : 1;
  broadcasts.set(broadcastId, {
    id: broadcastId, startTime: Date.now(), scene: currentScene,
    sceneObjects: [...currentSceneObjects], skybox: currentSkybox,
    studyTrial, studyMethod, participantId, studyTrialIndex, studyTotalTrials,
    groundTruthBandwidthMode, groundTruthBandwidthSafetyFactor,
  });

  // NEW: Track current broadcast for results
  currentBroadcastId = broadcastId;
  currentTraceStartTime = Date.now();
  // New trace = new network conditions; don't carry over the old estimate
  latestClientBandwidthMbps = null;
  manifestGeneration++;
  manifestOwners.clear();
  latestManifestSegId = -1;

  const runDir = broadcastResultsDir(broadcastId);
  fs.writeFileSync(
    path.join(runDir, "run.json"),
    JSON.stringify(
      {
        broadcastId,
        startedAt: new Date().toISOString(),
        label: req.body?.label || null,
        algorithm: req.body?.algorithm || null,
        pipeline: req.body?.pipeline || "mesh",
        baselinePort: req.body?.baselinePort || null,
        datasetManifest: req.body?.datasetManifest || null,
        scene: currentScene || null,
        sceneObjects: currentSceneObjects,
        skybox: currentSkybox || null,
        studyTrial: studyTrial || null,
        studyMethod: studyMethod || null,
        participantId: participantId || null,
        studyTrialIndex: Number.isInteger(studyTrialIndex) && studyTrialIndex > 0
          ? studyTrialIndex : null,
        studyTotalTrials: Number.isInteger(studyTotalTrials) && studyTotalTrials > 0
          ? studyTotalTrials : null,
        studyProtocolVersion: studyProtocolVersion || null,
        ablationVariant: clientConfiguration.ablationVariant || null,
        plannedDurationSeconds: Number.isFinite(plannedDurationSeconds)
          && plannedDurationSeconds > 0 ? plannedDurationSeconds : null,
        streamConfig: STREAM_CONFIG,
        serverRuntime: {
          nodeVersion: process.version,
          platform: process.platform,
          architecture: process.arch,
        },
        studyEnvironment: studySafeEnvironment(),
        client: privacySafeObject(req.body?.client || {}),
        clientConfiguration,
        privacy: {
          pseudonymousParticipantOnly: true,
          storesRawIpAddresses: false,
          storesControllerTrajectories: false,
          storesNamesOrBirthDates: false,
          headPoseStoredForSelectionReplay: true,
        },
      },
      null,
      2
    )
  );

  logInfo("BROADCAST", `Broadcast started: ${broadcastId}`
    + ` scene=${currentScene || "default"}`
    + ` objects=${currentSceneObjects.length}`
    + ` skybox=${currentSkybox || "client-default"}`);
  res.json({ broadcastId });
});

// The trace shaper is the authority for the currently installed TBF rate. In
// oracle experiments it publishes every rate change here; the Quest reads it
// immediately before an MCKP decision. Normal runs retain the value only as
// diagnostic provenance and continue using their causal estimator.
app.post("/api/ground-truth-bandwidth", (req, res) => {
  const broadcastId = String(req.body?.broadcastId || "").trim();
  const bandwidthMbps = Number(req.body?.bandwidthMbps);
  const elapsedSeconds = Number(req.body?.elapsedSeconds);
  if (!broadcastId || broadcastId !== currentBroadcastId
      || !broadcasts.has(broadcastId))
    return res.status(409).json({ error: "ground-truth bandwidth broadcast mismatch" });
  if (!Number.isFinite(bandwidthMbps) || bandwidthMbps <= 0
      || !Number.isFinite(elapsedSeconds) || elapsedSeconds < 0)
    return res.status(400).json({ error: "invalid ground-truth bandwidth sample" });
  const sample = {
    broadcastId, bandwidthMbps, elapsedSeconds,
    serverReceivedAt: new Date().toISOString(),
  };
  groundTruthBandwidthByBroadcast.set(broadcastId, sample);
  const broadcast = broadcasts.get(broadcastId);
  if (broadcast.groundTruthBandwidthMode)
    latestClientBandwidthMbps = bandwidthMbps
      * broadcast.groundTruthBandwidthSafetyFactor;
  fs.appendFileSync(
    path.join(broadcastResultsDir(broadcastId), "ground_truth_bandwidth.jsonl"),
    JSON.stringify(sample) + "\n");
  res.json({ status: "ok" });
});

app.get("/api/ground-truth-bandwidth/:broadcastId", (req, res) => {
  const broadcastId = String(req.params.broadcastId || "").trim();
  if (!broadcasts.has(broadcastId))
    return res.status(404).json({ error: "Broadcast not found" });
  const sample = groundTruthBandwidthByBroadcast.get(broadcastId);
  if (!sample)
    return res.status(503).json({ error: "ground-truth bandwidth is not available yet" });
  res.set("Cache-Control", "no-store");
  res.json(sample);
});

// Baseline media bypasses the ladder but keeps this HTTP control plane for
// provenance and telemetry. The TCP header supplies the calibration hash an
// exact replay must match.
app.post("/api/baseline/session", (req, res) => {
  const body = req.body || {};
  if (!broadcasts.has(body.broadcastId))
    return res.status(404).json({ error: "Broadcast not found" });
  const filepath = path.join(broadcastResultsDir(body.broadcastId), "baseline_session.json");
  const safeBody = privacySafeObject(body);
  fs.writeFileSync(filepath, JSON.stringify({
    ...safeBody,
    serverReceivedAt: new Date().toISOString(),
  }, null, 2));
  const runPath = path.join(broadcastResultsDir(body.broadcastId), "run.json");
  try {
    const run = JSON.parse(fs.readFileSync(runPath, "utf8"));
    run.baseline = safeBody;
    atomicWriteJson(runPath, run);
  } catch (error) {
    logWarn("BASELINE", `Could not merge session metadata into run.json: ${error.message}`);
  }
  logInfo("BASELINE", `Session ${body.broadcastId}: pipeline=${body.pipeline}`
    + ` streams=${body.streamCount} calibration=${body.calibrationHash}`);
  res.json({ status: "ok", saved: filepath });
});

app.post("/api/baseline/frames", (req, res) => {
  const { broadcastId, frames } = req.body || {};
  if (!broadcasts.has(broadcastId))
    return res.status(404).json({ error: "Broadcast not found" });
  if (!Array.isArray(frames) || frames.length > 1000)
    return res.status(400).json({ error: "expected a bounded frames array" });
  const filepath = path.join(broadcastResultsDir(broadcastId), "baseline_frames.jsonl");
  if (frames.length)
    fs.appendFileSync(filepath, frames.map(frame => JSON.stringify(frame)).join("\n") + "\n");
  res.json({ status: "ok", count: frames.length });
});

app.post("/api/viewpoint", async (req, res) => {
  const { broadcastId, viewpoint, segId } = req.body;

  if (!broadcasts.has(broadcastId)) {
    return res.status(404).json({ error: "Broadcast not found" });
  }

  const s = Number.isFinite(segId) ? segId : 0;
  const filename = `segment_${String(s).padStart(4, "0")}.json`;
  const filepath = path.join(SERVER_VIEWPOINTS_DIR, filename);

  fs.writeFileSync(filepath, JSON.stringify(viewpoint, null, 2));
  fs.writeFileSync(
    path.join(broadcastArtifactDir(broadcastId, "viewpoints"), filename),
    JSON.stringify(viewpoint, null, 2)
  );
  logInfo("VIEWPOINT", `Viewpoint received: seg=${s} file=${filename}`);

  // Always bootstrap this broadcast from its real segment-0 viewpoint. Await
  // completion so the client's immediately-following manifest GET cannot race
  // and consume a stale, uniformly weighted menu from an earlier/default run.
  if (latestManifestSegId < 0) {
    logInfo("LADDER", "Bootstrapping seg=0 ladder from the client viewpoint");
    // This path labels the solve with the same id as the pose it just wrote.
    await requestLadderUpdate(s, broadcastId, "viewpoint bootstrap", true, s);
  }
  return res.json({ status: "ok", segId: s, filename });
});

app.post("/api/segment/:id", async (req, res) => {
  const segId = parseInt(req.params.id, 10);
  const { broadcastId, viewpoint, selection } = req.body;
  if (!broadcasts.has(broadcastId)) {
    logWarn("SEGMENT", `Rejected seg=${segId}: unknown broadcast ${broadcastId}`);
    return res.status(404).json({ error: "Broadcast not found" });
  }

  // The ladder plans its published floor against what the client will actually
  // commit (vstream/config.py CLIENT_BUDGET_MULTIPLIER_STRUGGLING), so a client
  // that keeps its own headroom must report that budget or the floor lands
  // above what it will spend and it freezes objects. The JS client applies a
  // multiplier of 1 and reports only the raw estimate, so the fallback is
  // unchanged for it.
  const estBW = req.body.bandwidthBudget
    ?? req.body.estimatedBandwidth ?? selection?.estimatedBandwidth;
  if (typeof estBW === "number" && estBW > 0) {
    latestClientBandwidthMbps = estBW;
  }

  const received = [];
  if (viewpoint) {
    const filename = `segment_${String(segId).padStart(4, "0")}.json`;
    const viewpointFile = path.join(SERVER_VIEWPOINTS_DIR, filename);
    fs.writeFileSync(viewpointFile, JSON.stringify(viewpoint, null, 2));
    fs.writeFileSync(
      path.join(broadcastArtifactDir(broadcastId, "viewpoints"), filename),
      JSON.stringify(viewpoint, null, 2)
    );
    received.push("viewpoint");
  }

  if (selection) {
    selectionLog.push(selection);
    const selectionFile = path.join(
      SERVER_SELECTIONS_DIR,
      `selection_${String(segId).padStart(4, "0")}.json`
    );
    fs.writeFileSync(selectionFile, JSON.stringify(selection, null, 2));
    fs.writeFileSync(
      path.join(
        broadcastArtifactDir(broadcastId, "selections"),
        `selection_${String(segId).padStart(4, "0")}.json`
      ),
      JSON.stringify(selection, null, 2)
    );
    received.push("selection");
  }

  const stats = [];
  if (selection) {
    if (typeof selection.minBufferLevel === "number") stats.push(`buffer=${selection.minBufferLevel.toFixed(1)}s`);
    if (typeof selection.totalBitrate === "number") stats.push(`bitrate=${selection.totalBitrate.toFixed(1)}Mbps`);
    if (typeof selection.totalQuality === "number") stats.push(`quality=${selection.totalQuality.toFixed(1)}`);
    if (typeof selection.estimatedBandwidth === "number") stats.push(`estBW=${selection.estimatedBandwidth.toFixed(1)}Mbps`);
    if (typeof selection.segmentStallDurationSec === "number" && selection.segmentStallDurationSec > 0) {
      stats.push(`stall=${selection.segmentStallDurationSec.toFixed(2)}s`);
    }
    if (typeof selection.missingCount === "number" && selection.missingCount > 0) {
      stats.push(`missing=${selection.missingCount}[${(selection.missingObjects || []).join(",")}]`);
    }
    if (typeof selection.frozenCount === "number" && selection.frozenCount > 0) {
      stats.push(`frozen=${selection.frozenCount}[${(selection.frozenObjects || []).join(",")}]`);
    }
    if (selection.clientOverloadSkip) stats.push("client-overload-skip");
  }
  logInfo(
    "SEGMENT",
    `seg=${segId} received=[${received.join(",") || "none"}]${stats.length ? " " + stats.join(" ") : ""}`
  );
  // Solve for the segment the client will fetch NEXT, not the one it just
  // reported. A client asks for the manifest before it reports, so a solve
  // labelled with the reported id is always published too late for that
  // segment and gets picked up one segment later. Mid stream that was
  // invisible - every fetch still got a manifest one content step on - but
  // segment 0 and segment 1 both landed on t_00, because report(0) was
  // skipped entirely and there was nothing newer to fetch. The client played
  // the first segment's media twice before moving on.
  //
  // The pose is unaffected: last_seg_id tracks latestManifestSegId rather
  // than segId, and the viewpoint read is the one this report just wrote, so
  // a manifest is still solved one segment ahead of the frame it weights.
  // Only the content step it is labelled with changes, which is what makes
  // logical segment N line up with t_N.
  const needUpdate = Number.isFinite(segId) && segId >= 0
    && segId % UPDATE_INTERVAL_SEGMENTS === 0 && Boolean(viewpoint);
  // Solve segId+1, weighted by the pose written for segId just above - the one
  // this report carried, which is the client's predicted pose at its
  // viewportPredictionWindowSec lead.
  if (needUpdate) {
    requestLadderUpdate(segId + 1, broadcastId, "update", false, segId)
      .catch(()=>{});
  }

  // latestManifestSegId lets the client log manifest staleness (telemetry only)
  res.json({ segId, status: "ok", latestManifestSegId });
});

// Download-completion reports from the client (bandwidth/buffer telemetry).
// Appended to a JSONL so QoE analysis can correlate with selections.
app.post("/api/segment/:id/download-complete", (req, res) => {
  const segId = parseInt(req.params.id, 10);
  const record = {
    receivedAt: new Date().toISOString(),
    segId,
    ...req.body,
  };
  const downloadsFile = path.join(broadcastResultsDir(), "download_complete.jsonl");
  fs.appendFileSync(downloadsFile, JSON.stringify(record) + "\n");

  const parts = [];
  if (typeof req.body.downloadTimeMs === "number") parts.push(`time=${req.body.downloadTimeMs}ms`);
  if (typeof req.body.downloadSizeBytes === "number") parts.push(`size=${(req.body.downloadSizeBytes / 1024 / 1024).toFixed(2)}MB`);
  if (typeof req.body.measuredBandwidthMbps === "number") parts.push(`bw=${req.body.measuredBandwidthMbps.toFixed(1)}Mbps`);
  if (typeof req.body.prepareTimeMs === "number") parts.push(`prepare=${req.body.prepareTimeMs}ms`);
  if (typeof req.body.stagedObjects === "number" && typeof req.body.requestedObjects === "number") {
    parts.push(`staged=${req.body.stagedObjects}/${req.body.requestedObjects}`);
  }
  if (typeof req.body.geometryCacheHits === "number"
      && typeof req.body.geometryCacheRequested === "number") {
    const used = typeof req.body.geometryCacheBytes === "number"
      ? ` used=${(req.body.geometryCacheBytes / 1e9).toFixed(2)}GB` : "";
    parts.push(`geometry-cache=${req.body.geometryCacheHits}/${req.body.geometryCacheRequested}${used}`);
    if (Array.isArray(req.body.geometryCacheUncached)
        && req.body.geometryCacheUncached.length > 0)
      parts.push(`uncached=[${req.body.geometryCacheUncached.join(",")}]`);
  }
  if (req.body.isLate) parts.push("late");
  if (req.body.usedFallback) parts.push("fallback");
  logInfo("DOWNLOAD", `seg=${segId} complete${parts.length ? " " + parts.join(" ") : ""}`);

  res.json({ status: "ok", segId });
});

// Per-presented-frame truth from the interactive renderer. The client batches
// records to avoid a request per frame; JSONL keeps long runs streamable by the
// full-frame evaluator.
app.post("/api/render-frames", (req, res) => {
  const { broadcastId, frames } = req.body || {};
  if (!broadcasts.has(broadcastId)) {
    return res.status(404).json({ error: "Broadcast not found" });
  }
  if (!Array.isArray(frames) || frames.length === 0) {
    return res.status(400).json({ error: "frames must be a non-empty array" });
  }
  const renderFile = path.join(broadcastResultsDir(broadcastId), "render_frames.jsonl");
  fs.appendFileSync(renderFile, frames.map((frame) => JSON.stringify(frame)).join("\n") + "\n");
  const summary = summarizeRenderFrames(frames);
  if (summary.stuck) logWarn("RENDER", summary.line);
  else logInfo("RENDER", summary.line);
  res.json({ status: "ok", count: frames.length, saved: renderFile });
});

// Matured actual/predicted 6DoF pairs from the Quest. CSV is kept as the
// canonical artifact so the same offline plotter and server-side plotter score
// exactly the same samples.
app.post("/api/viewport-trace", (req, res) => {
  const { broadcastId, header, rows } = req.body || {};
  if (!broadcasts.has(broadcastId))
    return res.status(404).json({ error: "Broadcast not found" });
  if (typeof header !== "string" || !header.startsWith("t_s,")
      || !Array.isArray(rows) || rows.length === 0 || rows.length > 1000
      || rows.some(row => typeof row !== "string" || row.length > 4096
        || row.includes("\n") || row.includes("\r"))) {
    return res.status(400).json({ error: "invalid bounded viewport CSV batch" });
  }
  const filepath = viewportTracePath(broadcastId);
  if (!fs.existsSync(filepath)) {
    fs.writeFileSync(filepath, header + "\n");
  } else {
    const existingHeader = fs.readFileSync(filepath, "utf8").split(/\r?\n/, 1)[0];
    if (existingHeader !== header)
      return res.status(409).json({ error: "viewport trace header changed during run" });
  }
  fs.appendFileSync(filepath, rows.join("\n") + "\n");
  scheduleViewportPlots(broadcastId);
  logInfo("VIEWPORT", `Trace batch: run=${broadcastId} rows=${rows.length}`);
  res.json({
    status: "ok",
    count: rows.length,
    saved: filepath,
    evaluationUrl: `/viewport-evaluation/${broadcastId}`,
  });
});

function offlineTrajectorySamples(csvText, expectedBroadcastId = "") {
  const lines = String(csvText || "").trim().split(/\r?\n/);
  const header = (lines.shift() || "").split(",");
  const required = ["playback_s", "x", "y", "z", "yaw", "pitch", "roll"];
  const columns = Object.fromEntries(required.map(name => [name, header.indexOf(name)]));
  const broadcastColumn = header.indexOf("broadcast_id");
  if (required.some(name => columns[name] < 0)) {
    const error = new Error("trajectory predates offline playback-aligned pose logging");
    error.statusCode = 409;
    throw error;
  }

  // A stale upload batch from an earlier run used to be posted under the next
  // run's HTTP path. Keep the CSV as evidence, but select only rows whose own
  // embedded broadcast id matches the capture named by source.json.
  const epochs = [[]];
  let sampleCount = 0;
  for (const line of lines) {
    if (!line) continue;
    const values = line.split(",");
    if (expectedBroadcastId && broadcastColumn >= 0
        && values[broadcastColumn] !== expectedBroadcastId) continue;
    const raw = required.map(name => values[columns[name]]);
    // Number("") is zero in JavaScript. Pre-playback rows intentionally leave
    // playback_s blank, so reject blanks before numeric conversion.
    if (raw.some(value => value === undefined || value.trim() === "")) continue;
    const sample = raw.map(Number);
    if (!sample.every(Number.isFinite) || sample[0] < 0) continue;
    let epoch = epochs[epochs.length - 1];
    if (epoch.length && sample[0] < epoch[epoch.length - 1][0]) {
      epoch = [];
      epochs.push(epoch);
    }
    epoch.push(sample);
    if (++sampleCount > 10000) {
      const error = new Error("offline trajectory exceeds 10000 samples");
      error.statusCode = 413;
      throw error;
    }
  }
  // If playback restarted within one broadcast, use the epoch with the greatest
  // time coverage. Sorting all rows would splice physically unrelated paths.
  const samples = epochs.reduce((best, value) => {
    const duration = value.length > 1 ? value[value.length - 1][0] - value[0][0] : 0;
    const bestDuration = best.length > 1 ? best[best.length - 1][0] - best[0][0] : 0;
    return duration > bestDuration || (duration === bestDuration && value.length > best.length)
      ? value : best;
  }, []);
  if (samples.length < 2) {
    const error = new Error("offline trajectory has fewer than two playback samples");
    error.statusCode = 409;
    throw error;
  }
  return samples;
}

function offlineTrajectoryPayload(csvText, preferredBroadcastId = "") {
  try {
    return {
      sourceBroadcastId: preferredBroadcastId,
      samples: offlineTrajectorySamples(csvText, preferredBroadcastId),
    };
  } catch (error) {
    if (!preferredBroadcastId
        || error.message !== "offline trajectory has fewer than two playback samples") throw error;
    // Resume metadata from the first implementation used the placeholder
    // "archived". Recover the actual source id from the rows themselves and
    // choose the id with the most playback-aligned samples.
    const lines = String(csvText || "").trim().split(/\r?\n/);
    const header = (lines.shift() || "").split(",");
    const broadcastColumn = header.indexOf("broadcast_id");
    const playbackColumn = header.indexOf("playback_s");
    if (broadcastColumn < 0 || playbackColumn < 0) throw error;
    const counts = new Map();
    for (let lineIndex = 0; lineIndex < lines.length; lineIndex++) {
      const line = lines[lineIndex];
      const values = line.split(",");
      const id = values[broadcastColumn];
      const playback = values[playbackColumn];
      if (!id || !playback || !Number.isFinite(Number(playback))) continue;
      const previous = counts.get(id) || { count: 0, last: -1 };
      counts.set(id, { count: previous.count + 1, last: lineIndex });
    }
    const recovered = [...counts.entries()].sort(
      (left, right) => right[1].count - left[1].count || right[1].last - left[1].last
    )[0]?.[0];
    if (!recovered || recovered === preferredBroadcastId) throw error;
    return {
      sourceBroadcastId: recovered,
      samples: offlineTrajectorySamples(csvText, recovered),
    };
  }
}

function sceneRelativeTrajectoryPayload(value, preferredBroadcastId = "") {
  if (!value || value.schemaVersion !== 2
      || value.coordinateSpace !== "open3d-camera-extrinsic-column-major") {
    const error = new Error("unsupported scene-relative offline trajectory");
    error.statusCode = 409;
    throw error;
  }
  const samples = Array.isArray(value.samples) ? value.samples : [];
  if (samples.length < 2 || samples.length > 10000) {
    const error = new Error("scene-relative trajectory needs 2-10000 samples");
    error.statusCode = 409;
    throw error;
  }
  let previous = -Infinity;
  for (const sample of samples) {
    if (!Array.isArray(sample) || sample.length !== 17
        || !sample.every(Number.isFinite) || sample[0] < 0 || sample[0] < previous) {
      const error = new Error("invalid scene-relative trajectory sample");
      error.statusCode = 409;
      throw error;
    }
    previous = sample[0];
  }
  return {
    schemaVersion: 2,
    coordinateSpace: value.coordinateSpace,
    sourceBroadcastId: String(value.sourceBroadcastId || preferredBroadcastId),
    samples,
  };
}

// A fair offline comparison records one physical head trajectory and replays
// it for the remaining methods in the same scene/network block. Retained
// cross-session runs carry scene-relative Open3D matrices in trajectory.json;
// older in-session runs fall back to playback-aligned Unity poses in the CSV.
app.get("/api/offline-trajectory/:broadcastId", (req, res) => {
  const broadcastId = String(req.params.broadcastId || "");
  if (!/^[A-Za-z0-9._-]+$/.test(broadcastId))
    return res.status(400).json({ error: "invalid trajectory broadcast id" });
  const filepath = path.join(SERVER_RESULTS_DIR, broadcastId, "viewport_trace.csv");
  if (!fs.existsSync(filepath))
    return res.status(404).json({ error: "offline trajectory not found" });
  let sourceBroadcastId = broadcastId;
  const sourceFile = path.join(SERVER_RESULTS_DIR, broadcastId, "source.json");
  if (fs.existsSync(sourceFile)) {
    try {
      sourceBroadcastId = String(
        JSON.parse(fs.readFileSync(sourceFile, "utf8")).sourceBroadcastId || broadcastId);
    } catch (error) {
      return res.status(409).json({ error: `invalid trajectory source metadata: ${error.message}` });
    }
  }
  try {
    const sceneRelativeFile = path.join(
      SERVER_RESULTS_DIR, broadcastId, "trajectory.json");
    if (fs.existsSync(sceneRelativeFile)) {
      const payload = sceneRelativeTrajectoryPayload(
        JSON.parse(fs.readFileSync(sceneRelativeFile, "utf8")), sourceBroadcastId);
      res.set("Cache-Control", "no-store");
      return res.json(payload);
    }
    const payload = offlineTrajectoryPayload(
      fs.readFileSync(filepath, "utf8"), sourceBroadcastId);
    res.set("Cache-Control", "no-store");
    res.json({ schemaVersion: 1, ...payload });
  } catch (error) {
    res.status(error.statusCode || 409).json({ error: error.message });
  }
});

// Condenses one uploaded batch into the per-object playback truth.
//
// `State` alone is not enough. In the textured path the presented frame is the
// video decoder's last delivered frame, so an object keeps reporting "ok" while
// SourceFrame never moves - a freeze that is invisible to both the state field
// and the client's buffer accounting. Counting how often SourceFrame changes is
// what separates "playing" from "holding one frame".
function summarizeRenderFrames(frames) {
  const objects = new Map();
  for (const frame of frames) {
    const presented = frame.objects || frame.Objects || {};
    for (const [name, value] of Object.entries(presented)) {
      if (!objects.has(name)) {
        objects.set(name, {
          states: new Map(), advanced: 0, steps: 0, last: null, segment: null,
          playback: null, parts: new Set(),
        });
      }
      const entry = objects.get(name);
      const state = String(value?.State ?? value?.state ?? "unknown");
      const reason = value?.FreezeReason ?? value?.freezeReason ?? null;
      const key = state === "ok" ? "ok" : reason ? `${state}:${reason}` : state;
      entry.states.set(key, (entry.states.get(key) || 0) + 1);
      entry.segment = value?.MediaSegmentId ?? value?.mediaSegmentId ?? entry.segment;
      const playback = value?.PlaybackFrame ?? value?.playbackFrame ?? null;
      if (playback !== null && playback !== undefined) entry.playback = playback;
      const part = value?.Part ?? value?.part ?? null;
      if (part !== null && part !== undefined && state !== "ok") entry.parts.add(part);
      const source = value?.AbsoluteSourceFrame ?? value?.absoluteSourceFrame
        ?? value?.SourceFrame ?? value?.sourceFrame ?? null;
      if (source === null || source === undefined) continue;
      if (entry.last !== null) {
        entry.steps++;
        if (source !== entry.last) entry.advanced++;
      }
      entry.last = source;
    }
  }
  const trouble = [];
  let healthy = 0;
  for (const [name, entry] of objects) {
    const allOk = entry.states.size === 1 && entry.states.has("ok");
    const advancing = entry.steps === 0 || entry.advanced > 0;
    if (allOk && advancing) {
      healthy++;
      continue;
    }
    const states = [...entry.states.entries()]
      .sort((a, b) => b[1] - a[1])
      .map(([key, count]) => `${key}=${count}`)
      .join(",");
    // `part` is what a stalled object is waiting on, and it is derived from the
    // playback clock rather than the delivered frame. Reading the stall location
    // off SourceFrame instead points at the wrong temporal part entirely.
    const parts = [...entry.parts].sort().join("/");
    trouble.push(
      `${name}[${states} advanced=${entry.advanced}/${entry.steps}` +
        ` seg=${entry.segment} frame=${entry.last}` +
        (entry.playback === null ? "" : ` clock=${entry.playback}`) +
        (parts ? ` part=${parts}` : "") +
        `]`
    );
  }
  const line =
    `frames=${frames.length} playing=${healthy}/${objects.size}` +
    (trouble.length ? ` ${trouble.join(" ")}` : "");
  return { line, stuck: trouble.length > 0 };
}

// The headset has one USB-C port and it carries ethernet during a run, so
// `adb logcat` is unavailable while streaming. The client relays its own Unity
// log here instead; this is the only view of renderer-side diagnostics while a
// run is live. Deliberately does not require a known broadcast: startup and
// warm-up failures happen before one exists.
app.post("/api/client-log", (req, res) => {
  const { broadcastId, entries, lost } = req.body || {};
  if (!Array.isArray(entries) || entries.length === 0) {
    return res.status(400).json({ error: "entries must be a non-empty array" });
  }
  const safeEntries = entries.map(entry => ({
    ...entry,
    message: redactIpAddresses(entry.message),
    stack: entry.stack ? redactIpAddresses(entry.stack) : entry.stack,
  }));
  const logFile = path.join(broadcastResultsDir(broadcastId), "client_log.jsonl");
  fs.appendFileSync(logFile, safeEntries.map((entry) => JSON.stringify(entry)).join("\n") + "\n");
  for (const entry of safeEntries) {
    const level = String(entry.level || "INFO").toUpperCase();
    const message = String(entry.message || "").replace(/\s+/g, " ").trim();
    const stack = entry.stack ? ` | ${String(entry.stack).replace(/\s+/g, " ").trim()}` : "";
    log(level === "ERROR" || level === "WARN" ? level : "INFO", "CLIENT", message + stack);
  }
  if (lost > 0) logWarn("CLIENT", `${lost} client log entries were dropped before upload`);
  res.json({ status: "ok", count: safeEntries.length });
});

// ==================== NEW ENDPOINTS ====================

// NEW: Receive 10-second interval bitrate counts per object
app.post("/api/bitrate-counts-interval", (req, res) => {
  if (!req.body || typeof req.body !== "object") {
    return res.status(400).json({ error: "expected JSON body" });
  }
  const { segmentId, timestamp, intervalMs, countsPerObject } = req.body;
  
  const data = {
    receivedAt: new Date().toISOString(),
    broadcastId: currentBroadcastId,
    segmentId,
    timestamp,
    intervalMs,
    countsPerObject
  };
  // Append to JSONL file (one JSON object per line)
  const intervalsFile = intervalsFilePath();
  fs.appendFileSync(intervalsFile, JSON.stringify(data) + "\n");

  // Log summary
  const objectCount = Object.keys(countsPerObject || {}).length;
  const totalSelections = Object.values(countsPerObject || {}).reduce(
    (sum, obj) => sum + Object.values(obj).reduce((s, c) => s + c, 0), 0
  );

  logInfo(
    "BITRATE",
    `Interval received: objects=${objectCount} selections=${totalSelections} t=${(timestamp / 1000).toFixed(1)}s`
  );
  
  res.json({ status: "ok", saved: intervalsFile });
});

// NEW: Receive final bitrate counts per object (end of trace)
app.post("/api/bitrate-counts-final", (req, res) => {
  if (!req.body || typeof req.body !== "object") {
    logWarn("BITRATE", "Rejected final counts: body is not JSON (missing Content-Type: application/json?)");
    return res.status(400).json({ error: "expected JSON body" });
  }
  const { totalSegments, bitrateRequestCounts, bitrateRequestCountsPerObject } = req.body;

  const filepath = path.join(broadcastResultsDir(), "bitrate_final.json");
  
  const data = {
    savedAt: new Date().toISOString(),
    broadcastId: currentBroadcastId,
    totalSegments,
    bitrateRequestCounts,
    bitrateRequestCountsPerObject
  };
  
  fs.writeFileSync(filepath, JSON.stringify(data, null, 2));
  
  // Log per-object summary
  logInfo("BITRATE", `Final counts received: totalSegments=${totalSegments} saved=${filepath}`);
  if (bitrateRequestCountsPerObject) {
    for (const [objName, counts] of Object.entries(bitrateRequestCountsPerObject)) {
      const sorted = Object.entries(counts).sort((a, b) => b[1] - a[1]);
      const top3 = sorted.slice(0, 3).map(([rep, cnt]) => `${rep}:${cnt}`).join(", ");
      logInfo("BITRATE", `  ${objName}: ${top3}`);
    }
  }
  
  res.json({ status: "ok", saved: filepath });
});

// NEW: Receive complete metrics JSON (end of trace)
app.post("/api/results", (req, res) => {
  const metrics = req.body;
  if (!metrics || typeof metrics !== "object") {
    logWarn("RESULTS", "Rejected metrics upload: body is not JSON (missing Content-Type: application/json?)");
    return res.status(400).json({ error: "expected JSON body" });
  }

  const resultBroadcastId =
    broadcasts.has(metrics.broadcastId) ? metrics.broadcastId : currentBroadcastId;
  const filepath = path.join(broadcastResultsDir(resultBroadcastId), "metrics.json");
  
  // Add server-side metadata
  metrics.serverReceivedAt = new Date().toISOString();
  metrics.serverBroadcastId = resultBroadcastId;
  try {
    metrics.runMetadata = JSON.parse(fs.readFileSync(
      path.join(broadcastResultsDir(resultBroadcastId), "run.json"), "utf8"));
  } catch (_) { /* Older/non-broadcast uploads have no run metadata. */ }
  
  fs.writeFileSync(filepath, JSON.stringify(metrics, null, 2));
  
  // Log summary
  const summary = metrics.summary || {};
  logInfo(
    "RESULTS",
    `Metrics received: segments=${summary.totalSegments || "N/A"} ` +
      `rebuffers=${summary.rebuffers || 0} ` +
      `stall=${((summary.totalStallDuration || 0) / 1000).toFixed(2)}s saved=${filepath}`
  );
  
  res.json({ status: "ok", saved: filepath });
});

const QUESTIONNAIRE_RATINGS = [
  "visualQuality", "geometryDepthFidelity", "temporalSmoothness",
  "overallQualityOfExperience",
];
const QUESTIONNAIRE_ARTIFACTS = new Set([
  "surface_geometry_degradation", "missing_parts", "quality_flicker", "freezing",
  "mixed_quality_parts", "client_low_fps", "view_dependent_degradation", "none",
]);
const QUESTIONNAIRE_MAX_ARTIFACTS = 2;

app.post("/api/questionnaire", (req, res) => {
  const body = req.body || {};
  const broadcastId = String(body.broadcastId || "");
  const broadcast = broadcasts.get(broadcastId);
  if (!broadcast)
    return res.status(404).json({ error: "Broadcast not found" });
  const ratings = body.ratings;
  if (!ratings || typeof ratings !== "object")
    return res.status(400).json({ error: "ratings are required" });
  for (const key of QUESTIONNAIRE_RATINGS) {
    if (!Number.isInteger(ratings[key]) || ratings[key] < 1 || ratings[key] > 5)
      return res.status(400).json({ error: `${key} must be an integer from 1 through 5` });
  }
  if (!Array.isArray(body.artifacts) || body.artifacts.length < 1)
    return res.status(400).json({ error: "select artifacts or none" });
  const artifacts = [...new Set(body.artifacts.map(value => String(value)))];
  const invalid = artifacts.filter(value => !QUESTIONNAIRE_ARTIFACTS.has(value));
  if (invalid.length)
    return res.status(400).json({ error: `unknown artifact(s): ${invalid.join(", ")}` });
  if (artifacts.includes("none") && artifacts.length !== 1)
    return res.status(400).json({ error: "none cannot be combined with artifacts" });
  if (!artifacts.includes("none") && artifacts.length > QUESTIONNAIRE_MAX_ARTIFACTS)
    return res.status(400).json({
      error: `select at most ${QUESTIONNAIRE_MAX_ARTIFACTS} most severe artifacts`,
    });

  const payload = {
    broadcastId,
    participantId: broadcast.participantId || null,
    trialIndex: broadcast.studyTrialIndex || null,
    totalTrials: broadcast.studyTotalTrials || null,
    studyTrial: broadcast.studyTrial || null,
    method: broadcast.studyMethod || null,
    scene: broadcast.scene || null,
    ratings: Object.fromEntries(QUESTIONNAIRE_RATINGS.map(key => [key, ratings[key]])),
    artifacts,
    artifactSelectionLimit: QUESTIONNAIRE_MAX_ARTIFACTS,
    responseTiming: privacySafeObject(body.responseTiming || {}),
    clientSubmittedAt: body.clientSubmittedAt || null,
    serverReceivedAt: new Date().toISOString(),
  };
  const filepath = path.join(broadcastResultsDir(broadcastId), "questionnaire.json");
  atomicWriteJson(filepath, payload);
  logInfo("QUESTIONNAIRE", `Saved ${broadcastId}: method=${payload.method || "-"}`
    + ` scene=${payload.scene || "-"} ratings=`
    + QUESTIONNAIRE_RATINGS.map(key => `${key}=${payload.ratings[key]}`).join(",")
    + ` artifacts=${artifacts.join(",")}`);
  res.json({ status: "ok", saved: filepath });
});

// NEW: Receive QoE results (from shell script)
// The QoE report is plain text (calculate-qoe.js output), uploaded as the raw
// body with trace/algorithm passed as query params.
app.post("/api/qoe", express.text({ type: "*/*", limit: "10mb" }), (req, res) => {
  if (typeof req.body !== "string" || req.body.length === 0) {
    logWarn("QOE", "Rejected QoE upload: empty or non-text body");
    return res.status(400).json({ error: "expected non-empty text body" });
  }

  const sanitize = (s) => String(s).replace(/[^a-zA-Z0-9._-]+/g, "-");
  const parts = ["qoe"];
  if (req.query.trace) parts.push(sanitize(req.query.trace));
  if (req.query.algorithm) parts.push(sanitize(req.query.algorithm));
  const filepath = path.join(broadcastResultsDir(), `${parts.join("_")}.txt`);

  fs.writeFileSync(filepath, req.body);

  logInfo("QOE", `QoE results saved: ${filepath}`);

  res.json({ status: "ok", saved: filepath });
});

// NEW: Get all results (for debugging/dashboard)
// Results live one level down, in server_results/<broadcastId>/; loose files
// in the root (from older runs) are still listed with broadcast=null.
app.get("/api/results", (req, res) => {
  const files = [];
  for (const entry of fs.readdirSync(SERVER_RESULTS_DIR, { withFileTypes: true })) {
    const entryPath = path.join(SERVER_RESULTS_DIR, entry.name);
    if (entry.isDirectory()) {
      for (const f of fs.readdirSync(entryPath)) {
        const p = path.join(entryPath, f);
        const st = fs.statSync(p);
        if (st.isFile()) {
          files.push({ broadcast: entry.name, filename: f, path: p, size: st.size, modified: st.mtime });
        }
      }
    } else if (entry.isFile()) {
      const st = fs.statSync(entryPath);
      files.push({ broadcast: null, filename: entry.name, path: entryPath, size: st.size, modified: st.mtime });
    }
  }
  files.sort((a, b) => b.modified - a.modified);

  res.json({
    resultsDir: SERVER_RESULTS_DIR,
    fileCount: files.length,
    files
  });
});

function validViewportBroadcastId(value) {
  return typeof value === "string" && /^[a-zA-Z0-9._-]+$/.test(value)
    && fs.existsSync(path.join(SERVER_RESULTS_DIR, value));
}

function latestViewportBroadcastId() {
  if (validViewportBroadcastId(currentBroadcastId)) return currentBroadcastId;
  return fs.readdirSync(SERVER_RESULTS_DIR, { withFileTypes: true })
    .filter(entry => entry.isDirectory())
    .map(entry => ({
      id: entry.name,
      path: path.join(SERVER_RESULTS_DIR, entry.name, "viewport_trace.csv"),
    }))
    .filter(entry => fs.existsSync(entry.path))
    .sort((a, b) => fs.statSync(b.path).mtimeMs - fs.statSync(a.path).mtimeMs)[0]?.id || null;
}

function viewportEvaluationData(broadcastId) {
  const output = viewportEvaluationDir(broadcastId);
  const trace = viewportTracePath(broadcastId);
  const metricPath = path.join(output, "viewport_metrics.json");
  let metrics = null;
  try { metrics = JSON.parse(fs.readFileSync(metricPath, "utf8")); }
  catch (_) { /* The first plot may still be rendering. */ }
  const plotNames = [
    "viewport_tracking.png", "viewport_error_cdf.png", "viewport_trajectory.png",
  ];
  const plots = Object.fromEntries(plotNames.map(name => [name, {
    ready: fs.existsSync(path.join(output, name)),
    url: `/server-results/${encodeURIComponent(broadcastId)}`
      + `/viewport_evaluation/${name}`,
  }]));
  const job = viewportPlotJobs.get(broadcastId);
  return {
    broadcastId,
    traceReady: fs.existsSync(trace),
    traceBytes: fs.existsSync(trace) ? fs.statSync(trace).size : 0,
    plotting: Boolean(job?.running || job?.timer),
    plotError: job?.lastError || null,
    metrics,
    plots,
  };
}

app.get("/api/viewport-evaluation/:broadcastId", (req, res) => {
  if (!validViewportBroadcastId(req.params.broadcastId))
    return res.status(404).json({ error: "unknown broadcast" });
  res.json(viewportEvaluationData(req.params.broadcastId));
});

function renderViewportEvaluation(req, res) {
  const broadcastId = req.params.broadcastId || latestViewportBroadcastId();
  if (!validViewportBroadcastId(broadcastId)) {
    return res.status(404).type("html").send(
      "<h1>No viewport evaluation yet</h1><p>Start a Quest mesh run and wait for the first trace batch.</p>"
    );
  }
  const data = viewportEvaluationData(broadcastId);
  const metric = value => Number.isFinite(value) ? value.toFixed(4) : "—";
  const featureRows = Object.entries(data.metrics?.features || {}).map(([name, value]) =>
    `<tr><td>${name}</td><td>${value.unit}</td><td>${value.n}</td>`
    + `<td>${metric(value.mae)}</td>`
    + `<td>${metric(value.rmse)}</td>`
    + `<td>${metric(value.p95)}</td></tr>`
  ).join("");
  const cards = Object.entries(data.plots).map(([name, plot]) =>
    `<section><h2>${name.replace("viewport_", "").replace(".png", "").replaceAll("_", " ")}</h2>`
    + (plot.ready
      ? `<a href="${plot.url}"><img src="${plot.url}?v=${Date.now()}" alt="${name}"></a>`
      : "<p>Waiting for the first server-side render…</p>")
    + "</section>"
  ).join("");
  res.type("html").send(`<!doctype html>
<html><head><meta charset="utf-8"><meta http-equiv="refresh" content="5">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Viewport evaluation — ${broadcastId}</title>
<style>
body{font:15px system-ui,sans-serif;margin:24px;background:#111827;color:#e5e7eb}
a{color:#93c5fd} h1{margin-bottom:4px} .status{color:#9ca3af}
table{border-collapse:collapse;margin:18px 0}th,td{padding:6px 12px;border-bottom:1px solid #374151;text-align:right}
th:first-child,td:first-child{text-align:left}.plots{display:grid;grid-template-columns:repeat(auto-fit,minmax(480px,1fr));gap:18px}
section{background:#1f2937;padding:14px;border-radius:10px}section h2{margin:0 0 10px;text-transform:capitalize}
img{display:block;width:100%;height:auto;background:white;border-radius:6px}
</style></head><body>
<h1>Viewport predictor evaluation</h1>
<div class="status">Run: ${broadcastId} · trace ${(data.traceBytes / 1024).toFixed(1)} KiB · `
    + `${data.plotting ? "updating plots" : data.plotError ? "plot failed" : "plots current"} · auto-refresh 5 s</div>
${data.plotError ? `<p>Plot error: ${data.plotError}</p>` : ""}
${featureRows ? `<table><thead><tr><th>Feature</th><th>Unit</th><th>N</th><th>MAE</th><th>RMSE</th><th>P95</th></tr></thead><tbody>${featureRows}</tbody></table>` : "<p>Waiting for metrics…</p>"}
<div class="plots">${cards}</div>
</body></html>`);
}

app.get("/viewport-evaluation", renderViewportEvaluation);
app.get("/viewport-evaluation/:broadcastId", renderViewportEvaluation);

// NEW: Get latest interval data (for live monitoring)
app.get("/api/bitrate-counts-interval/latest", (req, res) => {
  const intervalsFile = intervalsFilePath();

  if (!fs.existsSync(intervalsFile)) {
    return res.json({ latest: null, message: "No interval data yet" });
  }
  
  const lines = fs.readFileSync(intervalsFile, 'utf-8').trim().split('\n');
  if (lines.length === 0) {
    return res.json({ latest: null, message: "No interval data yet" });
  }
  
  try {
    const latest = JSON.parse(lines[lines.length - 1]);
    res.json({ latest, totalIntervals: lines.length });
  } catch (e) {
    res.status(500).json({ error: "Failed to parse latest interval" });
  }
});

// NEW: Clear interval data (for new trace)
app.delete("/api/bitrate-counts-interval", (req, res) => {
  const intervalsFile = intervalsFilePath();

  if (fs.existsSync(intervalsFile)) {
    fs.unlinkSync(intervalsFile);
    logInfo("BITRATE", "Interval data cleared");
  }
  
  res.json({ status: "ok", message: "Interval data cleared" });
});

// ==================== END NEW ENDPOINTS ====================

app.get("/api/health", (req, res) => {
  res.json({
    status: "ok",
    active: broadcasts.size,
    segments: selectionLog.length,
    ladderBusy,
    pendingSegId,
    latestManifestSegId,
    latestManifestPath: getManifestPath(latestManifestSegId),
    streamConfig: STREAM_CONFIG,
    currentBroadcastId, // NEW
    currentScene,
    currentSceneObjects,
    currentSkybox,
    resultsDir: SERVER_RESULTS_DIR // NEW
  });
});
app.use((req, res, next) => { res.header('Access-Control-Allow-Origin','*'); res.header('Access-Control-Allow-Headers','Content-Type'); next(); });
app.get("/api/selections", (req, res) => {
  res.json(selectionLog);
});

// -------------------- Startup --------------------
async function bootstrap() {
  logInfo("SERVER", "Viewpoint logger + ladder generator starting");
  logInfo("SERVER", `  port:        ${PORT}`);
  logInfo("SERVER", `  manifest:    ${getManifestPath(0)} (initial)`);
  logInfo("SERVER", `  viewpoints:  ${SERVER_VIEWPOINTS_DIR}`);
  logInfo("SERVER", `  selections:  ${SERVER_SELECTIONS_DIR}`);
  logInfo("SERVER", `  results:     ${SERVER_RESULTS_DIR}`);
  logInfo("SERVER", `  ladder:      ${PYTHON_BIN} -m vstream.ladder.ladder_service (cwd ${REPO_ROOT})`);
  logInfo("SERVER", `  static:      /files -> ${FILES_ROOT}`);
  logInfo(
    "SERVER",
    `  config:      segments=${TOTAL_SEGMENTS} interval=${SEGMENT_INTERVAL}ms frames=${FRAMES_PER_SEGMENT} updateEvery=${UPDATE_INTERVAL_SEGMENTS}`
  );
  try {
    const launch = questLaunchProfile("");
    logInfo("SERVER", `  quest:       pipeline=${launch.pipeline}`
      + (launch.pipeline === "mesh" ? ""
        : ` mediaPort=${launch.baselinePort}`)
      + ` bandwidth=${launch.groundTruthBandwidthMode
        ? "ground-truth-trace"
        : launch.fullBandwidthMode ? "fixed-probe" : "adaptive"}`
      + ` prediction=${launch.viewportPredictionEnabled}`
      + ` culling=${launch.orbitViewCullingEnabled}`
      + ` (${launch.source})`);
  } catch (error) {
    logError("SERVER", `  quest:       invalid launch profile: ${error.message}`);
  }
  // Spawn the ladder service now so its heavy context (models, meshes,
  // raycast scenes, cached predictions) loads before the first request.
  startLadderService();

  if (latestManifestSegId < 0) {
    logInfo("SERVER", "No manifest on disk - generating default seg=0 manifest");
    requestLadderUpdate(0, null, "default manifest").catch(() => {});
  } else {
    logInfo("SERVER", "Initial segment-0 manifest already exists");
  }
  logInfo("SERVER", "First broadcast viewpoint will refresh segment 0 weights");
}

if (require.main === module) {
  // Before the port opens: a mismatched media cache must stop the server, not
  // produce a run whose reported bitrates belong to a different codec.
  try {
    enforceCorpusStamp();
  } catch (error) {
    logError("SERVER", error.message);
    process.exit(1);
  }
  if (COMPRESSED_ROOT) logInfo("SERVER", `corpus ${COMPRESSED_ROOT}`);
  app.listen(PORT, HOST, () => {
    bootstrap().catch((e) => {
      logError("SERVER", `Bootstrap failed: ${e.message}`);
    });
  });
}

module.exports = {
  app, questLaunchProfile, offlineTrajectorySamples, offlineTrajectoryPayload,
  sceneRelativeTrajectoryPayload,
};
