'use strict';

/**
 * Vega baseline viewer: fetches exported VGS frames and plays them as splats.
 *
 * Unlike the V4DS baselines there is no socket and no server-side adaptation
 * here. `export_quest.py` writes a per-object sequence of `.vgs` frames plus a
 * catalogue, and this plays them at the catalogue's frame rate — which is what
 * the Vega baseline is in this repo: an offline-evaluation asset path, not a
 * live streaming system (see baselines/Vega/README.md "Quest scope").
 *
 * So this viewer answers "what does Vega's representation look like, decoded
 * and rendered in a browser", and deliberately does not pretend to measure
 * adaptation it does not perform.
 */

const THREE = require('three');
const { OrbitControls } = require('three/examples/jsm/controls/OrbitControls.js');

const { decodeVgsFrame, decodedFrameBytes } = require('./vgs-format');
const { SplatObject } = require('./splat-renderer');

class VegaClient {
    /**
     * @param {object} args
     * @param {string} args.assetBase   URL prefix serving the export directory
     * @param {HTMLCanvasElement} args.canvas
     * @param {number} [args.splatScale]
     * @param {(event: object) => void} [args.onEvent]
     */
    constructor({
        assetBase, canvas, splatScale = 1.0, onEvent = null,
        splatMode = 'isotropic', frameMode = 'object'
    }) {
        this.assetBase = assetBase.replace(/\/+$/, '');
        this.canvas = canvas;
        this.splatScale = splatScale;
        this.splatMode = splatMode;
        this._onEvent = onEvent;

        this.catalog = null;
        this.objects = new Map();      // name -> { entry, splat, frames: Map }
        this.frameIndex = 0;
        this.playing = false;
        // Frames that are loaded for EVERY object, so playback loops over a
        // contiguous run rather than stuttering through half-loaded ones.
        this.loadedFrames = 0;
        this._inflight = new Map();    // `${name}/${index}` -> Promise
        this._failed = new Map();      // same key -> message, reported once

        this.stats = {
            framesFetched: 0, framesDecoded: 0, decodeFailures: 0,
            bytesFetched: 0, decodedBytes: 0, splatsOnScreen: 0,
            framesPresented: 0, sorts: 0, lastDecodeMs: 0
        };

        this._three = null;
        this._userMovedCamera = false;
        this._framed = false;
        this._lastAdvance = 0;
        this.frameMode = frameMode;
        this._focus = null;
    }

    _emit(type, detail = {}) { this._onEvent?.({ type, ...detail }); }

