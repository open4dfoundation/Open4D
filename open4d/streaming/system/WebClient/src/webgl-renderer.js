'use strict';

/**
 * Browser RendererAdapter: Three.js scene + Draco worker + WebCodecs textures.
 *
 * Implements the renderer capability of ../../ClientCore/platform.js. Everything
 * the core needs is here: `start`, `stop`, `stageSegment`, `setPlaybackState`,
 * `setCallbacks`, and a live `latestCamera`.
 *
 * The contract that matters most is the crediting one: an object becomes
 * playable only when this reports `object_ready`. The core waits for that event
 * before filling the object's buffer, so a renderer that stages a segment and
 * never acknowledges it will show every object as missing. Acknowledge exactly
 * once per (segment, object), and report `object_error` with `superseded: true`
 * when a newer segment picked a different representation — that is not a failure
 * and the core counts it separately.
 *
 * Presentation is index-aligned: frame N of the geometry is shown with frame N
 * of the texture. A null geometry frame (a download or decode gap) holds the
 * previous frame rather than shifting the clip, which is why download-plan
 * preallocates a null slot per frame.
 */

const THREE = require('three');
const { OrbitControls } = require('three/examples/jsm/controls/OrbitControls.js');

const { DecodeCache } = require('./decode-cache');
const { fromThreeCamera } = require('./camera-pose');
const {
    decodeTextureClip, closeTextureClip, textureClipBytes
} = require('./texture-decoder');

/** Bytes a decoded geometry clip occupies. */
function geometryClipBytes(frames) {
    let total = 0;
    for (const frame of frames) {
        if (!frame) continue;
        for (const key of ['positions', 'normals', 'uvs', 'indices']) {
            total += frame[key]?.byteLength || 0;
        }
    }
    return total;
}

/** One object's presentable clip: geometry frames plus optional texture frames. */
class ObjectClip {
    constructor({ repId, geometry, texture }) {
        this.repId = repId;
        this.geometry = geometry;           // (frame | null)[]
        this.texture = texture;             // { frames, info } | null
        this.frameCount = geometry.length;
    }

    get bytes() {
        return geometryClipBytes(this.geometry)
            + (this.texture
                ? textureClipBytes(this.texture.info, this.texture.frames.length)
                : 0);
    }

    dispose() {
        closeTextureClip(this.texture);
        this.texture = null;
        this.geometry = [];
    }
}

class WebGLRenderer {
    /**
     * @param {object} args
     * @param {HTMLCanvasElement} args.canvas
     * @param {(handle: string) => (ArrayBuffer|Promise<ArrayBuffer>|null)} args.readAsset
     *   Reads downloaded bytes back out by handle (platform.assetStore.get).
     * @param {string} [args.workerUrl] bundled draco-worker.js
     * @param {string} [args.vendorBase] where draco_decoder.{js,wasm} are served
     * @param {number} [args.decodeBudgetBytes]
     * @param {object} [args.scene] { background, cameraDistance }
     */
    constructor({
        canvas,
        readAsset,
        workerUrl = '/web/draco-worker.js',
        vendorBase = '/web/vendor/draco',
        decodeBudgetBytes = 512 * 1024 * 1024,
        // Must match vstream/config.py VIEW_RAYCAST_UNITS_PER_METER: the baked
        // corpus is metres but Open3D extrinsics are millimetres, and a mismatch
        // makes every server-side raycast miss.
        unitsPerMeter = 1000,
        scene: sceneOptions = {}
    }) {
        this.canvas = canvas;
        this._readAsset = readAsset;
        this._workerUrl = workerUrl;
        this._vendorBase = vendorBase;
        this._sceneOptions = sceneOptions;
        this._unitsPerMeter = unitsPerMeter;

        this.callbacks = {};
        this.latestCamera = null;

        this._running = false;
        this._fps = 30;
        this._frameCount = 60;
        this._objects = new Map();      // objectName -> { mesh, material, clip }
        this._playback = { segmentId: 0, objects: {} };
        this._frameIndex = 0;
        this._lastAdvance = 0;
        this._presented = 0;
        this._dropped = 0;
        this._acknowledged = new Set();  // "segmentId:objectName"
        this._pendingStage = new Map();  // objectName -> { segmentId, info }
        this._userMovedCamera = false;
        this._framedObjectCount = 0;
        this._sceneBounds = null;        // THREE.Box3 union of present geometry

        this._decodeCache = new DecodeCache({
            budgetBytes: decodeBudgetBytes,
            onEvict: clip => clip.dispose?.()
        });

        this._worker = null;
        this._workerSeq = 0;
        this._workerWaiters = new Map();
    }

