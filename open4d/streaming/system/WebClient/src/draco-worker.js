'use strict';

/**
 * Draco decode worker.
 *
 * A segment is up to 60 Draco meshes per object. Decoding them on the main
 * thread would compete with the render loop for exactly the window in which
 * frames must be presented, so every decode happens here and only typed arrays
 * cross back — transferred, not copied.
 *
 * Loads the official Draco JS/WASM decoder vendored under ../vendor/draco.
 * That build is upstream's, unmodified.
 *
 * Handles BOTH Draco geometry kinds, because the two pipelines differ:
 *
 *   TRIANGULAR_MESH  the mesh ladder (`compress_geometry.py` -> textured OBJs)
 *   POINT_CLOUD      every V4DS baseline (`draco_encoder -point_cloud -qp 11
 *                    -qg 8`), carrying POSITION plus 8-bit COLOR
 *
 * The kind is read from the payload rather than configured, so one worker
 * serves both the mesh client and the baseline client.
 *
 * Protocol:
 *   in  { id, buffers: ArrayBuffer[] }   frames in order
 *   out { id, frames: [frame|null] } | { id, error }
 *     mesh  frame = { kind: 'mesh',  positions, normals, uvs, indices }
 *     cloud frame = { kind: 'cloud', positions, colors }
 *
 * A frame that fails to decode comes back as null in the array rather than
 * failing the whole clip: download-plan already tolerates gaps, and the
 * renderer holds the previous frame for one.
 */

let decoderModulePromise = null;

function loadDecoder(vendorBase) {
    if (decoderModulePromise) return decoderModulePromise;
    decoderModulePromise = new Promise((resolve, reject) => {
        try {
            self.importScripts(`${vendorBase}/draco_decoder.js`);
        } catch (err) {
            reject(new Error(`could not load draco_decoder.js: ${err.message}`));
            return;
        }
        // The emscripten build resolves its .wasm through locateFile.
        self.DracoDecoderModule({
            locateFile: file => `${vendorBase}/${file}`
        }).then(resolve, reject);
    });
    return decoderModulePromise;
}

/**
 * Decode one Draco buffer into plain typed arrays.
 *
 * Every Draco object must be explicitly destroyed: the WASM heap is not
 * garbage-collected from JS, and leaking one mesh per frame at 30 fps exhausts
 * it within a minute.
 */
function decodeGeometry(draco, decoder, buffer) {
    const dracoBuffer = new draco.DecoderBuffer();
    dracoBuffer.Init(new Int8Array(buffer), buffer.byteLength);

    let geometry = null;
    try {
        const geometryType = decoder.GetEncodedGeometryType(dracoBuffer);

        if (geometryType === draco.TRIANGULAR_MESH) {
            geometry = new draco.Mesh();
            const status = decoder.DecodeBufferToMesh(dracoBuffer, geometry);
            if (!status.ok() || geometry.ptr === 0) {
                throw new Error(status.error_msg() || 'mesh decode failed');
            }
            return {
                kind: 'mesh',
                positions: readFloatAttribute(
                    draco, decoder, geometry, draco.POSITION, 3),
                normals: readFloatAttribute(
                    draco, decoder, geometry, draco.NORMAL, 3),
                uvs: readFloatAttribute(
                    draco, decoder, geometry, draco.TEX_COORD, 2),
                indices: readIndices(draco, decoder, geometry)
            };
        }

        if (geometryType === draco.POINT_CLOUD) {
            geometry = new draco.PointCloud();
            const status = decoder.DecodeBufferToPointCloud(dracoBuffer, geometry);
            if (!status.ok() || geometry.ptr === 0) {
                throw new Error(status.error_msg() || 'point cloud decode failed');
            }
            return {
                kind: 'cloud',
                positions: readFloatAttribute(
                    draco, decoder, geometry, draco.POSITION, 3),
                // Colours are quantised to 8 bits by the encoder's -qg 8, so
                // read them as bytes. Reading them as floats yields 0..255
                // values that then have to be rediscovered downstream.
                colors: readUint8Attribute(
                    draco, decoder, geometry, draco.COLOR, 3)
            };
        }

        throw new Error(`unsupported draco geometry type ${geometryType}`);
    } finally {
        if (geometry) draco.destroy(geometry);
        draco.destroy(dracoBuffer);
    }
}

function readFloatAttribute(draco, decoder, geometry, attributeType, components) {
    const id = decoder.GetAttributeId(geometry, attributeType);
    if (id < 0) return null;
    const attribute = decoder.GetAttribute(geometry, id);
    const count = geometry.num_points();
    const array = new draco.DracoFloat32Array();
    try {
        decoder.GetAttributeFloatForAllPoints(geometry, attribute, array);
        const out = new Float32Array(count * components);
        for (let i = 0; i < out.length; i++) out[i] = array.GetValue(i);
        return out;
    } finally {
        draco.destroy(array);
    }
}

function readUint8Attribute(draco, decoder, geometry, attributeType, components) {
    const id = decoder.GetAttributeId(geometry, attributeType);
    if (id < 0) return null;
    const attribute = decoder.GetAttribute(geometry, id);
    const count = geometry.num_points();
    const array = new draco.DracoUInt8Array();
    try {
        decoder.GetAttributeUInt8ForAllPoints(geometry, attribute, array);
        const out = new Uint8Array(count * components);
        for (let i = 0; i < out.length; i++) out[i] = array.GetValue(i);
        return out;
    } finally {
        draco.destroy(array);
    }
}

function readIndices(draco, decoder, mesh) {
    const faceCount = mesh.num_faces();
    const out = new Uint32Array(faceCount * 3);
    const face = new draco.DracoInt32Array();
    try {
        for (let i = 0; i < faceCount; i++) {
            decoder.GetFaceFromMesh(mesh, i, face);
            out[i * 3] = face.GetValue(0);
            out[i * 3 + 1] = face.GetValue(1);
            out[i * 3 + 2] = face.GetValue(2);
        }
        return out;
    } finally {
        draco.destroy(face);
    }
}

self.onmessage = async event => {
    const { id, buffers, vendorBase } = event.data;
    try {
        const draco = await loadDecoder(vendorBase || '/web/vendor/draco');
        const decoder = new draco.Decoder();
        const frames = [];
        const transfer = [];
        try {
            for (const buffer of buffers) {
                if (!buffer) { frames.push(null); continue; }
                try {
                    const frame = decodeGeometry(draco, decoder, buffer);
                    frames.push(frame);
                    // Transfer rather than copy: a 60-frame clip is tens of MB.
                    for (const key of ['positions', 'normals', 'uvs', 'indices',
                                       'colors']) {
                        if (frame[key]) transfer.push(frame[key].buffer);
                    }
                } catch (_) {
                    // One bad frame is a gap the renderer can hold through, not
                    // a reason to discard the whole object-segment.
                    frames.push(null);
                }
            }
        } finally {
            draco.destroy(decoder);
        }
        self.postMessage({ id, frames }, transfer);
    } catch (err) {
        self.postMessage({ id, error: err.message });
    }
};
