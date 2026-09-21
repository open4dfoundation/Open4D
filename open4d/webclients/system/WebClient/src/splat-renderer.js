'use strict';

/**
 * 3D Gaussian splat renderer for the Vega baseline.
 *
 * Draws anisotropic Gaussians the way 3DGS does: project each splat's 3D
 * covariance into a screen-space 2D covariance, size a camera-facing quad to
 * its eigenvectors, and evaluate the Gaussian falloff in the fragment shader
 * with premultiplied-alpha blending, back to front.
 *
 * Two design choices worth knowing:
 *
 * 1. SPLAT DATA LIVES IN A TEXTURE, and the only per-instance attribute is an
 *    index into it. Correct 3DGS needs back-to-front ordering, which changes
 *    every time the camera moves. Reordering the attribute buffers themselves
 *    would mean copying 14 floats per splat per sort — about 3 MB for a 58k
 *    frame. Reordering a single index attribute is 230 KB.
 *
 * 2. SORTING IS A COUNTING SORT over quantised depth, not a comparison sort.
 *    At 58k splats a comparison sort costs milliseconds of the frame budget;
 *    a 16-bit bucket pass is a few hundred microseconds and the ordering error
 *    within a bucket is far below what the blending can show.
 *
 * Depth testing is off and depth writing is off, as 3DGS requires: the ordering
 * IS the depth resolution. Turning them on produces hard edges where splats
 * should blend.
 */

const THREE = require('three');

/** Texels per splat in the data texture: 4 x RGBA32F = 16 floats. */
const TEXELS_PER_SPLAT = 4;
const TEXTURE_WIDTH = 1024;
/** Depth buckets for the counting sort. */
const SORT_BUCKETS = 1 << 16;

