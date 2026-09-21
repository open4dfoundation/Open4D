/* The decode worker.
 *
 * Parsing a frame is the one part of playback that is unavoidably expensive and
 * unavoidably synchronous: measured on this repository's content, 16.5 ms for a
 * 3DGS PLY, 32.1 ms for a 439k-Gaussian .splat, 17.8 ms for a Draco mesh. On
 * the main thread each of those is a frame's worth of `requestAnimationFrame`
 * missed, so playback stuttered in proportion to how much geometry it was
 * showing -- and the panes that were not decoding stuttered too, because there
 * is only one main thread.
 *
 * So the codecs live here and nowhere else. Not shared with the page: sharing
 * would mean either duplicating them, which drifts, or a build step, which this
 * client does not have. The page holds renderers and UI; this holds every byte
 * that has to be turned into geometry.
 *
 * `decodeImage` is the exception and stays on the main thread. It needs `Image`,
 * and a browser already decodes an image off the main thread, so moving it here
 * would buy nothing and cost the ImageBitmap-to-<img> problem.
 *
 * Results come back with their buffers *transferred*, not copied -- a 4.3 MB
 * frame parses to several megabytes of typed arrays, and copying those across
 * the boundary would give back much of what the worker saved.
 */

const SH_C0 = 0.28209479177387814;

/* ------------------------------------------------------------------ PLY ---
 * The 3DGS PLY: a binary_little_endian vertex element of float32 properties
 * named x y z, nx ny nz, f_dc_*, f_rest_*, opacity, scale_*, rot_*. Values are
 * raw, so opacity is a logit, scale a log, and rot an unnormalised quaternion.
 * f_rest is ignored: a view-dependent band cannot be evaluated without knowing
 * the direction the exporter baked colour from, and the exports this viewer is
 * built for are degree 0 anyway.
 */