    async start(objectNames = null) {
        this._three = this._buildScene();
        this._loop();

        // fetch() rejects with a bare "Failed to fetch"/"Load failed" for every
        // network-level cause -- server down, connection reset, a blocking
        // extension -- and without the URL that is not diagnosable. Name it.
        const catalogUrl = new URL(`${this.assetBase}/catalog.json`,
            window.location.href).href;
        let response;
        try {
            response = await fetch(catalogUrl, { cache: 'no-store' });
        } catch (cause) {
            throw new Error(
                `could not reach ${catalogUrl} (${cause.message}). The request `
                + 'never got an HTTP reply, so this is the server being '
                + 'unreachable, the connection being reset, or an extension '
                + 'blocking it -- not a missing export. Open that URL directly '
                + 'to tell which.');
        }
        if (!response.ok) {
            throw new Error(
                `${catalogUrl} returned HTTP ${response.status}; the export is `
                + 'missing or served from a different root than ?assets=');
        }
        this.catalog = await response.json();
        if (this.catalog.format !== 'vs4d-vega-quest') {
            throw new Error(`unexpected catalogue format ${this.catalog.format}`);
        }

        const wanted = objectNames && objectNames.length
            ? new Set(objectNames) : null;
        const entries = this.catalog.objects.filter(
            entry => !wanted || wanted.has(entry.name));
        if (entries.length === 0) {
            throw new Error('no objects in the catalogue matched the request');
        }

        for (const entry of entries) {
            // The real maximum, from the catalogue, not a placeholder. Growing
            // the instanced attribute after the geometry has been drawn once
            // silently caps the draw at one splat; see SplatObject._allocate.
            const capacity = Math.max(
                1, ...(entry.frames || []).map(frame => Number(frame.points) || 0));
            const splat = new SplatObject({ capacity, mode: this.splatMode });
            splat.material.uniforms.splatScale.value = this.splatScale;
            this._three.scene.add(splat.mesh);
            this.objects.set(entry.name, { entry, splat, frames: new Map() });
        }

        this._emit('catalog', {
            fps: this.catalog.fps,
            frameCount: this.catalog.frameCount,
            objects: entries.map(e => ({
                name: e.name, frames: e.frames.length,
                points: e.frames[0]?.points ?? 0
            })),
            colorApproximation: this.catalog.colorApproximation
        });

        // The whole clip is loaded before playback starts, and nothing is
        // evicted. This is not a nicety, it is the only workable shape.
        //
        // A frame is ~1.1 MB and the catalogue's rate is 30 fps, so streaming
        // two objects in real time needs ~509 Mbps. Over anything slower the
        // previous design -- fetch a sliding window on every frame tick --
        // issued requests far faster than they could complete, and because it
        // checked only the DECODED cache it re-issued each pending frame on
        // every subsequent tick. Chrome's socket pool saturated and began
        // rejecting with ERR_INSUFFICIENT_RESOURCES, which surfaces as a bare
        // `TypeError: Failed to fetch` with no backoff, so the log filled with
        // failures forever. It worked only when served from the same machine,
        // where a fetch finishes inside one tick.
        //
        // Vega here is an offline-evaluation asset path, not a live streaming
        // system, so there is nothing to preserve by streaming it: load the
        // 30-frame clip once and loop it from memory, exactly as the NeVo page
        // does with its renders.
        await this._preload();
        this._showFrame(0);
        this.playing = true;
        this._lastAdvance = performance.now();
    }

    /** Frames playable right now: the contiguous run present for every object. */
    get frameCount() {
        return this.loadedFrames || 0;
    }

    get catalogFrameCount() {
        return this.catalog?.frameCount ?? 0;
    }

    /**
     * Load every frame of every object, bounded, frame-major.
     *
     * Frame-major so that a preload cut short still yields a contiguous run
     * playable for all objects; object-major would leave one object complete
     * and the other empty. Concurrency is bounded because that bound is the
     * whole point -- unbounded is what broke this.
     */
    async _preload(concurrency = 6) {
        const total = this.catalogFrameCount;
        const objects = [...this.objects.values()];
        const tasks = [];
        for (let index = 0; index < total; index++) {
            for (const object of objects) tasks.push({ object, index });
        }

        let next = 0;
        let done = 0;
        const worker = async () => {
            while (next < tasks.length) {
                const task = tasks[next++];
                await this._ensureFrame(task.object, task.index);
                done++;
                if (done % objects.length === 0) {
                    this._emit('preload', {
                        frames: done / objects.length,
                        totalFrames: total,
                        bytes: this.stats.bytesFetched
                    });
                }
            }
        };
        await Promise.all(Array.from(
            { length: Math.min(concurrency, tasks.length) }, worker));

        // The playable run stops at the first frame any object is missing.
        let playable = 0;
        while (playable < total
               && objects.every(object => object.frames.has(playable))) {
            playable++;
        }
        this.loadedFrames = playable;
        if (playable === 0) {
            const reasons = [...new Set(this._failed.values())].slice(0, 2);
            throw new Error('no frame loaded for every object'
                + (reasons.length ? `; first causes: ${reasons.join(' | ')}` : ''));
        }
        this._emit('preloaded', {
            frames: playable,
            totalFrames: total,
            bytes: this.stats.bytesFetched,
            decodedBytes: this.stats.decodedBytes,
            failures: this._failed.size
        });
    }