    // ------------------------------------------------------------ contract

    setCallbacks(callbacks) { this.callbacks = callbacks || {}; }

    async start({ fps, frameCount, initialCamera }) {
        this._fps = fps || 30;
        this._frameCount = frameCount || 60;

        this._three = this._buildScene(initialCamera);
        this.latestCamera = this._readCamera();

        this._worker = new Worker(this._workerUrl);
        this._worker.onmessage = event => {
            const waiter = this._workerWaiters.get(event.data.id);
            this._workerWaiters.delete(event.data.id);
            if (!waiter) return;
            if (event.data.error) waiter.reject(new Error(event.data.error));
            else waiter.resolve(event.data.frames);
        };
        this._worker.onerror = err => {
            this._log('error', `draco worker failed: ${err.message}`);
        };

        this._running = true;
        this._lastAdvance = performance.now();
        this._loop();
    }

    async stop() {
        this._running = false;
        if (this._rafHandle) cancelAnimationFrame(this._rafHandle);
        this._resizeObserver?.disconnect();
        this._resizeObserver = null;
        if (this._onWindowResize) {
            window.removeEventListener('resize', this._onWindowResize);
            this._onWindowResize = null;
        }
        this._worker?.terminate();
        this._worker = null;
        this._decodeCache.clear();
        for (const entry of this._objects.values()) {
            entry.mesh.geometry.dispose();
            entry.material.dispose();
            entry.texture?.dispose();
        }
        this._objects.clear();
        this._three?.renderer.dispose();
    }

    /**
     * Take one segment's per-object asset handles and decode them.
     *
     * Fire-and-forget by design: the core does not await this, it waits for the
     * `object_ready` events. Decoding is per object so a slow object cannot
     * delay its neighbours.
     */
    stageSegment(segmentId, objects) {
        for (const [objectName, info] of Object.entries(objects)) {
            this._pendingStage.set(objectName, { segmentId, info });
            this._prepareObject(segmentId, objectName, info).catch(err => {
                this._emitObjectError(segmentId, objectName, info.repId, err.message);
            });
        }
    }

    setPlaybackState(segmentId, objects) {
        this._playback = { segmentId, objects: objects || {} };
    }

    // -------------------------------------------------------------- decode

    async _prepareObject(segmentId, objectName, info) {
        // A newer segment may have replaced this rep before the decode started.
        // That is not a failure: no work was lost and the new rep is queued.
        const stillCurrent = () =>
            this._pendingStage.get(objectName)?.segmentId === segmentId;

        const started = performance.now();
        const { clip, cacheHit, decodeShared } = await this._decodeCache.get(
            objectName, info.repId, async () => {
                const clip = await this._decodeClip(objectName, info);
                return { clip, bytes: clip.bytes };
            });

        if (!stillCurrent()) {
            this._emitObjectError(segmentId, objectName, info.repId,
                'superseded', { superseded: true });
            return;
        }

        this._applyClip(objectName, clip);
        // Protect the clip now on screen: evicting it would show up as a frame
        // reverting mid-playback rather than as an error.
        this._decodeCache.pinOnly(objectName, info.repId);

        const key = `${segmentId}:${objectName}`;
        if (this._acknowledged.has(key)) return;   // credit exactly once
        this._acknowledged.add(key);
        this.callbacks.onObjectReady?.({
            type: 'object_ready',
            segmentId,
            objectName,
            repId: info.repId,
            decodeMs: performance.now() - started,
            cacheHit,
            decodeShared
        });
    }