function parsePly(buffer) {
  const bytes = new Uint8Array(buffer);
  const limit = Math.min(bytes.length, 1 << 16);
  const head = new TextDecoder("ascii").decode(bytes.subarray(0, limit));
  const marker = head.indexOf("end_header");
  if (marker < 0) throw new Error("not a PLY: no end_header in the first 64 KiB");
  const dataStart = head.indexOf("\n", marker) + 1;
  const lines = head.slice(0, marker).split("\n").map((line) => line.trim());

  if (!lines.some((line) => line.startsWith("format binary_little_endian"))) {
    throw new Error("only binary_little_endian PLY is supported");
  }
  let count = 0, inVertex = false;
  const names = [];
  for (const line of lines) {
    if (line.startsWith("element ")) {
      const parts = line.split(/\s+/);
      inVertex = parts[1] === "vertex";
      if (inVertex) count = parseInt(parts[2], 10);
    } else if (line.startsWith("property ") && inVertex) {
      names.push(line.split(/\s+/)[2]);
    }
  }
  if (!count) throw new Error("PLY declares no vertices");

  const stride = names.length;
  // Copied rather than viewed in place: the header length is whatever it is, so
  // `dataStart` is rarely a multiple of 4 and a Float32Array cannot be created
  // at an unaligned offset.
  const table = new Float32Array(buffer.slice(dataStart, dataStart + count * stride * 4));
  const at = {};
  names.forEach((name, index) => { at[name] = index; });
  for (const need of ["x", "y", "z", "opacity", "scale_0", "rot_0", "f_dc_0"]) {
    if (at[need] === undefined) throw new Error(`PLY is missing property ${need}`);
  }

  // Three RGBA32F texels per Gaussian: (xyz, opacity), (c00 c01 c02 c11),
  // (c12 c22 _ _). Colour rides in a separate RGBA8 texture so neither has to
  // be bit-packed.
  const data = new Float32Array(count * 12);
  const colors = new Uint8Array(count * 4);
  const positions = new Float32Array(count * 3);

  for (let i = 0; i < count; i++) {
    const row = i * stride;
    const px = table[row + at.x], py = table[row + at.y], pz = table[row + at.z];
    positions[i * 3] = px; positions[i * 3 + 1] = py; positions[i * 3 + 2] = pz;

    const sx = Math.exp(table[row + at.scale_0]);
    const sy = Math.exp(table[row + at.scale_1]);
    const sz = Math.exp(table[row + at.scale_2]);

    let qw = table[row + at.rot_0], qx = table[row + at.rot_1];
    let qy = table[row + at.rot_2], qz = table[row + at.rot_3];
    const norm = Math.hypot(qw, qx, qy, qz) || 1;
    qw /= norm; qx /= norm; qy /= norm; qz /= norm;

    // 3DGS's build_rotation, with (w, x, y, z) as stored.
    const r00 = 1 - 2 * (qy * qy + qz * qz), r01 = 2 * (qx * qy - qw * qz), r02 = 2 * (qx * qz + qw * qy);
    const r10 = 2 * (qx * qy + qw * qz), r11 = 1 - 2 * (qx * qx + qz * qz), r12 = 2 * (qy * qz - qw * qx);
    const r20 = 2 * (qx * qz - qw * qy), r21 = 2 * (qy * qz + qw * qx), r22 = 1 - 2 * (qx * qx + qy * qy);

    // Sigma = (R diag(s)) (R diag(s))^T, upper triangle only.
    const m00 = r00 * sx, m01 = r01 * sy, m02 = r02 * sz;
    const m10 = r10 * sx, m11 = r11 * sy, m12 = r12 * sz;
    const m20 = r20 * sx, m21 = r21 * sy, m22 = r22 * sz;

    const base = i * 12;
    data[base] = px; data[base + 1] = py; data[base + 2] = pz;
    data[base + 3] = 1 / (1 + Math.exp(-table[row + at.opacity]));
    data[base + 4] = m00 * m00 + m01 * m01 + m02 * m02;
    data[base + 5] = m00 * m10 + m01 * m11 + m02 * m12;
    data[base + 6] = m00 * m20 + m01 * m21 + m02 * m22;
    data[base + 7] = m10 * m10 + m11 * m11 + m12 * m12;
    data[base + 8] = m10 * m20 + m11 * m21 + m12 * m22;
    data[base + 9] = m20 * m20 + m21 * m21 + m22 * m22;

    for (let c = 0; c < 3; c++) {
      const value = 0.5 + SH_C0 * table[row + at["f_dc_" + c]];
      colors[i * 4 + c] = Math.max(0, Math.min(255, Math.round(value * 255)));
    }
    colors[i * 4 + 3] = 255;
  }
  return { count, data, colors, positions };
}

/* One frame of `.splat`: 32 bytes per Gaussian, the quantised delivery form
 * (see gs_tools/io/splat.py for the byte layout and what it costs).
 *
 * The values are stored ALREADY ACTIVATED, unlike a PLY -- scales are
 * world-space standard deviations, opacity is in [0, 1], the quaternion is
 * unit. So this applies no exp, no sigmoid and no normalise, and the one bug to
 * watch for is doing so anyway: it renders as fog, exactly as writing activated
 * values into a PLY does. Returns the same shape parsePly does, so nothing
 * downstream can tell which format a frame arrived in. */
