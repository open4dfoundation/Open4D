'use strict';

/**
 * Point-cloud renderer for the V4DS baselines.
 *
 * One `THREE.Points` per object, fed world-space clouds from
 * `point-reconstruction.js`. Shared by MetaStream, DeltaStream, ViVo, NAVA and
 * LiVo, which is the point: they differ in what they choose to send, not in how
 * a received cloud is drawn, so putting the drawing in one place keeps the
 * comparison about the algorithms.
 *
 * Deliberately separate from `webgl-renderer.js`. That one implements the
 * ClientPlatform renderer contract for the mesh ladder — decode caching,
 * per-frame clip playback, `object_ready` crediting. None of that applies here:
 * a baseline pushes a fresh cloud whenever it likes, there is no segment
 * buffer, and there is no ABR asking whether an object became playable. Forcing
 * both through one class would mean a contract that fits neither.
 *
 * Geometry is reallocated only when a cloud outgrows its buffer. Point counts
 * swing frame to frame (a DeltaStream residual adds a few thousand, a keyframe
 * replaces everything), and allocating a new BufferGeometry per frame at 30 Hz
 * makes the garbage collector the bottleneck.
 */

const THREE = require('three');
const { OrbitControls } = require('three/examples/jsm/controls/OrbitControls.js');

/** Extra headroom when growing a buffer, so small growth is not a realloc. */
const GROWTH_FACTOR = 1.5;

class PointRenderer {
    /**
     * @param {object} args
     * @param {HTMLCanvasElement} args.canvas
     * @param {number} [args.pointSize] world-space point size, metres
     * @param {object} [args.scene] { background }
     */
    constructor({ canvas, pointSize = 0.012, scene: sceneOptions = {} }) {
        this.canvas = canvas;
        this.pointSize = pointSize;
        this._sceneOptions = sceneOptions;
        this._objects = new Map();      // objectId -> { points, geometry, capacity }
        this._userMovedCamera = false;
        this._framedObjectCount = 0;
        this._running = false;
        this._presented = 0;
        this._three = null;
    }

    start() {
        this._three = this._buildScene();
        this._running = true;
        this._loop();
        return this;
    }

    stop() {
        this._running = false;
        if (this._rafHandle) cancelAnimationFrame(this._rafHandle);
        this._resizeObserver?.disconnect();
        for (const entry of this._objects.values()) {
            entry.geometry.dispose();
            entry.points.material.dispose();
        }
        this._objects.clear();
        this._three?.renderer.dispose();
    }

    _buildScene() {
        const renderer = new THREE.WebGLRenderer({ canvas: this.canvas, antialias: false });
        const scene = new THREE.Scene();
        scene.background = new THREE.Color(this._sceneOptions.background ?? 0x101014);
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
            renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
            renderer.setSize(width, height, false);
            camera.aspect = width / height;
            camera.updateProjectionMatrix();
        };
        apply();
        if (typeof ResizeObserver === 'function') {
            this._resizeObserver = new ResizeObserver(apply);
            this._resizeObserver.observe(this.canvas);
        } else {
            window.addEventListener('resize', apply);
        }

