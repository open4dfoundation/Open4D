'use strict';

/**
 * Camera pose conversion: WebGL/Three.js camera -> Open3D
 * `PinholeCameraParameters`.
 *
 * This is not a convenience. What the client POSTs to `/api/viewpoint` is
 * written to disk verbatim and read back by
 * `o3d.io.read_pinhole_camera_parameters` (see
 * `vstream/ladder/ladder_service.py` and `create_ladder.py`), so the JSON must
 * be exactly that schema or the ladder cannot parse the pose at all. When it
 * cannot, `normalize_weights` necessarily returns equal weights and the ladder
 * silently stops being viewpoint-aware — which is the entire contribution being
 * measured. A wrong pose here does not crash anything; it quietly invalidates
 * the experiment.
 *
 * Two conventions have to be bridged:
 *
 * 1. AXES. Three.js cameras look down -Z with +Y up. Open3D's camera frame is
 *    OpenCV-style: +X right, +Y DOWN, +Z FORWARD. So the world->camera matrix
 *    gets its Y and Z rows negated. The captured viewpoint files show this
 *    directly — their extrinsic diagonal is roughly (+1, -1, -1).
 *
 * 2. UNITS. `vstream/config.py` is explicit: "Baked OBJ/Draco coordinates are
 *    metres, while Open3D camera extrinsics and the quality model's
 *    mean-distance feature are millimetres", and
 *    VIEW_RAYCAST_UNITS_PER_METER defaults to 1000. The captured files agree —
 *    their translations are in the thousands. A browser scene in metres must
 *    therefore scale its translation by 1000, or the server raycasts from
 *    ~4 mm away, every ray misses, and the weights come back uniform.
 *
 *    Only the translation scales. The extrinsic maps world points to camera
 *    space as `p_cam = R*p_world + t` with `t = -R*C`, so re-expressing the
 *    same pose in a millimetre world gives `t_mm = 1000 * t_m` and leaves R
 *    untouched.
 */

/** Open3D stores the principal point at the pixel-grid centre. */
function principalPoint(size) {
    return (size - 1) / 2;
}

/**
 * Vertical focal length in pixels for a vertical field of view.
 *
 * Cross-check against the captured corpus: a 60 degree vertical FOV at 1920 px
 * gives 960 / tan(30 deg) = 1662.7687752661222, which is exactly the value in
 * `system/Client/viewpoints/view_00.json`.
 */
function focalLengthPx(fovDegrees, height) {
    const fovRadians = (fovDegrees * Math.PI) / 180;
    return (height / 2) / Math.tan(fovRadians / 2);
}

/**
 * Build an Open3D `PinholeCameraParameters` object.
 *
 * @param {object} args
 * @param {number[]} args.viewMatrix world->camera matrix, COLUMN-major, 16
 *   elements. In Three.js this is `camera.matrixWorldInverse.elements`, which is
 *   already column-major.
 * @param {number} args.fovDegrees vertical field of view
 * @param {number} args.width viewport width in pixels
 * @param {number} args.height viewport height in pixels
 * @param {number} [args.unitsPerMeter=1000] world units per metre on the
 *   SERVER side; must match config.VIEW_RAYCAST_UNITS_PER_METER
 * @returns {object} JSON-ready PinholeCameraParameters
 */
function toOpen3DCameraParameters({
    viewMatrix, fovDegrees, width, height, unitsPerMeter = 1000
}) {
    if (!Array.isArray(viewMatrix) && !(viewMatrix instanceof Float32Array)
        && !(viewMatrix instanceof Float64Array)) {
        throw new TypeError('viewMatrix must be an array of 16 numbers');
    }
    if (viewMatrix.length !== 16) {
        throw new RangeError(`viewMatrix must have 16 elements, got ${viewMatrix.length}`);
    }
    if (!(width > 0) || !(height > 0)) {
        throw new RangeError('width and height must be positive');
    }
    if (!(fovDegrees > 0) || fovDegrees >= 180) {
        throw new RangeError(`fovDegrees out of range: ${fovDegrees}`);
    }

    // Column-major indexing: element(row, col) = viewMatrix[col * 4 + row].
    // Negating rows 1 and 2 applies diag(1, -1, -1, 1) on the left, which is
    // the Y-up/-Z-forward -> Y-down/+Z-forward change of basis.
    const extrinsic = new Array(16);
    for (let col = 0; col < 4; col++) {
        for (let row = 0; row < 4; row++) {
            const index = col * 4 + row;
            const flip = (row === 1 || row === 2) ? -1 : 1;
            // `+ 0` normalizes -0 away. Negating a zero row entry yields -0,
            // which is numerically harmless but does not survive a JSON round
            // trip, so it would make the emitted pose fail an equality check
            // against itself.
            extrinsic[index] = flip * viewMatrix[index] + 0;
        }
    }
    // Translation is the last column; scale it into the server's world units.
    extrinsic[12] *= unitsPerMeter;
    extrinsic[13] *= unitsPerMeter;
    extrinsic[14] *= unitsPerMeter;

    const focal = focalLengthPx(fovDegrees, height);

    return {
        class_name: 'PinholeCameraParameters',
        extrinsic,
        intrinsic: {
            height,
            // Column-major 3x3: [fx, 0, 0, 0, fy, 0, cx, cy, 1].
            // Square pixels: Three.js takes a vertical FOV and derives the
            // horizontal extent from the aspect ratio, so fx == fy.
            intrinsic_matrix: [
                focal, 0, 0,
                0, focal, 0,
                principalPoint(width), principalPoint(height), 1
            ],
            width
        },
        version_major: 1,
        version_minor: 0
    };
}

/**
 * Convenience wrapper for a Three.js PerspectiveCamera.
 *
 * `updateMatrixWorld` then `matrixWorldInverse` rather than trusting whatever
 * the render loop last computed: the pose is read on the segment tick, which is
 * not synchronised with a frame.
 */
function fromThreeCamera(camera, { width, height, unitsPerMeter = 1000 }) {
    camera.updateMatrixWorld();
    camera.updateProjectionMatrix();
    return toOpen3DCameraParameters({
        viewMatrix: Array.from(camera.matrixWorldInverse.elements),
        fovDegrees: camera.fov,
        width,
        height,
        unitsPerMeter
    });
}

/**
 * Map a world point into the camera frame described by these parameters.
 * Used by the tests to assert the axis convention: a point the camera is
 * looking at must land at POSITIVE z.
 */
function projectToCameraSpace(parameters, worldPointMetres) {
    const e = parameters.extrinsic;
    const scale = 1000;   // parameters are in the server's units
    const [x, y, z] = worldPointMetres.map(v => v * scale);
    return [
        e[0] * x + e[4] * y + e[8] * z + e[12],
        e[1] * x + e[5] * y + e[9] * z + e[13],
        e[2] * x + e[6] * y + e[10] * z + e[14]
    ];
}

module.exports = {
    toOpen3DCameraParameters,
    fromThreeCamera,
    focalLengthPx,
    principalPoint,
    projectToCameraSpace
};