    _ensureFrame(object, index) {
        if (object.frames.has(index)) {
            return Promise.resolve(object.frames.get(index));
        }
        const key = `${object.entry.name}/${index}`;
        // Dedupe: without this a frame still in flight is requested again by
        // the next caller, which is how the old sliding-window prefetch
        // multiplied one slow link into a request storm.
        if (this._inflight.has(key)) return this._inflight.get(key);
        if (this._failed.has(key)) return Promise.resolve(null);

        const descriptor = object.entry.frames[index];
        if (!descriptor) return Promise.resolve(null);
        const frameUrl = new URL(`${this.assetBase}/${descriptor.file}`,
            window.location.href).href;

        const work = (async () => {
            try {
                const response = await fetch(frameUrl, { cache: 'no-store' });
                if (!response.ok) {
                    throw new Error(`HTTP ${response.status} for ${frameUrl}`);
                }
                const buffer = await response.arrayBuffer();
                this.stats.framesFetched++;
                this.stats.bytesFetched += buffer.byteLength;

                const started = performance.now();
                const decoded = decodeVgsFrame(buffer, {
                    scaleLogMin: this.catalog.scaleLogMin,
                    scaleLogMax: this.catalog.scaleLogMax
                });
                this.stats.lastDecodeMs = performance.now() - started;
                this.stats.framesDecoded++;
                this.stats.decodedBytes += decodedFrameBytes(decoded);
                object.frames.set(index, decoded);
                return decoded;
            } catch (error) {
                // Remembered, not retried. A frame that failed once will fail
                // the same way on a tick 33 ms later, and retrying forever is
                // what turned one fault into thousands of identical log lines.
                this.stats.decodeFailures++;
                const message = `${key}: ${error.message}`;
                this._failed.set(key, message);
                if (this._failed.size <= 3) this._emit('error', { message });
                else if (this._failed.size === 4) {
                    this._emit('error', {
                        message: 'further frame failures suppressed; see the '
                            + 'failure count in the stats panel'
                    });
                }
                return null;
            } finally {
                this._inflight.delete(key);
            }
        })();
        this._inflight.set(key, work);
        return work;
    }

    _showFrame(index) {
        let splats = 0;
        for (const object of this.objects.values()) {
            const frame = object.frames.get(index);
            if (!frame) continue;
            object.splat.setFrame(frame);
            splats += frame.count;
        }
        this.stats.splatsOnScreen = splats;
        if (!this._framed && splats > 0) {
            this._framed = true;
            if (this.frameMode !== 'all' && !this._focus) {
                this._focus = this.objectNames[0] ?? null;
                const spread = this._boundsOf().getSize(new THREE.Vector3());
                const largest = Math.max(spread.x, spread.y, spread.z);
                this._emit('framed', {
                    focus: this._focus,
                    others: this.objectNames.filter(n => n !== this._focus),
                    sceneSpanMetres: Number(largest.toFixed(1))
                });
            }
            this._frameScene();
        }
    }

    _buildScene() {
        const renderer = new THREE.WebGLRenderer({
            canvas: this.canvas, antialias: false, premultipliedAlpha: false
        });
        const scene = new THREE.Scene();
        scene.background = new THREE.Color(0x101014);
        const camera = new THREE.PerspectiveCamera(60, 1, 0.05, 500);
        camera.position.set(0, 1.6, 4);
        const controls = new OrbitControls(camera, this.canvas);
        controls.target.set(0, 1.0, 0);
        controls.enableDamping = true;
        controls.update();
        controls.addEventListener('start', () => { this._userMovedCamera = true; });

        const apply = () => {
            const width = this.canvas.clientWidth || this.canvas.width || 1280;
            const height = this.canvas.clientHeight || this.canvas.height || 720;
            if (width <= 0 || height <= 0) return;
            const pixelRatio = Math.min(window.devicePixelRatio || 1, 2);
            renderer.setPixelRatio(pixelRatio);
            renderer.setSize(width, height, false);
            camera.aspect = width / height;
            camera.updateProjectionMatrix();
            for (const object of this.objects.values()) {
                object.splat.material.uniforms.viewport.value.set(
                    width * pixelRatio, height * pixelRatio);
            }
        };
        apply();
        this._applyViewport = apply;
        if (typeof ResizeObserver === 'function') {
            this._resizeObserver = new ResizeObserver(apply);
            this._resizeObserver.observe(this.canvas);
        } else {
            window.addEventListener('resize', apply);
        }
        return { renderer, scene, camera, controls };
    }

