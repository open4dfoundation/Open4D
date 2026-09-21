'use strict';

/**
 * Differential test: the extracted planner vs. the pre-refactor implementation.
 *
 * `legacyPlanSegmentDownload` below is the download-planning half of
 * `downloadSegmentFiles` as it stood in system/Client/client.js before the
 * ClientCore extraction (commit 6c2569d), transcribed with its module globals
 * turned into parameters and nothing else changed. If the extraction altered
 * behaviour for any manifest shape, these assertions fail.
 *
 * Keep this file until the browser client is landed and the parity harness in
 * the plan (same trace through both clients) is running; after that this is
 * redundant with the end-to-end comparison.
 *
 * Run: node --test tests/test_client_core_parity.js
 */

const assert = require('assert');
const test = require('node:test');
const path = require('path');

const { planSegmentDownload } = require('../system/ClientCore/download-plan');

// --------------------------------------------------------------------------
// The pre-refactor implementation, verbatim apart from injected globals.
// --------------------------------------------------------------------------
function legacyPlanSegmentDownload(selection, segmentIdx, manifest, {
    FRAMES_PER_SEGMENT, INTERACTIVE_MODE, clientCacheRoot, pathToUrl
}) {
    const framesPerSegment = FRAMES_PER_SEGMENT;
    const tasks = [];
    const perObj = new Map();

    for (const [objName, rep] of Object.entries(selection.combo)) {
        const objData = manifest.objects[objName];
        const startNumber = objData.start_number || 1;
        const baseDir = rep.paths.base_dir;
        const files = [];
        const objectDir = INTERACTIVE_MODE ? path.join(
            clientCacheRoot,
            `segment_${String(segmentIdx).padStart(4, '0')}`,
            objName,
            rep.id.replace(/[^a-zA-Z0-9._-]+/g, '-')
        ) : null;

        const textureAssets = (
            Array.isArray(rep.paths.texture_urls) && rep.paths.texture_urls.length > 0
                ? rep.paths.texture_urls
                : Array.isArray(rep.paths.texture_mp4s) && rep.paths.texture_mp4s.length > 0
                    ? rep.paths.texture_mp4s
                    : [rep.paths.texture_url || rep.paths.texture_mp4].filter(Boolean)
        );
        textureAssets.forEach((textureAsset, textureIndex) => {
            const url = pathToUrl(textureAsset);
            if (url) {
                files.push({
                    url,
                    destination: INTERACTIVE_MODE
                        ? path.join(objectDir, `texture_${String(textureIndex).padStart(4, '0')}.mp4`)
                        : null,
                    kind: 'texture',
                    textureIndex,
                    frameNum: null
                });
            }
        });

        const geometryPattern = rep.paths.geometry_url_pattern || rep.paths.geometry_drc_pattern;
        if (geometryPattern) {
            for (let f = 0; f < framesPerSegment; f++) {
                const frameNum = startNumber + f;
                const drcFile = geometryPattern.replace('%04d', String(frameNum).padStart(4, '0'));
                const asset = drcFile.startsWith('/files/') || /^https?:\/\//.test(drcFile)
                    ? drcFile
                    : `${baseDir}/${drcFile}`;
                const url = pathToUrl(asset);
                if (url) {
                    files.push({
                        url,
                        destination: INTERACTIVE_MODE
                            ? path.join(objectDir, `geometry_${String(f).padStart(4, '0')}.drc`)
                            : null,
                        kind: 'geometry',
                        frameNum,
                        frameIndex: f
                    });
                }
            }
        }

        perObj.set(objName, {
            repId: rep.id,
            size: 0,
            success: 0,
            total: files.length,
            geometryTotal: files.filter(f => f.kind === 'geometry').length,
            geometrySuccess: 0,
            objectDir,
            textureFile: null,
            textureFiles: new Array(textureAssets.length).fill(null),
            geometryFiles: new Array(framesPerSegment).fill(null)
        });
        for (const file of files) tasks.push({ objName, ...file });
    }

    return { tasks, perObj };
}

// --------------------------------------------------------------------------
// The current implementation, wired exactly as client.js wires it.
// --------------------------------------------------------------------------
function currentPlan(selection, segmentIdx, manifest, env) {
    return planSegmentDownload({
        selection,
        manifest,
        framesPerSegment: env.FRAMES_PER_SEGMENT,
        interactive: env.INTERACTIVE_MODE,
        objectDirFor: (objName, repId) => path.join(
            env.clientCacheRoot,
            `segment_${String(segmentIdx).padStart(4, '0')}`,
            objName,
            repId.replace(/[^a-zA-Z0-9._-]+/g, '-')
        ),
        destinationFor: (objName, objectDir, file) => (file.kind === 'texture'
            ? path.join(objectDir, `texture_${String(file.textureIndex).padStart(4, '0')}.mp4`)
            : path.join(objectDir, `geometry_${String(file.frameIndex).padStart(4, '0')}.drc`)),
        pathToUrl: env.pathToUrl
    });
}

