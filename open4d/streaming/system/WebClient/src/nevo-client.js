'use strict';

/**
 * NeVo viewer: plays pre-rendered ReRF / NeVo / captured-camera panels.
 *
 * A 2D canvas, not WebGL, because there is no geometry to draw — the frames are
 * images. See nevo-manifest.js for why NeVo cannot be rendered client-side at
 * all, and therefore why there is no camera control here.
 */

const {
    resolveConditions, frameFiles, frameFile, layoutPanels, conditionCaption,
    clipSummary
} = require('./nevo-manifest');

class NevoClient {
    /**
     * @param {object} args
     * @param {string} args.assetBase   URL prefix serving the render output root
     * @param {HTMLCanvasElement} args.canvas
     * @param {string} args.object      clip directory name, e.g. g_dancer
     * @param {boolean} [args.nevoOnly]
     * @param {number} [args.fps]
     * @param {(event: object) => void} [args.onEvent]
     */
    constructor({
        assetBase, canvas, object, nevoOnly = false, fps = 8, onEvent = null
    }) {
        this.assetBase = assetBase.replace(/\/+$/, '');
        this.canvas = canvas;
        this.object = object;
        this.nevoOnly = nevoOnly;
        this.fps = fps;
        this._onEvent = onEvent;

        this.manifest = null;
        this.conditions = [];
        this.images = new Map();       // file -> HTMLImageElement
        this.crop = null;
        this.frameIndex = 0;
        this.playing = false;

        this.stats = {
            imagesLoaded: 0, imagesFailed: 0, bytesUnknown: true,
            framesPresented: 0, lastDrawMs: 0
        };
        this._context = canvas.getContext('2d');
        this._lastAdvance = 0;
    }

    _emit(type, detail = {}) { this._onEvent?.({ type, ...detail }); }

    get clipBase() { return `${this.assetBase}/${this.object}`; }

    async start() {
        const response = await fetch(`${this.clipBase}/manifest.json`,
            { cache: 'no-store' });
        if (!response.ok) {
            throw new Error(
                `manifest fetch failed for ${this.object}: HTTP ${response.status}`);
        }
        this.manifest = await response.json();
        this.conditions = resolveConditions(this.manifest,
            { nevoOnly: this.nevoOnly });
        this._emit('manifest', clipSummary(this.manifest, this.conditions));

        this._installResize();
        await this._preload();

        this.playing = true;
        this._lastAdvance = performance.now();
        this._loop();
    }

    /**
     * Load every image before playing.
     *
     * A clip is 30 images at most, so loading them all up front costs a second
     * and removes any chance of the comparison showing one condition a frame
     * behind another — which would be indistinguishable from a real difference
     * between the conditions.
     */
    async _preload() {
        const entries = frameFiles(this.manifest, this.conditions);
        await Promise.all(entries.map(entry => new Promise(resolve => {
            const image = new Image();
            image.onload = () => {
                this.images.set(entry.file, image);
                this.stats.imagesLoaded++;
                resolve();
            };
            image.onerror = () => {
                this.stats.imagesFailed++;
                this._emit('error', { message: `missing render ${entry.file}` });
                resolve();
            };
            image.src = `${this.clipBase}/${entry.file}`;
        })));
        this.crop = this._contentCrop();
        this._emit('ready', {
            loaded: this.stats.imagesLoaded, failed: this.stats.imagesFailed,
            crop: this.crop
        });
    }