function parseSplat(buffer) {
  const SPLAT_BYTES = 32;
  if (buffer.byteLength % SPLAT_BYTES) {
    throw new Error(`not a .splat: ${buffer.byteLength} bytes is not a multiple of 32`);
  }
  const count = buffer.byteLength / SPLAT_BYTES;
  if (!count) throw new Error(".splat frame is empty");
  const bytes = new Uint8Array(buffer);
  // Copied, not viewed: byteOffset 0 is aligned here, but a Float32Array view
  // over the same buffer would still need a stride this layout does not have.
  const floats = new Float32Array(buffer);

  const data = new Float32Array(count * 12);
  const colors = new Uint8Array(count * 4);
  const positions = new Float32Array(count * 3);

  for (let i = 0; i < count; i++) {
    const f = i * 8;          // 8 float32 slots per record
    const b = i * SPLAT_BYTES;
    const px = floats[f], py = floats[f + 1], pz = floats[f + 2];
    positions[i * 3] = px; positions[i * 3 + 1] = py; positions[i * 3 + 2] = pz;

    const sx = floats[f + 3], sy = floats[f + 4], sz = floats[f + 5];

    // Renormalised: the quantisation is not symmetric, so q = 1 encodes as 256,
    // clamps to 255 and arrives as 0.992. Skipping this scales every Gaussian by
    // ~1.6% -- small enough to look fine and still be wrong.
    let qw = (bytes[b + 28] - 128) / 128;
    let qx = (bytes[b + 29] - 128) / 128;
    let qy = (bytes[b + 30] - 128) / 128;
    let qz = (bytes[b + 31] - 128) / 128;
    const norm = Math.hypot(qw, qx, qy, qz) || 1;
    qw /= norm; qx /= norm; qy /= norm; qz /= norm;

    const r00 = 1 - 2 * (qy * qy + qz * qz), r01 = 2 * (qx * qy - qw * qz), r02 = 2 * (qx * qz + qw * qy);
    const r10 = 2 * (qx * qy + qw * qz), r11 = 1 - 2 * (qx * qx + qz * qz), r12 = 2 * (qy * qz - qw * qx);
    const r20 = 2 * (qx * qz - qw * qy), r21 = 2 * (qy * qz + qw * qx), r22 = 1 - 2 * (qx * qx + qy * qy);

    const m00 = r00 * sx, m01 = r01 * sy, m02 = r02 * sz;
    const m10 = r10 * sx, m11 = r11 * sy, m12 = r12 * sz;
    const m20 = r20 * sx, m21 = r21 * sy, m22 = r22 * sz;

    const base = i * 12;
    data[base] = px; data[base + 1] = py; data[base + 2] = pz;
    data[base + 3] = bytes[b + 27] / 255;       // opacity, already activated
    data[base + 4] = m00 * m00 + m01 * m01 + m02 * m02;
    data[base + 5] = m00 * m10 + m01 * m11 + m02 * m12;
    data[base + 6] = m00 * m20 + m01 * m21 + m02 * m22;
    data[base + 7] = m10 * m10 + m11 * m11 + m12 * m12;
    data[base + 8] = m10 * m20 + m11 * m21 + m12 * m22;
    data[base + 9] = m20 * m20 + m21 * m21 + m22 * m22;

    colors[i * 4] = bytes[b + 24];              // RGB, not an SH coefficient
    colors[i * 4 + 1] = bytes[b + 25];
    colors[i * 4 + 2] = bytes[b + 26];
    colors[i * 4 + 3] = 255;
  }
  return { count, data, colors, positions };
}

/* A mesh or point-cloud frame, as `open4d.io.write_sequence` writes it: a
 * binary_little_endian PLY with a vertex element and, for a mesh, a face
 * element of `list uchar int vertex_indices`.
 *
 * A general-enough PLY reader rather than the fixed-stride one parsePly uses,
 * because these headers genuinely vary: colour is float in Open4D's canon (see
 * open4d.core.dtypes) but uchar in most files from elsewhere, and a face list
 * is variable-length, so neither element has a stride known up front.
 *
 * Normals are not read. Open4D's PLY writer refuses to store them, and the
 * renderer derives a face normal from screen-space derivatives instead, which
 * works for any mesh regardless of what its producer chose to carry. */