// The real pathToUrl from client.js, so URL rewriting is part of the comparison.
const SERVER_URL = 'http://10.0.0.5:3000';
function pathToUrl(assetPath) {
    if (!assetPath) return null;
    if (/^https?:\/\//.test(assetPath)) return assetPath;
    if (assetPath.startsWith('/files/')) return `${SERVER_URL}${assetPath}`;
    const match = assetPath.match(/files\/(.+)/);
    return match ? `${SERVER_URL}/files/${match[1]}` : null;
}

function env(overrides = {}) {
    return {
        FRAMES_PER_SEGMENT: 60,
        INTERACTIVE_MODE: true,
        clientCacheRoot: '/tmp/vs4d-client-abc123',
        pathToUrl,
        ...overrides
    };
}

function assertParity(selection, segmentIdx, manifest, environment, label) {
    const legacy = legacyPlanSegmentDownload(selection, segmentIdx, manifest, environment);
    const current = currentPlan(selection, segmentIdx, manifest, environment);

    assert.deepStrictEqual(current.tasks, legacy.tasks, `${label}: task list differs`);
    assert.deepStrictEqual([...current.perObj.keys()], [...legacy.perObj.keys()],
        `${label}: object order differs`);
    for (const key of legacy.perObj.keys()) {
        assert.deepStrictEqual(current.perObj.get(key), legacy.perObj.get(key),
            `${label}: accumulator for ${key} differs`);
    }
}

// --------------------------------------------------------------------------
// Manifest shapes drawn from what ladderlib.write_ladder actually publishes.
// --------------------------------------------------------------------------

function rep(id, paths) {
    return { id, paths, predicted: { bitrate_mbps: 12.5, quality: 38.2 } };
}

// The normal corpus path: independently encoded groups published in playback
// order, geometry as a per-frame %04d pattern.
const multiGroup = {
    selection: {
        combo: {
            dancer: rep('r_res960_crf24_qp7', {
                base_dir: '/files/media/dancer/r_res960_crf24_qp7',
                texture_urls: [
                    '/files/media/dancer/r_res960_crf24_qp7/dancer_fr0001_w960_crf24_part00.mp4',
                    '/files/media/dancer/r_res960_crf24_qp7/dancer_fr0001_w960_crf24_part01.mp4'
                ],
                texture_mp4: null,
                texture_url: null,
                geometry_url_pattern:
                    '/files/media/dancer/r_res960_crf24_qp7/dancer_fr%04d_qp7.drc',
                geometry_drc_pattern: 'dancer_fr%04d_qp7.drc'
            }),
            basketball: rep('r_res480_crf30_qp7', {
                base_dir: '/files/media/basketball/r_res480_crf30_qp7',
                texture_urls: [
                    '/files/media/basketball/r_res480_crf30_qp7/basketball_player_fr0001_w480_crf30_part00.mp4'
                ],
                geometry_url_pattern:
                    '/files/media/basketball/r_res480_crf30_qp7/basketball_player_fr%04d_qp7.drc'
            })
        }
    },
    manifest: {
        objects: {
            dancer: { start_number: 1 },
            basketball: { start_number: 121 }
        }
    }
};

test('parity: multi-group textures and per-frame geometry', () => {
    assertParity(multiGroup.selection, 7, multiGroup.manifest, env(), 'multi-group');
});

test('parity: simulated mode writes no destinations', () => {
    assertParity(multiGroup.selection, 7, multiGroup.manifest,
        env({ INTERACTIVE_MODE: false, clientCacheRoot: null }), 'simulated');
});

test('parity: legacy single-file texture fallback', () => {
    const selection = {
        combo: {
            mitch: rep('r_res720_crf26_qp8', {
                base_dir: '/files/media/mitch/r_res720_crf26_qp8',
                texture_mp4s: [],
                texture_urls: [],
                texture_url: '/files/media/mitch/r_res720_crf26_qp8/mitch_fr0001_w720_crf26.mp4',
                geometry_url_pattern:
                    '/files/media/mitch/r_res720_crf26_qp8/mitch_fr%04d_qp8.drc'
            })
        }
    };
    assertParity(selection, 3, { objects: { mitch: { start_number: 61 } } },
        env(), 'single-texture');
});

test('parity: texture_mp4s middle fallback', () => {
    const selection = {
        combo: {
            thomas: rep('r_res1440_crf22_qp9', {
                base_dir: '/files/media/thomas/r_res1440_crf22_qp9',
                texture_mp4s: [
                    '/media/frozzzen/DataDrive/files/media/thomas/r/a_part00.mp4',
                    '/media/frozzzen/DataDrive/files/media/thomas/r/a_part01.mp4'
                ],
                geometry_url_pattern:
                    '/files/media/thomas/r_res1440_crf22_qp9/thomas_fr%04d_qp9.drc'
            })
        }
    };
    assertParity(selection, 0, { objects: { thomas: { start_number: 618 } } },
        env(), 'texture_mp4s');
});

test('parity: relative geometry pattern resolved against base_dir', () => {
    // geometry_drc_pattern only: the pattern is relative, so it must be joined
    // to base_dir before pathToUrl sees it.
    const selection = {
        combo: {
            UMA0: rep('r_res240_crf36_qp7', {
                base_dir: '/files/media/UMA0/r_res240_crf36_qp7',
                texture_url: '/files/media/UMA0/r_res240_crf36_qp7/UMA0_w240_crf36.mp4',
                geometry_drc_pattern: 'UMA0_fr%04d_qp7.drc'
            })
        }
    };
    assertParity(selection, 11, { objects: { UMA0: { start_number: 900 } } },
        env(), 'relative-geometry');
});

test('parity: absolute http geometry pattern passed through', () => {
    const selection = {
        combo: {
            UMA1: rep('r1', {
                base_dir: '/files/media/UMA1/r1',
                texture_url: '/files/media/UMA1/r1/t.mp4',
                geometry_url_pattern: 'https://cdn.example/UMA1/UMA1_fr%04d_qp7.drc'
            })
        }
    };
    assertParity(selection, 2, { objects: { UMA1: { start_number: 1800 } } },
        env(), 'absolute-geometry');
});

test('parity: object with no texture at all', () => {
    const selection = {
        combo: {
            ghost: rep('r0', {
                base_dir: '/files/media/ghost/r0',
                texture_urls: [],
                texture_mp4s: [],
                texture_url: null,
                texture_mp4: null,
                geometry_url_pattern: '/files/media/ghost/r0/ghost_fr%04d_qp7.drc'
            })
        }
    };
    assertParity(selection, 1, { objects: { ghost: { start_number: 1 } } },
        env(), 'no-texture');
});

test('parity: object with no geometry pattern at all', () => {
    const selection = {
        combo: {
            flat: rep('r0', {
                base_dir: '/files/media/flat/r0',
                texture_url: '/files/media/flat/r0/t.mp4'
            })
        }
    };
    assertParity(selection, 1, { objects: { flat: { start_number: 1 } } },
        env(), 'no-geometry');
});

test('parity: missing start_number defaults to frame 1', () => {
    assertParity(multiGroup.selection, 4,
        { objects: { dancer: {}, basketball: {} } }, env(), 'default-start');
});

test('parity: rep ids needing sanitization for the cache path', () => {
    const selection = {
        combo: {
            dancer: rep('r res/960:crf 24*qp7', {
                base_dir: '/files/media/dancer/x',
                texture_url: '/files/media/dancer/x/t.mp4',
                geometry_url_pattern: '/files/media/dancer/x/dancer_fr%04d_qp7.drc'
            })
        }
    };
    assertParity(selection, 9, { objects: { dancer: { start_number: 1 } } },
        env(), 'sanitized-rep-id');
});

test('parity: the full nine-object study scene at 60 frames', () => {
    const names = ['dancer', 'basketball', 'mitch', 'thomas',
                   'UMA0', 'UMA1', 'UMA2', 'UMA3', 'UMA4'];
    const combo = {};
    const objects = {};
    names.forEach((name, i) => {
        combo[name] = rep(`r_res${240 * (i % 4 + 1)}_crf${20 + i}_qp7`, {
            base_dir: `/files/media/${name}/r${i}`,
            texture_urls: [
                `/files/media/${name}/r${i}/${name}_part00.mp4`,
                `/files/media/${name}/r${i}/${name}_part01.mp4`
            ],
            geometry_url_pattern: `/files/media/${name}/r${i}/${name}_fr%04d_qp7.drc`
        });
        objects[name] = { start_number: 1 + i * 60 };
    });
    const { tasks } = currentPlan({ combo }, 12, { objects }, env());
    assert.strictEqual(tasks.length, 9 * 62, '9 objects x (2 textures + 60 frames)');
    assertParity({ combo }, 12, { objects }, env(), 'nine-object-scene');
});

test('parity: frame numbering across a segment boundary', () => {
    for (const startNumber of [1, 60, 61, 121, 618, 900, 3900]) {
        assertParity(
            multiGroup.selection, 5,
            { objects: { dancer: { start_number: startNumber }, basketball: { start_number: 1 } } },
            env(), `start=${startNumber}`);
    }
});