const VERTEX_SHADER = /* glsl */`
precision highp float;
precision highp int;

// RawShaderMaterial does NOT inject Three.js's built-in uniforms, so they are
// declared here. GLSL 3 is required for transpose(), which GLSL ES 1.00 lacks.
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 projectionMatrix;

uniform sampler2D splatData;
uniform vec2 splatTextureSize;
uniform vec2 viewport;
uniform float splatScale;

in vec2 quadPosition;    // corner in [-1, 1]
in float splatIndex;

out vec4 vColor;
out vec2 vGaussian;

vec4 fetch(float texel) {
    float index = splatIndex * ${TEXELS_PER_SPLAT}.0 + texel;
    float x = mod(index, splatTextureSize.x);
    float y = floor(index / splatTextureSize.x);
    return texture(splatData, (vec2(x, y) + 0.5) / splatTextureSize);
}

mat3 quaternionToMatrix(vec4 q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    return mat3(
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y),
        2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x),
        2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y)
    );
}

void main() {
    vec4 centerAndOpacity = fetch(0.0);
    vec4 logScale = fetch(1.0);
    vec4 rotation = fetch(2.0);
    vec4 color = fetch(3.0);

    vec3 center = centerAndOpacity.xyz;
    float opacity = centerAndOpacity.w;

    mat4 modelView = viewMatrix * modelMatrix;
    vec4 cam = modelView * vec4(center, 1.0);
    vec4 clip = projectionMatrix * cam;
    // Behind the camera: park the vertex outside the clip volume rather than
    // letting a divide by a near-zero w throw the quad across the screen.
    if (clip.w <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vColor = vec4(0.0);
        vGaussian = vec2(0.0);
        return;
    }

    vec3 scale = exp(logScale.xyz) * splatScale;

#ifdef ISOTROPIC_SPLATS
    // Isotropic path: size the quad from the mean scale projected through the
    // focal length, with no covariance projection at all.
    //
    // Two reasons this is the default rather than a fallback. First, Vega's
    // Gaussians are near-isotropic — the exported log scales sit within about
    // 0.3 of each other — so the anisotropic projection buys very little here.
    // Second, the full covariance path (mat3 transposes and products in the
    // vertex stage) does not render under software GL: measured in headless
    // Chrome with SwiftShader, a shader that merely CONTAINS that math draws
    // nothing even when gl_Position does not use its result, while the same
    // shader without it draws correctly. That is a driver-level failure, not a
    // logic error, but it makes the anisotropic path unverifiable here.
    float fxIso = projectionMatrix[0][0] * viewport.x * 0.5;
    float fyIso = projectionMatrix[1][1] * viewport.y * 0.5;
    float meanScale = (scale.x + scale.y + scale.z) / 3.0;
    float depth = max(-cam.z, 1e-4);
    // Two sigma, with a half-pixel floor so a distant splat still marks a pixel.
    vec2 radiusPx = vec2(max(2.0 * fxIso * meanScale / depth, 0.5),
                         max(2.0 * fyIso * meanScale / depth, 0.5));
    vec2 offset = quadPosition * radiusPx;
    vColor = vec4(color.rgb, opacity);
    vGaussian = quadPosition * 2.0;
    gl_Position = vec4(
        clip.xy / clip.w + offset / viewport * 2.0,
        clip.z / clip.w, 1.0);
#else
    mat3 rotationMatrix = quaternionToMatrix(rotation);
    mat3 scaled = mat3(
        rotationMatrix[0] * scale.x,
        rotationMatrix[1] * scale.y,
        rotationMatrix[2] * scale.z);
    mat3 covariance3d = scaled * transpose(scaled);

    // Focal lengths in pixels, recovered from the projection matrix so this
    // follows whatever FOV and viewport the camera currently has.
    float fx = projectionMatrix[0][0] * viewport.x * 0.5;
    float fy = projectionMatrix[1][1] * viewport.y * 0.5;

    // Jacobian of the perspective projection at this splat's camera position.
    mat3 jacobian = mat3(
        fx / cam.z, 0.0, -(fx * cam.x) / (cam.z * cam.z),
        0.0, fy / cam.z, -(fy * cam.y) / (cam.z * cam.z),
        0.0, 0.0, 0.0);
    mat3 world = transpose(mat3(modelView));
    mat3 transform = world * jacobian;
    mat3 covariance2d = transpose(transform) * covariance3d * transform;

    // A low-pass term keeps sub-pixel splats from vanishing entirely, which is
    // what 3DGS calls the dilation filter.
    float a = covariance2d[0][0] + 0.3;
    float b = covariance2d[0][1];
    float c = covariance2d[1][1] + 0.3;
    float mid = 0.5 * (a + c);
    float radius = length(vec2(0.5 * (a - c), b));
    float lambda1 = mid + radius;
    float lambda2 = max(mid - radius, 0.1);
    // A splat smaller than a pixel contributes nothing; skip its quad.
    if (lambda1 < 0.02) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        vColor = vec4(0.0);
        vGaussian = vec2(0.0);
        return;
    }

    // The eigenvector of the 2D covariance, guarded against the isotropic case.
    //
    // This guard is essential, not defensive. For a near-isotropic splat the
    // off-diagonal b tends to 0 AND lambda1 tends to a, so the unguarded
    // normalize(vec2(b, lambda1 - a)) is normalize(vec2(0, 0)) = NaN. A NaN
    // gl_Position produces no primitive at all, silently: every intermediate
    // value reads correct, 100k triangles are submitted, and not one pixel is
    // shaded. Vega's Gaussians are close to isotropic (log scales within 0.3 of
    // each other), so this is the common case here, not a rare one.
    //
    // When the covariance really is isotropic, any orthonormal basis is a
    // correct pair of axes, so falling back to the x-axis loses nothing.
    vec2 eigenDirection = vec2(b, lambda1 - a);
    float eigenLength = length(eigenDirection);
    vec2 majorAxis = eigenLength > 1e-6
        ? eigenDirection / eigenLength
        : vec2(1.0, 0.0);
    // Clamped so one degenerate splat cannot ask for a screen-filling quad.
    vec2 axis1 = min(sqrt(2.0 * lambda1), 1024.0) * majorAxis;
    vec2 axis2 = min(sqrt(2.0 * lambda2), 1024.0) * vec2(majorAxis.y, -majorAxis.x);

    vec2 offset = quadPosition.x * axis1 + quadPosition.y * axis2;
    vColor = vec4(color.rgb, opacity);
    // Two sigma across the quad, matching the 4.0 cutoff in the fragment stage.
    vGaussian = quadPosition * 2.0;

    gl_Position = vec4(
        clip.xy / clip.w + offset / viewport * 2.0,
        clip.z / clip.w, 1.0);
#endif
}
`;

const FRAGMENT_SHADER = /* glsl */`
precision highp float;

in vec4 vColor;
in vec2 vGaussian;

out vec4 fragColor;

void main() {
    float power = -dot(vGaussian, vGaussian);
    // Beyond two sigma the contribution is under 2%; discarding there saves
    // most of the fill cost with no visible change.
    if (power < -4.0) discard;
    float alpha = exp(0.5 * power) * vColor.a;
    if (alpha < 1.0 / 255.0) discard;
    // Premultiplied alpha, to match the ONE / ONE_MINUS_SRC_ALPHA blend.
    fragColor = vec4(vColor.rgb * alpha, alpha);
}
`;

