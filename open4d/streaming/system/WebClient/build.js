#!/usr/bin/env node
'use strict';

/**
 * Bundle the browser client.
 *
 * Two entry points, because the Draco decode runs in a worker and a worker
 * needs its own script: `main.js` and `draco-worker.js`. ClientCore is
 * CommonJS and shared with the Node client, which esbuild bundles for the
 * browser unchanged — that is the point, since the whole aim is for both
 * clients to run the same streaming logic.
 *
 *   node build.js            one-shot build
 *   node build.js --watch    rebuild on change
 */

const esbuild = require('esbuild');
const fs = require('fs');
const path = require('path');

const ROOT = __dirname;
const OUT = path.join(ROOT, 'dist');
const watch = process.argv.includes('--watch');

const options = {
    entryPoints: {
        main: path.join(ROOT, 'src/main.js'),
        'draco-worker': path.join(ROOT, 'src/draco-worker.js'),
        baseline: path.join(ROOT, 'src/baseline-main.js'),
        vega: path.join(ROOT, 'src/vega-main.js'),
        nevo: path.join(ROOT, 'src/nevo-main.js'),
        chooser: path.join(ROOT, 'src/chooser.js')
    },
    outdir: OUT,
    bundle: true,
    format: 'iife',
    platform: 'browser',
    target: ['chrome110', 'safari16', 'firefox115'],
    sourcemap: true,
    minify: !watch,
    logLevel: 'info',
    // ClientCore is CommonJS; browsers have no `process`, and some transitive
    // code sniffs for it.
    define: { 'process.env.NODE_ENV': '"production"' }
};

function copyStatic() {
    fs.mkdirSync(OUT, { recursive: true });
    for (const page of ['index.html', 'baseline.html', 'vega.html',
                        'nevo.html', 'compare.html']) {
        fs.copyFileSync(path.join(ROOT, 'public', page), path.join(OUT, page));
    }
    const vendorOut = path.join(OUT, 'vendor/draco');
    fs.mkdirSync(vendorOut, { recursive: true });
    for (const file of ['draco_decoder.js', 'draco_decoder.wasm']) {
        fs.copyFileSync(path.join(ROOT, 'vendor/draco', file),
                        path.join(vendorOut, file));
    }
}

async function run() {
    copyStatic();
    if (watch) {
        const context = await esbuild.context(options);
        await context.watch();
        console.log('watching for changes …');
        return;
    }
    await esbuild.build(options);
    const sizes = fs.readdirSync(OUT)
        .filter(f => f.endsWith('.js'))
        .map(f => `${f} ${(fs.statSync(path.join(OUT, f)).size / 1024).toFixed(0)} kB`);
    console.log(`built -> ${path.relative(process.cwd(), OUT)}: ${sizes.join(', ')}`);
}

run().catch(err => { console.error(err); process.exit(1); });