        return { renderer, scene, camera, controls };
    }

    _entry(objectId, requiredPoints) {
        let entry = this._objects.get(objectId);
        if (!entry) {
            const geometry = new THREE.BufferGeometry();
            const capacity = Math.max(1, Math.ceil(requiredPoints * GROWTH_FACTOR));
            geometry.setAttribute('position',
                new THREE.BufferAttribute(new Float32Array(capacity * 3), 3));
            geometry.setAttribute('color',
                new THREE.BufferAttribute(new Uint8Array(capacity * 3), 3, true));
            const material = new THREE.PointsMaterial({
                size: this.pointSize, sizeAttenuation: true, vertexColors: true
            });
            const points = new THREE.Points(geometry, material);
            points.frustumCulled = false;   // bounds change every frame
            this._three.scene.add(points);
            entry = { points, geometry, capacity };
            this._objects.set(objectId, entry);
            return entry;
        }
        if (requiredPoints > entry.capacity) {
            const capacity = Math.ceil(requiredPoints * GROWTH_FACTOR);
            entry.geometry.setAttribute('position',
                new THREE.BufferAttribute(new Float32Array(capacity * 3), 3));
            entry.geometry.setAttribute('color',
                new THREE.BufferAttribute(new Uint8Array(capacity * 3), 3, true));
            entry.capacity = capacity;
        }
        return entry;
    }

    /**
     * Show the per-object world clouds produced by a reconstruction step.
     *
     * @param {Map<number, {positions: Float32Array, colors: Uint8Array, pointCount: number}>} clouds
     */
    update(clouds) {
        for (const [objectId, cloud] of clouds) {
            const count = cloud.pointCount;
            const entry = this._entry(objectId, count);
            const position = entry.geometry.getAttribute('position');
            const color = entry.geometry.getAttribute('color');
            position.array.set(cloud.positions);
            color.array.set(cloud.colors);
            // drawRange, not a resize: the buffers are oversized on purpose and
            // only the first `count` points are valid this frame.
            entry.geometry.setDrawRange(0, count);
            position.needsUpdate = true;
            color.needsUpdate = true;
            entry.points.visible = count > 0;
            entry.lastCount = count;
        }
        // An object the stream stopped sending should disappear rather than
        // freeze at its last cloud, which would read as a live object.
        for (const [objectId, entry] of this._objects) {
            if (!clouds.has(objectId)) entry.points.visible = false;
        }
        if (!this._userMovedCamera && this._objects.size !== this._framedObjectCount) {
            this._framedObjectCount = this._objects.size;
            this.frameScene(clouds);
        }
    }

    /**
     * Point the camera at the content.
     *
     * Computed from the cloud data rather than Three.js bounding spheres: the
     * buffers are oversized, so the unused tail would drag the bounds towards
     * the origin. The ORBIT corpus is also baked into venue world coordinates
     * with raised floor tiers, so a fixed pose frames empty air.
     */
    frameScene(clouds) {
        if (this._userMovedCamera || !this._three) return;
        let minX = Infinity, minY = Infinity, minZ = Infinity;
        let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
        let total = 0;
        for (const cloud of clouds.values()) {
            for (let i = 0; i < cloud.pointCount; i++) {
                const x = cloud.positions[i * 3];
                const y = cloud.positions[i * 3 + 1];
                const z = cloud.positions[i * 3 + 2];
                if (x < minX) minX = x;
                if (y < minY) minY = y;
                if (z < minZ) minZ = z;
                if (x > maxX) maxX = x;
                if (y > maxY) maxY = y;
                if (z > maxZ) maxZ = z;
                total++;
            }
        }
        if (total === 0 || !Number.isFinite(minX)) return;

        const centre = new THREE.Vector3(
            (minX + maxX) / 2, (minY + maxY) / 2, (minZ + maxZ) / 2);
        const radius = Math.max(
            0.5, Math.hypot(maxX - minX, maxY - minY, maxZ - minZ) / 2);
        const { camera, controls } = this._three;
        const distance = (radius / Math.tan((camera.fov * Math.PI) / 360)) * 1.6;
        controls.target.copy(centre);
        camera.position.set(centre.x, centre.y + radius * 0.15, centre.z + distance);
        camera.near = Math.max(radius / 20, 0.05);
        camera.far = distance + radius * 30;
        camera.updateProjectionMatrix();
        controls.update();
    }

    _loop() {
        if (!this._running) return;
        this._rafHandle = requestAnimationFrame(() => this._loop());
        this._three.controls.update();
        this._three.renderer.render(this._three.scene, this._three.camera);
        this._presented++;
    }

    /** Camera pose in the shape V4DS FEEDBACK wants. */
    viewerState() {
        const { camera, controls } = this._three;
        camera.updateMatrixWorld();
        const forward = new THREE.Vector3();
        camera.getWorldDirection(forward);
        return {
            position: camera.position.toArray(),
            forward: forward.toArray(),
            up: camera.up.toArray(),
            target: controls.target.toArray(),
            verticalFovDegrees: camera.fov,
            aspect: camera.aspect,
            near: camera.near,
            far: camera.far
        };
    }

    get stats() {
        return {
            framesPresented: this._presented,
            objects: [...this._objects].map(([objectId, entry]) => ({
                objectId,
                points: entry.lastCount ?? 0,
                capacity: entry.capacity,
                visible: entry.points.visible
            }))
        };
    }
}

module.exports = { PointRenderer };