/**
 * One object's splat cloud: a data texture plus a sortable index attribute.
 */
class SplatObject {
    /**
     * @param {object} args
     * @param {number} args.capacity
     * @param {'isotropic'|'anisotropic'} [args.mode] quad sizing. Isotropic is
     *   the default; see the ISOTROPIC_SPLATS note in the vertex shader.
     */
    constructor({ capacity, mode = 'isotropic' }) {
        this.mode = mode;
        this._bound = false;
        this.capacity = 0;
        this.count = 0;
        this.geometry = new THREE.InstancedBufferGeometry();

        // A unit quad, two triangles, shared by every instance.
        this.geometry.setAttribute('quadPosition',
            new THREE.BufferAttribute(new Float32Array([
                -1, -1, 1, -1, 1, 1, -1, 1
            ]), 2));
        this.geometry.setIndex([0, 1, 2, 0, 2, 3]);

        this.material = new THREE.RawShaderMaterial({
            vertexShader: VERTEX_SHADER,
            fragmentShader: FRAGMENT_SHADER,
            glslVersion: THREE.GLSL3,
            defines: mode === 'isotropic' ? { ISOTROPIC_SPLATS: '' } : {},
            uniforms: {
                splatData: { value: null },
                splatTextureSize: { value: new THREE.Vector2(1, 1) },
                viewport: { value: new THREE.Vector2(1, 1) },
                splatScale: { value: 1.0 }
            },
            transparent: true,
            // 3DGS blending: the ordering carries the depth information, so
            // depth test and write must both be off or splats hard-clip each
            // other instead of blending.
            depthTest: false,
            depthWrite: false,
            blending: THREE.CustomBlending,
            blendSrc: THREE.OneFactor,
            blendDst: THREE.OneMinusSrcAlphaFactor,
            blendSrcAlpha: THREE.OneFactor,
            blendDstAlpha: THREE.OneMinusSrcAlphaFactor
        });

        this.mesh = new THREE.Mesh(this.geometry, this.material);
        this.mesh.frustumCulled = false;   // bounds change every frame
        // Hidden until setFrame supplies data. Not cosmetic: a visible mesh is
        // bound by the render loop, and being bound at a placeholder capacity
        // is what caps the instance count forever (see _allocate).
        this.mesh.visible = false;
        this._allocate(Math.max(capacity, 1));

        // Sort scratch, reused across frames.
        this._depths = new Float32Array(0);
        this._counts = new Uint32Array(SORT_BUCKETS);
        this._sorted = new Float32Array(0);
    }

    /**
     * Grow the data texture and the sort index to hold `capacity` splats.
     *
     * Growing AFTER the geometry has been drawn once is a trap. Three caches
     * `_maxInstanceCount` from the instanced attributes the first time it sets
     * up the vertex bindings, and the draw count is
     * `min(geometry.instanceCount, _maxInstanceCount)`. Swapping in a larger
     * `splatIndex` attribute later does not always reset that cache, so a
     * geometry first bound at capacity 1 keeps drawing ONE instance no matter
     * what `instanceCount` says -- two triangles instead of two hundred
     * thousand, which looks exactly like an empty canvas.
     *
     * Measured: identical draw calls, 213,224 triangles when the growth
     * happened before the first bind and 4 when it happened after, varying
     * per page load. Callers therefore pass the real capacity up front (see
     * `VegaClient`), and the mesh stays hidden until it has a frame so it
     * cannot be bound at a placeholder size.
     */
    _allocate(capacity) {
        if (capacity <= this.capacity) return;
        if (this._bound) {
            // Not reachable when the caller sized this correctly, and a loud
            // failure beats silently rendering one splat out of sixty thousand.
            throw new Error(
                `SplatObject grew from ${this.capacity} to ${capacity} after it `
                + 'was first drawn; construct it with the maximum splat count '
                + 'for the sequence instead');
        }
        this.capacity = capacity;
        const texels = capacity * TEXELS_PER_SPLAT;
        const height = Math.ceil(texels / TEXTURE_WIDTH);
        this._textureData = new Float32Array(TEXTURE_WIDTH * height * 4);
        this._texture?.dispose();
        this._texture = new THREE.DataTexture(
            this._textureData, TEXTURE_WIDTH, height,
            THREE.RGBAFormat, THREE.FloatType);
        this._texture.needsUpdate = true;
        this.material.uniforms.splatData.value = this._texture;
        this.material.uniforms.splatTextureSize.value.set(TEXTURE_WIDTH, height);

        this._indexAttribute = new THREE.InstancedBufferAttribute(
            new Float32Array(capacity), 1);
        this._indexAttribute.setUsage(THREE.DynamicDrawUsage);
        this.geometry.setAttribute('splatIndex', this._indexAttribute);
        this._depths = new Float32Array(capacity);
        this._sorted = new Float32Array(capacity);
    }