    async _decodeClip(objectName, info) {
        const geometryBuffers = await Promise.all(
            (info.geometryFiles || []).map(handle => (
                handle ? this._readAsset(handle) : null)));

        const geometry = await this._decodeGeometry(geometryBuffers);

        let texture = null;
        const textureHandle = (info.textureFiles || []).find(Boolean)
            || info.textureFile || null;
        if (textureHandle) {
            try {
                const buffer = await this._readAsset(textureHandle);
                if (buffer) texture = await decodeTextureClip(buffer);
            } catch (err) {
                // An untextured mesh still plays, and download-plan already
                // treats the texture as optional. Say so once and continue,
                // rather than failing the object.
                this._log('warn',
                    `${objectName}: texture decode failed (${err.message}); `
                    + 'rendering untextured');
            }
        }

        return new ObjectClip({ repId: info.repId, geometry, texture });
    }

    _decodeGeometry(buffers) {
        if (!this._worker) return Promise.resolve([]);
        const id = ++this._workerSeq;
        const transfer = buffers.filter(Boolean);
        return new Promise((resolve, reject) => {
            this._workerWaiters.set(id, { resolve, reject });
            this._worker.postMessage(
                { id, buffers, vendorBase: this._vendorBase }, transfer);
        });
    }

    // --------------------------------------------------------------- scene