function parseMeshPly(buffer) {
  const bytes = new Uint8Array(buffer);
  const limit = Math.min(bytes.length, 1 << 16);
  const head = new TextDecoder("ascii").decode(bytes.subarray(0, limit));
  const marker = head.indexOf("end_header");
  if (marker < 0) throw new Error("not a PLY: no end_header in the first 64 KiB");
  const dataStart = head.indexOf("\n", marker) + 1;
  const lines = head.slice(0, marker).split("\n").map((line) => line.trim());
  if (!lines.some((line) => line.startsWith("format binary_little_endian"))) {
    throw new Error("only binary_little_endian PLY is supported");
  }

  const SIZES = { char: 1, int8: 1, uchar: 1, uint8: 1, short: 2, int16: 2,
                  ushort: 2, uint16: 2, int: 4, int32: 4, uint: 4, uint32: 4,
                  float: 4, float32: 4, double: 8, float64: 8 };
  const elements = [];
  for (const line of lines) {
    const parts = line.split(/\s+/);
    if (parts[0] === "element") {
      elements.push({ name: parts[1], count: parseInt(parts[2], 10), properties: [] });
    } else if (parts[0] === "property" && elements.length) {
      const element = elements[elements.length - 1];
      if (parts[1] === "list") {
        element.properties.push({ list: true, countType: parts[2], type: parts[3], name: parts[4] });
      } else {
        element.properties.push({ list: false, type: parts[1], name: parts[2] });
      }
    }
  }

  const view = new DataView(buffer);
  let offset = dataStart;
  const read = (type) => {
    switch (type) {
      case "char": case "int8": return view.getInt8(offset++);
      case "uchar": case "uint8": return view.getUint8(offset++);
      case "short": case "int16": { const v = view.getInt16(offset, true); offset += 2; return v; }
      case "ushort": case "uint16": { const v = view.getUint16(offset, true); offset += 2; return v; }
      case "int": case "int32": { const v = view.getInt32(offset, true); offset += 4; return v; }
      case "uint": case "uint32": { const v = view.getUint32(offset, true); offset += 4; return v; }
      case "float": case "float32": { const v = view.getFloat32(offset, true); offset += 4; return v; }
      case "double": case "float64": { const v = view.getFloat64(offset, true); offset += 8; return v; }
      default: throw new Error(`unsupported PLY property type ${type}`);
    }
  };

  let count = 0;
  let positions = null, colors = null;
  const faces = [];

  for (const element of elements) {
    if (element.name === "vertex") {
      count = element.count;
      const names = element.properties.map((property) => property.name);
      if (!["x", "y", "z"].every((need) => names.includes(need))) {
        throw new Error("PLY vertex element has no x/y/z");
      }
      // Colour is float in [0, 1] when Open4D wrote it and uchar in [0, 255]
      // when most other tools did; the property type says which.
      const colourNames = names.includes("red") ? ["red", "green", "blue"]
                        : names.includes("r") ? ["r", "g", "b"] : null;
      const colourType = colourNames
        ? element.properties.find((property) => property.name === colourNames[0]).type
        : null;
      const byteScale = colourType && SIZES[colourType] === 1 ? 1 : 255;
      positions = new Float32Array(count * 3);
      if (colourNames) colors = new Uint8Array(count * 4);
      for (let i = 0; i < count; i++) {
        const row = {};
        for (const property of element.properties) {
          if (property.list) {
            const n = read(property.countType);
            for (let k = 0; k < n; k++) read(property.type);
          } else {
            row[property.name] = read(property.type);
          }
        }
        positions[i * 3] = row.x; positions[i * 3 + 1] = row.y; positions[i * 3 + 2] = row.z;
        if (colourNames) {
          for (let c = 0; c < 3; c++) {
            const value = row[colourNames[c]] * byteScale;
            colors[i * 4 + c] = Math.max(0, Math.min(255, Math.round(value)));
          }
          colors[i * 4 + 3] = 255;
        }
      }
    } else {
      for (let i = 0; i < element.count; i++) {
        for (const property of element.properties) {
          if (property.list) {
            const n = read(property.countType);
            const indices = [];
            for (let k = 0; k < n; k++) indices.push(read(property.type));
            // Triangle fan, so a quad or an n-gon renders rather than being
            // dropped. Open4D only writes triangles; other producers do not.
            for (let k = 2; k < indices.length; k++) {
              faces.push(indices[0], indices[k - 1], indices[k]);
            }
          } else {
            read(property.type);
          }
        }
      }
    }
  }

  if (!count) throw new Error("PLY declares no vertices");
  const indices = faces.length ? new Uint32Array(faces) : null;

  const lower = [Infinity, Infinity, Infinity];
  const upper = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < count; i++) {
    for (let c = 0; c < 3; c++) {
      const value = positions[i * 3 + c];
      if (value < lower[c]) lower[c] = value;
      if (value > upper[c]) upper[c] = value;
    }
  }
  return { count, positions, colors, indices, bounds: [lower, upper] };
}