    /** Frame the catalogue's bounds; they are venue world coordinates. */
    /** Bounding box of one object, or of every object when name is null. */
    _boundsOf(name = null) {
        const box = new THREE.Box3();
        for (const object of this.objects.values()) {
            if (name && object.entry.name !== name) continue;
            box.expandByPoint(new THREE.Vector3(...object.entry.boundsMin));
            box.expandByPoint(new THREE.Vector3(...object.entry.boundsMax));
        }
        return box;
    }

    /** Object names in catalogue order, for cycling the focus. */
    get objectNames() {
        return [...this.objects.values()].map(object => object.entry.name);
    }

    /**
     * Point the camera at one object, or at the whole scene when null.
     *
     * Framing the whole scene is the wrong default here, which is not obvious
     * until you look at where ORBIT bakes its subjects. They sit in venue world
     * coordinates on separate floor tiers: `dancer` occupies z -1.8..-0.9 while
     * `thomas` is at z +8.0..+8.4, nearly ten metres away. A camera that fits
     * both has to retreat to z 18.5, and at 60 degrees each subject then covers
     * a few hundred pixels -- measured at 0.43% of the canvas, which looks
     * exactly like an empty player rather than like a framing problem.
     *
     * So the default is to fill the view with one object and say that the
     * others are elsewhere. `focus(name)` cycles; `?frame=all` restores the
     * whole-scene fit for looking at the venue layout itself.
     */
    focus(name = null) {
        this._focus = name;
        this._userMovedCamera = false;
        this._frameScene();
    }

    _frameScene() {
        if (this._userMovedCamera || !this.catalog) return;
        const box = this._focus ? this._boundsOf(this._focus) : this._boundsOf();
        if (box.isEmpty()) return;
        const centre = box.getCenter(new THREE.Vector3());
        const radius = Math.max(box.getSize(new THREE.Vector3()).length() / 2, 0.5);
        const { camera, controls } = this._three;
        const distance = (radius / Math.tan((camera.fov * Math.PI) / 360)) * 1.6;
        controls.target.copy(centre);
        camera.position.set(centre.x, centre.y + radius * 0.15, centre.z + distance);
        camera.near = Math.max(radius / 20, 0.05);
        camera.far = distance + radius * 30;
        camera.updateProjectionMatrix();
        controls.update();
        this._applyViewport();
    }

    _loop() {
        this._rafHandle = requestAnimationFrame(() => this._loop());
        const { renderer, scene, camera, controls } = this._three;
        controls.update();
        camera.updateMatrixWorld();

        if (this.playing && this.frameCount > 0) {
            const interval = 1000 / (this.catalog.fps || 30);
            const now = performance.now();
            if (now - this._lastAdvance >= interval) {
                const steps = Math.floor((now - this._lastAdvance) / interval);
                this._lastAdvance += steps * interval;
                this.frameIndex = (this.frameIndex + steps) % this.frameCount;
                this._showFrame(this.frameIndex);
            }
        }

        // Re-sort only when the view or the frame changed; SplatObject.sort
        // decides that from the view matrix itself.
        for (const object of this.objects.values()) {
            if (object.splat.sort(camera)) this.stats.sorts++;
        }
        renderer.render(scene, camera);
        this.stats.framesPresented++;
    }