    /** Upload a decoded VGS frame. */
    setFrame(frame) {
        this._allocate(frame.count);
        this.count = frame.count;
        const data = this._textureData;
        for (let i = 0; i < frame.count; i++) {
            const base = i * TEXELS_PER_SPLAT * 4;
            data[base] = frame.positions[i * 3];
            data[base + 1] = frame.positions[i * 3 + 1];
            data[base + 2] = frame.positions[i * 3 + 2];
            data[base + 3] = frame.opacities[i];

            data[base + 4] = frame.scales[i * 3];
            data[base + 5] = frame.scales[i * 3 + 1];
            data[base + 6] = frame.scales[i * 3 + 2];
            data[base + 7] = 0;

            data[base + 8] = frame.rotations[i * 4];
            data[base + 9] = frame.rotations[i * 4 + 1];
            data[base + 10] = frame.rotations[i * 4 + 2];
            data[base + 11] = frame.rotations[i * 4 + 3];

            data[base + 12] = frame.colors[i * 3];
            data[base + 13] = frame.colors[i * 3 + 1];
            data[base + 14] = frame.colors[i * 3 + 2];
            data[base + 15] = 0;
        }
        this._texture.needsUpdate = true;
        this._positions = frame.positions;
        this.geometry.instanceCount = frame.count;
        this._sortedForKey = null;
        this.mesh.visible = frame.count > 0;
        // From here the geometry may be bound at this capacity, so growth is
        // no longer safe.
        this._bound = this._bound || frame.count > 0;
    }

    /**
     * Order splats back to front for the given camera.
     *
     * Counting sort over depth quantised to 16 bits. Re-sorting only when the
     * camera or the frame actually changed keeps a static view free.
     */
    sort(camera) {
        if (this.count === 0 || !this._positions) return false;
        const matrix = camera.matrixWorldInverse.elements;
        // Third row of the view matrix gives camera-space z directly.
        const m2 = matrix[2], m6 = matrix[6], m10 = matrix[10], m14 = matrix[14];
        const key = `${m2.toFixed(5)},${m6.toFixed(5)},${m10.toFixed(5)},`
            + `${m14.toFixed(3)},${this.count}`;
        if (this._sortedForKey === key) return false;
        this._sortedForKey = key;

        const depths = this._depths;
        let min = Infinity;
        let max = -Infinity;
        for (let i = 0; i < this.count; i++) {
            const z = m2 * this._positions[i * 3]
                + m6 * this._positions[i * 3 + 1]
                + m10 * this._positions[i * 3 + 2] + m14;
            depths[i] = z;
            if (z < min) min = z;
            if (z > max) max = z;
        }
        const span = max - min || 1;
        const counts = this._counts.fill(0);
        const scale = (SORT_BUCKETS - 1) / span;
        // Camera-space z is negative in front of the camera, so ASCENDING z is
        // far to near — exactly the back-to-front order the blend needs.
        for (let i = 0; i < this.count; i++) {
            counts[((depths[i] - min) * scale) | 0]++;
        }
        let running = 0;
        for (let bucket = 0; bucket < SORT_BUCKETS; bucket++) {
            const value = counts[bucket];
            counts[bucket] = running;
            running += value;
        }
        const order = this._indexAttribute.array;
        for (let i = 0; i < this.count; i++) {
            order[counts[((depths[i] - min) * scale) | 0]++] = i;
        }
        this._indexAttribute.needsUpdate = true;
        this._indexAttribute.updateRanges = [{ start: 0, count: this.count }];
        return true;
    }

    dispose() {
        this.geometry.dispose();
        this.material.dispose();
        this._texture?.dispose();
    }
}

module.exports = {
    SplatObject, VERTEX_SHADER, FRAGMENT_SHADER,
    TEXELS_PER_SPLAT, TEXTURE_WIDTH, SORT_BUCKETS
};