/* ------------------------------------------------------------------ draco ---
 * A compressed mesh frame, decoded here rather than on the server.
 *
 * This is the first format in this client that is actually a *compression* of
 * geometry rather than an interchange dump of it: measured on the basketball
 * sequence, 761 kB of PLY becomes 59 kB of Draco at 14-bit quantisation, which
 * is 1.8 MB/s at 30 fps instead of 23. That is the difference between a link
 * and a LAN, and it is why this exists.
 *
 * Two costs, both real and both bounded. Position is quantised -- at 14 bits the
 * worst vertex moved 0.0046% of the model's diagonal on that sequence -- and
 * duplicate vertices are merged, so the decoded point count is lower than the
 * encoder was given (20,672 -> 19,747 there, because the source OBJ splits
 * vertices at seams). Neither changes what the surface looks like; both mean a
 * `.drc` frame is a delivery form and the PLY stays the source of truth.
 *
 * The decoder is Google's, vendored under client/vendor/draco and served from
 * this origin -- never a CDN, which is what keeps the page free of external
 * dependencies. It is a WASM module, so loading is asynchronous and happens once
 * on first use: `Scheduler.decode` is awaited, which is what makes an async
 * decoder possible at all here.
 */
const DRACO_PATH = "vendor/draco";

let dracoModule = null;


/* One Draco frame, in the shape parseMeshPly returns so the renderer cannot
 * tell which format a frame arrived in. */
async function parseDraco(buffer) {
  const draco = await loadDraco();
  const decoder = new draco.Decoder();
  const input = new draco.DecoderBuffer();
  input.Init(new Int8Array(buffer), buffer.byteLength);

  let mesh = null;
  try {
    if (decoder.GetEncodedGeometryType(input) !== draco.TRIANGULAR_MESH) {
      throw new Error("not a Draco triangular mesh");
    }
    mesh = new draco.Mesh();
    const status = decoder.DecodeBufferToMesh(input, mesh);
    if (!status.ok()) throw new Error(status.error_msg());

    const count = mesh.num_points();
    const faces = mesh.num_faces();

    const positions = new Float32Array(count * 3);
    const attribute = decoder.GetAttribute(
      mesh, decoder.GetAttributeId(mesh, draco.POSITION));
    const values = new draco.DracoFloat32Array();
    decoder.GetAttributeFloatForAllPoints(mesh, attribute, values);
    for (let i = 0; i < count * 3; i++) positions[i] = values.GetValue(i);
    draco.destroy(values);

    // Through the heap rather than GetFaceFromMesh per face: 39k calls across
    // the WASM boundary is most of the decode time on this content.
    const bytes = faces * 3 * 4;
    const pointer = draco._malloc(bytes);
    decoder.GetTrianglesUInt32Array(mesh, bytes, pointer);
    const indices = new Uint32Array(draco.HEAPF32.buffer, pointer, faces * 3).slice();
    draco._free(pointer);

    let colors = null;
    const colourId = decoder.GetAttributeId(mesh, draco.COLOR);
    if (colourId >= 0) {
      const colourAttribute = decoder.GetAttribute(mesh, colourId);
      const raw = new draco.DracoFloat32Array();
      decoder.GetAttributeFloatForAllPoints(mesh, colourAttribute, raw);
      const channels = colourAttribute.num_components();
      colors = new Uint8Array(count * 4);
      for (let i = 0; i < count; i++) {
        for (let c = 0; c < 3; c++) {
          const value = raw.GetValue(i * channels + Math.min(c, channels - 1));
          // Draco keeps whatever range it was given; Open4D's canon is [0, 1].
          colors[i * 4 + c] = Math.max(0, Math.min(255, Math.round(
            value <= 1.0001 ? value * 255 : value)));
        }
        colors[i * 4 + 3] = 255;
      }
      draco.destroy(raw);
    }

    const lower = [Infinity, Infinity, Infinity];
    const upper = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < count; i++) {
      for (let c = 0; c < 3; c++) {
        const value = positions[i * 3 + c];
        if (value < lower[c]) lower[c] = value;
        if (value > upper[c]) upper[c] = value;
      }
    }
    return { count, positions, colors, indices, bounds: [lower, upper] };
  } finally {
    if (mesh) draco.destroy(mesh);
    draco.destroy(input);
    draco.destroy(decoder);
  }
}