    _buildScene(initialCamera) {
        const renderer = new THREE.WebGLRenderer({
            canvas: this.canvas, antialias: true
        });
        renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));

        const scene = new THREE.Scene();
        scene.background = new THREE.Color(this._sceneOptions.background ?? 0x101014);

        // The corpus is metric, Y-up (see scene_layout.json), so a 1.6 m eye
        // height and a few metres back frames a human-scale subject.
        const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
        camera.position.set(0, 1.6, this._sceneOptions.cameraDistance ?? 4);

        const controls = new OrbitControls(camera, this.canvas);
        controls.target.set(0, 1.0, 0);
        controls.enableDamping = true;
        controls.update();
        // Once the viewer takes the camera, stop auto-framing: their pose is
        // the experiment's input and must not be overridden.
        controls.addEventListener('start', () => { this._userMovedCamera = true; });

        scene.add(new THREE.HemisphereLight(0xffffff, 0x404050, 1.1));
        const key = new THREE.DirectionalLight(0xffffff, 0.8);
        key.position.set(2, 4, 3);
        scene.add(key);

        if (initialCamera?.position) {
            camera.position.fromArray(initialCamera.position);
            if (initialCamera.target) controls.target.fromArray(initialCamera.target);
            controls.update();
        }

        this._installResizeHandling(renderer, camera);

        return { renderer, scene, camera, controls };
    }

    /**
     * Keep the drawing buffer and the camera aspect matched to the canvas box.
     *
     * A `window.resize` listener alone is not enough: the canvas lives in a
     * grid column beside a growing log pane, so its box changes without the
     * window ever resizing. Any mismatch between the buffer aspect and the
     * displayed box shows up as a stretched render, and the drift is gradual
     * enough to look like a projection bug rather than a layout one.
     *
     * ResizeObserver fires on the initial layout too, which also removes the
     * need to measure the canvas before CSS has settled.
     */
    _installResizeHandling(renderer, camera) {
        const apply = () => {
            // Fall back to the buffer size, then a sane default: a zero-sized
            // canvas would make aspect NaN and blank the view.
            const width = this.canvas.clientWidth || this.canvas.width || 1280;
            const height = this.canvas.clientHeight || this.canvas.height || 720;
            if (width <= 0 || height <= 0) return;
            renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
            // updateStyle: false — CSS owns the displayed size; Three.js only
            // owns the buffer. Letting it write inline styles here would fight
            // the grid.
            renderer.setSize(width, height, false);
            camera.aspect = width / height;
            camera.updateProjectionMatrix();
        };

        apply();
        if (typeof ResizeObserver === 'function') {
            this._resizeObserver = new ResizeObserver(apply);
            this._resizeObserver.observe(this.canvas);
        } else {
            this._onWindowResize = apply;
            window.addEventListener('resize', apply);
        }
        return apply;
    }

    _objectEntry(objectName) {
        let entry = this._objects.get(objectName);
        if (entry) return entry;
        const material = new THREE.MeshStandardMaterial({
            color: 0xbfbfc6, roughness: 0.85, metalness: 0.0
        });
        const mesh = new THREE.Mesh(new THREE.BufferGeometry(), material);
        mesh.frustumCulled = false;   // bounds change every frame
        this._three.scene.add(mesh);
        entry = { mesh, material, texture: null, clip: null };
        this._objects.set(objectName, entry);
        return entry;
    }

    _applyClip(objectName, clip) {
        const entry = this._objectEntry(objectName);
        entry.clip = clip;
    }

    /**
     * Point the camera at the content that actually arrived.
     *
     * A hardcoded pose cannot work here. The ORBIT corpus is baked into venue
     * world coordinates with three floor tiers (scene_layout.json: ground y=0,
     * stage y=1.55, riser y=2.11), so a subject's mesh can sit metres above the
     * origin — the first frame measured for `dancer` was centred at y=3.03.
     * Framing from a guessed target showed an empty view while everything else
     * worked perfectly, which is an expensive thing to debug.
     *
     * Only runs until the viewer touches the camera.
     */
    _frameScene() {
        if (this._userMovedCamera || !this._three) return;
        const box = new THREE.Box3();
        let any = false;
        for (const entry of this._objects.values()) {
            const geometry = entry.mesh.geometry;
            if (!geometry.attributes.position) continue;
            geometry.computeBoundingBox();
            if (!geometry.boundingBox) continue;
            box.union(geometry.boundingBox);
            any = true;
        }
        if (!any || box.isEmpty()) return;

        const centre = box.getCenter(new THREE.Vector3());
        const radius = Math.max(box.getSize(new THREE.Vector3()).length() / 2, 0.5);
        const { camera, controls } = this._three;
        // Fit the bounding sphere in the vertical field of view, with margin.
        const distance = (radius / Math.tan((camera.fov * Math.PI) / 360)) * 1.6;

        controls.target.copy(centre);
        camera.position.set(
            centre.x, centre.y + radius * 0.15, centre.z + distance);
        // Keep the near:far ratio modest. distance/1000 with a far plane 20x the
        // distance gave a ratio of ~5400, which pushed the subject's NDC depth
        // to 0.993 and wastes almost all of the depth buffer's precision —
        // z-fighting territory once several objects overlap. A 5 cm near plane
        // is closer than anyone can usefully orbit to.
        camera.near = Math.max(radius / 20, 0.05);
        camera.far = distance + radius * 30;
        camera.updateProjectionMatrix();
        controls.update();
        this._sceneBounds = box;
        this._log('info', 'framed scene', {
            centre: centre.toArray().map(v => Number(v.toFixed(2))),
            radius: Number(radius.toFixed(2)),
            distance: Number(distance.toFixed(2))
        });
    }

    /** Upload frame `index` of an object's clip to its mesh. */
    _presentFrame(objectName, entry) {
        const clip = entry.clip;
        if (!clip || clip.frameCount === 0) return;

        const index = this._frameIndex % clip.frameCount;
        const frame = clip.geometry[index];
        // A null frame is a download or decode gap: hold the previous frame
        // rather than shifting the clip.
        if (frame) {
            const geometry = entry.mesh.geometry;
            geometry.setAttribute('position',
                new THREE.BufferAttribute(frame.positions, 3));
            if (frame.normals) {
                geometry.setAttribute('normal',
                    new THREE.BufferAttribute(frame.normals, 3));
            }
            if (frame.uvs) {
                geometry.setAttribute('uv', new THREE.BufferAttribute(frame.uvs, 2));
            }
            if (frame.indices) geometry.setIndex(
                new THREE.BufferAttribute(frame.indices, 1));
            if (!frame.normals) geometry.computeVertexNormals();
            geometry.computeBoundingSphere();
        }

        const videoFrame = clip.texture?.frames?.[index];
        if (videoFrame) {
            // Recreated per frame: a VideoFrame is not a persistent texture
            // source, and Three.js needs to re-upload it either way.
            entry.texture?.dispose();
            entry.texture = new THREE.CanvasTexture(videoFrame);
            entry.texture.flipY = false;
            entry.material.map = entry.texture;
            entry.material.needsUpdate = true;
        }
    }

    /**
     * Which objects may be shown.
     *
     * MISSING is unintentional starvation; FROZEN is a deliberate policy
     * decision under bandwidth deficit, where the object keeps showing its last
     * frame while the rest of the scene plays. So a frozen object stays visible
     * and simply does not advance.
     */
    _visibleState(objectName) {
        return this._playback.objects?.[objectName]?.state ?? 'OK';
    }

    _loop() {
        if (!this._running) return;
        this._rafHandle = requestAnimationFrame(() => this._loop());

        const now = performance.now();
        const frameInterval = 1000 / this._fps;
        const elapsed = now - this._lastAdvance;
        let advanced = false;
        let dropped = 0;

        if (elapsed >= frameInterval) {
            // Content runs at a fixed rate; if the display fell behind by more
            // than one content frame, count the skipped ones rather than
            // silently slowing playback down.
            const steps = Math.floor(elapsed / frameInterval);
            dropped = Math.max(0, steps - 1);
            this._frameIndex += steps;
            this._lastAdvance += steps * frameInterval;
            advanced = true;
        }

        let presentedAny = false;
        for (const [objectName, entry] of this._objects) {
            const state = this._visibleState(objectName);
            entry.mesh.visible = state !== 'MISSING';
            if (advanced && state === 'OK') {
                this._presentFrame(objectName, entry);
                presentedAny = true;
            }
        }
        // Re-frame while the object set is still growing; objects arrive over
        // several segments under a bandwidth deficit.
        if (presentedAny && !this._userMovedCamera
            && this._objects.size !== this._framedObjectCount) {
            this._framedObjectCount = this._objects.size;
            this._frameScene();
        }

        this._three.controls.update();
        this._three.renderer.render(this._three.scene, this._three.camera);

        if (advanced) {
            this._presented++;
            this._dropped += dropped;
            this.latestCamera = this._readCamera();
            this.callbacks.onFrame?.({
                type: 'frame',
                segmentId: this._playback.segmentId,
                sourceFrame: this._frameIndex % this._frameCount,
                droppedFramesBefore: dropped,
                camera: this.latestCamera,
                objects: Object.fromEntries(
                    [...this._objects.keys()].map(name => [name, {
                        state: this._visibleState(name),
                        sourceFrame: this._objects.get(name)?.clip
                            ? this._frameIndex % this._objects.get(name).clip.frameCount
                            : null
                    }]))
            });
        }
    }

    /**
     * Camera pose in the shape the ladder service expects.
     *
     * This closes the viewpoint-aware loop: the core posts it every segment and
     * the server re-solves the ladder against it. It MUST be Open3D
     * `PinholeCameraParameters` — the server writes the posted JSON to disk
     * verbatim and reads it back with `o3d.io.read_pinhole_camera_parameters`.
     * Any other shape leaves the ladder unable to parse the pose, and it then
     * falls back to equal object weights with no error at all. See
     * ./camera-pose.js for the axis and unit conversions.
     */
    _readCamera() {
        const { camera } = this._three;
        return fromThreeCamera(camera, {
            width: this.canvas.clientWidth || this.canvas.width || 1280,
            height: this.canvas.clientHeight || this.canvas.height || 720,
            unitsPerMeter: this._unitsPerMeter
        });
    }

    _emitObjectError(segmentId, objectName, repId, message, extra = {}) {
        this.callbacks.onObjectReady?.({
            type: 'object_error', segmentId, objectName, repId, message, ...extra
        });
    }

    _log(level, message, data = null) {
        this.callbacks.onLog?.(level, message, data);
    }

    /** Close the window, as the desktop renderer's window close does. */
    requestClose(reason = 'user closed the view') {
        this.callbacks.onClosed?.({ reason });
    }

    get stats() {
        return {
            presented: this._presented,
            dropped: this._dropped,
            decodeCache: { ...this._decodeCache.stats, bytes: this._decodeCache.bytes }
        };
    }
}

module.exports = { WebGLRenderer, ObjectClip, geometryClipBytes };