    setPlaying(playing) {
        this.playing = playing;
        this._lastAdvance = performance.now();
    }

    stop() {
        this.playing = false;
        if (this._rafHandle) cancelAnimationFrame(this._rafHandle);
        this._resizeObserver?.disconnect();
        for (const object of this.objects.values()) object.splat.dispose();
        this.objects.clear();
        this._three?.renderer.dispose();
    }

    /**
     * Render and read the canvas back in ONE synchronous task, plus every
     * input that decides splat size.
     *
     * Needed because "nothing is drawn" has several causes that look
     * identical on screen: the viewport uniform never updated, the camera
     * framed somewhere empty, the quads collapsed to sub-pixel, or the pixels
     * are there but too dark to see. A screenshot cannot tell them apart, and
     * `preserveDrawingBuffer` is false so reading later returns zeros.
     */
    probe() {
        if (!this._three) return { error: 'renderer not built' };
        const { renderer, scene, camera, controls } = this._three;
        renderer.render(scene, camera);
        const gl = renderer.getContext();
        const width = gl.drawingBufferWidth;
        const height = gl.drawingBufferHeight;
        const pixels = new Uint8Array(width * height * 4);
        gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
        const background = [pixels[0], pixels[1], pixels[2]];
        let lit = 0;
        let brightest = 0;
        for (let i = 0; i < pixels.length; i += 4) {
            const delta = Math.max(
                Math.abs(pixels[i] - background[0]),
                Math.abs(pixels[i + 1] - background[1]),
                Math.abs(pixels[i + 2] - background[2]));
            if (delta > 6) lit++;
            if (delta > brightest) brightest = delta;
        }
        const first = [...this.objects.values()][0];
        const uniforms = first?.splat.material.uniforms;
        // Where the splats land in normalised device coordinates: |x|,|y| <= 1
        // is on screen, and z outside [-1, 1] means clipped by near/far.
        const projected = [...this.objects.values()].map(object => {
            const centre = object.entry.boundsMin.map(
                (value, index) => (value + object.entry.boundsMax[index]) / 2);
            const point = new THREE.Vector3(...centre).project(camera);
            return {
                name: object.entry.name,
                ndc: point.toArray().map(value => Number(value.toFixed(3))),
                splats: object.splat.count
            };
        });
        return {
            litPixels: lit,
            coveragePct: Number((100 * lit / (width * height)).toFixed(3)),
            brightestDelta: brightest,
            background,
            drawingBuffer: [width, height],
            canvasCss: [this.canvas.clientWidth, this.canvas.clientHeight],
            devicePixelRatio: window.devicePixelRatio,
            viewportUniform: uniforms?.viewport.value.toArray(),
            splatScale: uniforms?.splatScale.value,
            splatMode: first?.splat.mode,
            defines: first?.splat.material.defines,
            focus: this._focus,
            camera: {
                position: camera.position.toArray().map(v => Number(v.toFixed(2))),
                target: controls.target.toArray().map(v => Number(v.toFixed(2))),
                near: Number(camera.near.toFixed(3)),
                far: Number(camera.far.toFixed(1)),
                fov: camera.fov
            },
            projected
        };
    }

    inspect() {
        const { camera, controls } = this._three || {};
        return {
            frameIndex: this.frameIndex,
            frameCount: this.frameCount,
            playing: this.playing,
            stats: { ...this.stats },
            objects: [...this.objects.entries()].map(([name, object]) => ({
                name,
                splats: object.splat.count,
                cached: object.frames.size,
                visible: object.splat.mesh.visible
            })),
            camera: camera ? {
                position: camera.position.toArray().map(v => Number(v.toFixed(3))),
                target: controls.target.toArray().map(v => Number(v.toFixed(3))),
                near: camera.near, far: camera.far
            } : null
        };
    }
}

module.exports = { VegaClient };