/* ----------------------------------------------------------------- codecs ---
 * What is on the wire, keyed the way `streamer.codecs` keys it: by
 * (representation, suffix), not by suffix. The same `.ply` is a 3DGS cloud or a
 * mesh depending on which representation is asking, and they need different
 * parsers -- which is why this was previously two hand-written dispatchers, one
 * per representation, each sniffing an extension.
 *
 * Adding a codec is an entry here and a parser. Nothing else in this file
 * changes, and nothing outside it learns the format's name. */
function suffixOf(url) {
  const path = url.split("?", 1)[0];
  const dot = path.lastIndexOf(".");
  return dot < 0 ? "" : path.slice(dot).toLowerCase();
}

const CODECS = {
  mesh:      { ".ply": parseMeshPly, ".drc": parseDraco },
  points:    { ".ply": parseMeshPly, ".drc": parseDraco },
  gaussians: { ".ply": parsePly,     ".splat": parseSplat },
  // No pixels: an <img> needs the main thread, and the browser already
  // decodes one off it.
};

function codecFor(representation, url) {
  const table = CODECS[representation] || {};
  const suffix = suffixOf(url);
  const parse = table[suffix];
  if (!parse) {
    const offered = Object.keys(table).join(", ") || "nothing";
    throw new Error(
      `no decoder for a ${representation} frame ending ${suffix || "(none)"}; `
      + `this client decodes ${offered}`);
  }
  return parse;
}


/* `importScripts` rather than a script tag, and paths relative to this file
 * rather than to the bundle root. Same module and same wasm as the page used to
 * fetch, from the same origin. */
async function loadDraco() {
  if (dracoModule) return dracoModule;
  dracoModule = (async () => {
    if (!self.DracoDecoderModule) {
      importScripts(`${DRACO_PATH}/draco_wasm_wrapper.js`);
    }
    const wasmBinary = await (await fetch(`${DRACO_PATH}/draco_decoder.wasm`))
      .arrayBuffer();
    return self.DracoDecoderModule({ wasmBinary });
  })();
  return dracoModule;
}

/* Every typed array in a parsed frame, so they can be transferred rather than
 * copied. Collected by inspection rather than by a fixed list: a parser that
 * grows a field should not have to remember to declare it here. */
function transferables(parsed) {
  const buffers = [];
  for (const value of Object.values(parsed)) {
    if (value && value.buffer instanceof ArrayBuffer) buffers.push(value.buffer);
  }
  return buffers;
}

self.onmessage = async (event) => {
  const { id, representation, url, buffer } = event.data;
  try {
    const parse = codecFor(representation, url);
    const parsed = await parse(buffer, url);
    self.postMessage({ id, parsed }, transferables(parsed));
  } catch (error) {
    // The message, not the Error: an Error does not survive structured cloning
    // in every browser, and losing it would turn a clear failure into silence.
    self.postMessage({ id, error: String((error && error.message) || error) });
  }
};
