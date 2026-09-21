'use strict';

/**
 * Texture clip decode: MP4 demux (mp4box) + WebCodecs VideoDecoder.
 *
 * WebCodecs rather than an HTMLVideoElement on purpose. Geometry arrives as one
 * Draco mesh per frame, and a presented frame must pair frame N of the texture
 * with frame N of the geometry. A `<video>` element gives you `currentTime`
 * seeking, which is not frame-accurate and drifts — the texture would slide
 * against the mesh. `VideoDecoder` hands back discrete `VideoFrame`s in
 * decode order, so the pairing is exact.
 *
 * Codec note: nothing here is codec-specific. The codec string and the
 * parameter sets both come from the file -- `track.codec` and whichever of
 * avcC/hvcC the sample entry carries -- so the same path decodes the H.264
 * corpus (`avc1.64001f`, avcC) and the HEVC one (`hvc1.1.6.L90.90`, hvcC).
 * Which corpus is served is a server-side choice (VS4D_COMPRESSED_ROOT).
 *
 * Serve H.264 for a browser audience. HEVC is Safari-yes / Firefox-no /
 * Chrome-only-with-hardware, and measured against this corpus H.264 costs just
 * 1.003x the bitrate at matched quality, so the ladder barely moves.
 * `probeCodecSupport` is what tells a user why a page shows geometry with no
 * texture when the browser cannot decode what is being served.
 */

const MP4Box = require('mp4box');

/**
 * Ask the browser whether it can decode a codec string before committing to it.
 * Returns a diagnosis rather than throwing, so the page can degrade to
 * untextured geometry and say why.
 */
async function probeCodecSupport(codec) {
    if (typeof VideoDecoder === 'undefined') {
        return { supported: false, reason: 'WebCodecs is unavailable in this browser' };
    }
    try {
        const support = await VideoDecoder.isConfigSupported({ codec });
        return support.supported
            ? { supported: true }
            : { supported: false, reason: `no decoder for ${codec}` };
    } catch (err) {
        return { supported: false, reason: `${codec}: ${err.message}` };
    }
}

/**
 * Pull the video track's codec string, dimensions and decoder description out
 * of an MP4.
 *
 * The description matters: HEVC and H.264 both need their parameter sets
 * (hvcC / avcC) handed to the decoder up front, and mp4box is what exposes
 * them. Without it the decoder rejects the very first chunk.
 */
function demux(buffer) {
    return new Promise((resolve, reject) => {
        const file = MP4Box.createFile();
        const samples = [];
        let info = null;

        file.onError = error => reject(new Error(`mp4 demux failed: ${error}`));

        file.onReady = readyInfo => {
            const track = readyInfo.videoTracks?.[0];
            if (!track) {
                reject(new Error('mp4 has no video track'));
                return;
            }
            info = {
                codec: track.codec,
                width: track.video.width,
                height: track.video.height,
                frameCount: track.nb_samples,
                timescale: track.timescale,
                description: parameterSets(file, track)
            };
            file.setExtractionOptions(track.id, null, { nbSamples: track.nb_samples });
            file.start();
        };

        file.onSamples = (_id, _user, sampleList) => {
            for (const sample of sampleList) {
                samples.push({
                    type: sample.is_sync ? 'key' : 'delta',
                    timestamp: (sample.cts * 1e6) / sample.timescale,
                    duration: (sample.duration * 1e6) / sample.timescale,
                    data: sample.data
                });
            }
            if (info && samples.length >= info.frameCount) {
                resolve({ info, samples });
            }
        };

        // mp4box requires the buffer to carry its offset.
        const view = buffer instanceof ArrayBuffer ? buffer : buffer.buffer;
        const tagged = view.slice(0);
        tagged.fileStart = 0;
        file.appendBuffer(tagged);
        file.flush();

        // A single-group clip can complete inside flush(); if not, onSamples
        // resolves. Guard against a file that yields neither.
        if (info && samples.length >= info.frameCount) resolve({ info, samples });
    });
}

/** Extract avcC/hvcC parameter sets as the decoder description. */
function parameterSets(file, track) {
    const entry = file.moov?.traks
        ?.find(t => t.tkhd.track_id === track.id)
        ?.mdia?.minf?.stbl?.stsd?.entries?.[0];
    const box = entry?.avcC || entry?.hvcC || entry?.vpcC || entry?.av1C;
    if (!box) return null;
    // mp4box writes boxes including an 8-byte header; the decoder wants the
    // payload only.
    const stream = new MP4Box.DataStream(
        undefined, 0, MP4Box.DataStream.BIG_ENDIAN);
    box.write(stream);
    return new Uint8Array(stream.buffer, 8);
}

/**
 * Decode a texture clip to an array of `VideoFrame`s, index-aligned to the
 * clip's frames.
 *
 * The caller OWNS the returned frames and must `close()` each one. A VideoFrame
 * pins a GPU/system buffer that garbage collection will not reclaim, so leaking
 * them exhausts the decoder within a few segments.
 *
 * @param {ArrayBuffer} buffer the .mp4 bytes
 * @param {object} [options]
 * @param {number} [options.timeoutMs] give up rather than hang the pipeline
 * @returns {Promise<{frames: VideoFrame[], info: object}>}
 */
async function decodeTextureClip(buffer, { timeoutMs = 15000 } = {}) {
    const { info, samples } = await demux(buffer);

    const support = await probeCodecSupport(info.codec);
    if (!support.supported) {
        const error = new Error(support.reason);
        error.code = 'CODEC_UNSUPPORTED';
        error.codec = info.codec;
        throw error;
    }

    const frames = [];
    let settle;
    const done = new Promise((resolve, reject) => {
        settle = { resolve, reject };
    });

    const decoder = new VideoDecoder({
        output: frame => {
            frames.push(frame);
            if (frames.length >= info.frameCount) settle.resolve();
        },
        error: err => settle.reject(err)
    });

    decoder.configure({
        codec: info.codec,
        codedWidth: info.width,
        codedHeight: info.height,
        ...(info.description ? { description: info.description } : {}),
        // Latency is irrelevant here and quality is not: this is a whole clip
        // decoded ahead of presentation.
        optimizeForLatency: false
    });

    for (const sample of samples) {
        decoder.decode(new EncodedVideoChunk(sample));
    }

    const timer = setTimeout(
        () => settle.reject(new Error(`texture decode timed out after ${timeoutMs}ms`)),
        timeoutMs);
    try {
        await decoder.flush();
        await done;
    } finally {
        clearTimeout(timer);
        try { decoder.close(); } catch (_) { /* already closed on error */ }
    }

    return { frames, info };
}

/** Release every frame of a decoded clip. */
function closeTextureClip(clip) {
    for (const frame of clip?.frames || []) {
        try { frame.close(); } catch (_) { /* already closed */ }
    }
}

/** Decoded bytes a clip occupies, for the decode cache's budget. */
function textureClipBytes(info, frameCount) {
    // yuv420p: w*h*1.5 per frame, however the frame is actually stored.
    return Math.round(info.width * info.height * 1.5 * frameCount);
}

module.exports = {
    decodeTextureClip,
    closeTextureClip,
    textureClipBytes,
    probeCodecSupport,
    demux
};