    /**
     * Crop to the subject, so three 4:3 panels side by side are not mostly
     * empty background.
     *
     * Measured once from the captured-camera frame and then applied IDENTICALLY
     * to every panel — identically is the point, because the conditions have to
     * stay pixel-aligned or the comparison stops meaning anything. This mirrors
     * what live_demo.py does on the server side.
     *
     * The renders are a subject over pure white, so "content" is anything below
     * the white threshold.
     */
    _contentCrop(pad = 0.06, threshold = 246) {
        const reference = this.conditions.find(c => c.isReference)
            || this.conditions[0];
        const image = this.images.get(
            frameFile(reference.prefix, this.manifest.frames[0]));
        if (!image) return null;

        const probe = document.createElement('canvas');
        probe.width = image.naturalWidth;
        probe.height = image.naturalHeight;
        const context = probe.getContext('2d', { willReadFrequently: true });
        context.drawImage(image, 0, 0);
        let data;
        try {
            data = context.getImageData(0, 0, probe.width, probe.height).data;
        } catch (_) {
            return null;   // tainted canvas; fall back to the full frame
        }

        let minX = probe.width, minY = probe.height, maxX = -1, maxY = -1;
        for (let y = 0; y < probe.height; y++) {
            for (let x = 0; x < probe.width; x++) {
                const i = (y * probe.width + x) * 4;
                if (data[i] < threshold || data[i + 1] < threshold
                    || data[i + 2] < threshold) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        if (maxX < 0) return null;

        const padX = (maxX - minX) * pad;
        const padY = (maxY - minY) * pad;
        const left = Math.max(0, Math.round(minX - padX));
        const top = Math.max(0, Math.round(minY - padY));
        const right = Math.min(probe.width, Math.round(maxX + padX + 1));
        const bottom = Math.min(probe.height, Math.round(maxY + padY + 1));
        return { x: left, y: top, width: right - left, height: bottom - top };
    }

    _installResize() {
        const apply = () => {
            const ratio = Math.min(window.devicePixelRatio || 1, 2);
            const width = this.canvas.clientWidth || 1280;
            const height = this.canvas.clientHeight || 720;
            this.canvas.width = Math.max(1, Math.round(width * ratio));
            this.canvas.height = Math.max(1, Math.round(height * ratio));
            this._draw();
        };
        apply();
        if (typeof ResizeObserver === 'function') {
            this._resizeObserver = new ResizeObserver(apply);
            this._resizeObserver.observe(this.canvas);
        } else {
            window.addEventListener('resize', apply);
        }
    }

    _draw() {
        if (!this.manifest || !this._context) return;
        const started = performance.now();
        const context = this._context;
        const { width, height } = this.canvas;

        context.fillStyle = '#101014';
        context.fillRect(0, 0, width, height);

        const frame = this.manifest.frames[this.frameIndex];
        const first = this.images.get(
            frameFile(this.conditions[0].prefix, frame));
        if (!first) return;

        const crop = this.crop
            || { x: 0, y: 0, width: first.naturalWidth, height: first.naturalHeight };
        const layout = layoutPanels({
            panelCount: this.conditions.length,
            sourceWidth: crop.width,
            sourceHeight: crop.height,
            canvasWidth: width,
            canvasHeight: height,
            labelHeight: Math.max(18, Math.round(height * 0.03))
        });

        context.textBaseline = 'top';
        context.font = `${Math.max(10, Math.round(layout.labelHeight * 0.6))}px `
            + 'ui-monospace, Menlo, monospace';

        this.conditions.forEach((condition, index) => {
            const panel = layout.panels[index];
            const image = this.images.get(frameFile(condition.prefix, frame));
            if (image) {
                context.drawImage(image,
                    crop.x, crop.y, crop.width, crop.height,
                    panel.x, panel.y, panel.width, panel.height);
            } else {
                context.fillStyle = '#1a1a22';
                context.fillRect(panel.x, panel.y, panel.width, panel.height);
            }
            // The captured camera is ground truth, so mark it differently from
            // the two reconstructions it is there to judge.
            context.fillStyle = condition.isReference ? '#8b8b98' : '#e6e6ea';
            context.fillText(conditionCaption(condition),
                panel.x + 2, panel.labelY + 2, panel.width - 4);
        });

        this.stats.lastDrawMs = performance.now() - started;
        this.stats.framesPresented++;
    }

    _loop() {
        this._rafHandle = requestAnimationFrame(() => this._loop());
        if (!this.playing || !this.manifest) return;
        const interval = 1000 / this.fps;
        const now = performance.now();
        if (now - this._lastAdvance >= interval) {
            const steps = Math.floor((now - this._lastAdvance) / interval);
            this._lastAdvance += steps * interval;
            this.frameIndex =
                (this.frameIndex + steps) % this.manifest.frames.length;
            this._draw();
        }
    }

    setPlaying(playing) {
        this.playing = playing;
        this._lastAdvance = performance.now();
    }

    stop() {
        this.playing = false;
        if (this._rafHandle) cancelAnimationFrame(this._rafHandle);
        this._resizeObserver?.disconnect();
    }

    inspect() {
        return {
            object: this.object,
            frameIndex: this.frameIndex,
            frameCount: this.manifest ? this.manifest.frames.length : 0,
            playing: this.playing,
            conditions: this.conditions.map(c => ({
                prefix: c.prefix, label: c.label,
                keptFraction: c.kept_fraction ?? null
            })),
            summary: this.manifest
                ? clipSummary(this.manifest, this.conditions) : null,
            crop: this.crop,
            stats: { ...this.stats },
            canvas: { width: this.canvas.width, height: this.canvas.height }
        };
    }
}

module.exports = { NevoClient };
