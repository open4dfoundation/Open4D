'use strict';

/**
 * Enforces the one rule ClientCore exists to uphold: it must contain no
 * platform code.
 *
 * Without this, the rule is a comment in a README and the core drifts back into
 * being Node-only one `require('fs')` at a time — and the drift is invisible
 * until the browser client fails to bundle.
 *
 * Scope note: `testing/` is exempt from the module ban only insofar as it may
 * import the contract; it is still forbidden platform modules, because the fake
 * platform has to run in a browser test runner too.
 *
 * Run: node --test tests/test_client_core_purity.js
 */

const assert = require('assert');
const test = require('node:test');
const fs = require('fs');
const path = require('path');

const CORE_DIR = path.join(__dirname, '..', 'system', 'ClientCore');

/** Node built-ins that must never appear in a require() inside the core. */
const FORBIDDEN_MODULES = [
    'fs', 'node:fs', 'fs/promises', 'node:fs/promises',
    'http', 'node:http', 'https', 'node:https',
    'path', 'node:path', 'os', 'node:os',
    'child_process', 'node:child_process',
    'net', 'node:net', 'crypto', 'node:crypto',
    'node-fetch'
];

/**
 * Globals that tie code to one platform, or that bypass the injected clock.
 * `fetch` is included: the core must go through transport.getJson/fetchAsset so
 * that a test can route requests and so the no-store cache rule stays in the
 * adapter.
 */
const FORBIDDEN_GLOBALS = [
    { pattern: /\bprocess\s*\./, name: 'process.*' },
    { pattern: /\bconsole\s*\.\s*(log|warn|error|info|debug)\b/, name: 'console.*' },
    { pattern: /\bDate\s*\.\s*now\b/, name: 'Date.now' },
    { pattern: /\bsetInterval\s*\(/, name: 'setInterval' },
    { pattern: /\bclearInterval\s*\(/, name: 'clearInterval' },
    { pattern: /\bsetTimeout\s*\(/, name: 'setTimeout' },
    { pattern: /\bclearTimeout\s*\(/, name: 'clearTimeout' },
    { pattern: /\bfetch\s*\(/, name: 'fetch(' },
    { pattern: /\brequire\s*\(\s*['"]\.\.\/Client\//, name: "require('../Client/...')" }
];

function coreFiles() {
    const found = [];
    const walk = dir => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            const full = path.join(dir, entry.name);
            if (entry.isDirectory()) walk(full);
            else if (entry.name.endsWith('.js')) found.push(full);
        }
    };
    walk(CORE_DIR);
    return found;
}

/**
 * Strip comments and string literals so documentation that *mentions*
 * `process.exit` or `Date.now` is not mistaken for code that calls it.
 * Deliberately crude — it only has to be conservative enough not to hide real
 * code, and every core file is plain CommonJS.
 */
function stripCommentsAndStrings(source) {
    let out = '';
    let i = 0;
    const n = source.length;
    while (i < n) {
        const two = source.slice(i, i + 2);
        if (two === '//') {
            while (i < n && source[i] !== '\n') i++;
        } else if (two === '/*') {
            i += 2;
            while (i < n && source.slice(i, i + 2) !== '*/') i++;
            i += 2;
        } else if (source[i] === '"' || source[i] === "'" || source[i] === '`') {
            const quote = source[i];
            i++;
            while (i < n && source[i] !== quote) {
                if (source[i] === '\\') i++;
                i++;
            }
            i++;
            out += '""';
        } else {
            out += source[i];
            i++;
        }
    }
    return out;
}

test('the core directory actually contains the modules under test', () => {
    const names = coreFiles().map(f => path.relative(CORE_DIR, f));
    for (const expected of ['bandwidth.js', 'download-plan.js', 'link-throughput.js',
                            'platform.js', 'stream-config.js',
                            path.join('testing', 'fake-platform.js')]) {
        assert.ok(names.includes(expected), `expected ${expected} in ClientCore`);
    }
});

test('no core module requires a Node built-in', () => {
    const violations = [];
    for (const file of coreFiles()) {
        const code = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'));
        // Strings are blanked, so recover the specifier from the original text.
        const raw = fs.readFileSync(file, 'utf8');
        const requires = [...raw.matchAll(/require\s*\(\s*['"]([^'"]+)['"]\s*\)/g)]
            .map(m => m[1]);
        void code;
        for (const specifier of requires) {
            if (FORBIDDEN_MODULES.includes(specifier)) {
                violations.push(`${path.relative(CORE_DIR, file)} requires '${specifier}'`);
            }
        }
    }
    assert.deepStrictEqual(violations, [],
        'ClientCore must be platform-free; move this into an adapter');
});

/**
 * Narrow, per-file exemptions. Each one needs a reason; a blanket exemption
 * would make this suite decorative.
 *
 * `testing/fake-platform.js` -> `console.*`: FakeLogger's opt-in `echo` prints
 * captured lines while debugging a failing test. `console` exists in browsers
 * as well as Node, so this is a layering preference rather than a portability
 * break, and a logger test double is the one place writing to a console is the
 * point rather than a leak.
 */
const GLOBAL_EXEMPTIONS = {
    [path.join('testing', 'fake-platform.js')]: ['console.*']
};

test('no core module uses a platform global or bypasses the injected clock', () => {
    const violations = [];
    for (const file of coreFiles()) {
        const relative = path.relative(CORE_DIR, file);
        const exempt = GLOBAL_EXEMPTIONS[relative] || [];
        const code = stripCommentsAndStrings(fs.readFileSync(file, 'utf8'));
        for (const { pattern, name } of FORBIDDEN_GLOBALS) {
            if (exempt.includes(name)) continue;
            if (pattern.test(code)) {
                violations.push(`${relative} uses ${name}`);
            }
        }
    }
    assert.deepStrictEqual(violations, [],
        'use platform.clock / platform.logger / platform.transport instead');
});

test('every exemption is still needed', () => {
    // An exemption that no longer matches anything is stale and should be
    // deleted, so the list stays an accurate account of the remaining leaks.
    for (const [relative, names] of Object.entries(GLOBAL_EXEMPTIONS)) {
        const full = path.join(CORE_DIR, relative);
        assert.ok(fs.existsSync(full), `exempted file ${relative} no longer exists`);
        const code = stripCommentsAndStrings(fs.readFileSync(full, 'utf8'));
        for (const name of names) {
            const rule = FORBIDDEN_GLOBALS.find(g => g.name === name);
            assert.ok(rule, `exemption names an unknown rule: ${name}`);
            assert.ok(rule.pattern.test(code),
                `${relative} no longer uses ${name}; drop the exemption`);
        }
    }
});

test('the comment stripper does not hide real code', () => {
    // Guards the guard: if stripping were too aggressive it would silently
    // stop detecting violations, and this suite would pass vacuously.
    const source = [
        '// mentions process.exit in a comment',
        '/* and Date.now in a block comment */',
        'const message = "setInterval in a string";',
        'const real = Date.now();'
    ].join('\n');
    const stripped = stripCommentsAndStrings(source);
    assert.ok(!stripped.includes('process.exit'), 'line comment removed');
    assert.ok(!stripped.includes('setInterval'), 'string literal blanked');
    assert.strictEqual(
        (stripped.match(/Date\s*\.\s*now/g) || []).length, 1,
        'the one real Date.now call survives stripping');
});

test('the platform contract is the only place adapters are described', () => {
    // platform.js documents Node/browser specifics in prose; that is its job.
    // Every OTHER core module must be free even of those references, so the
    // logic cannot quietly grow a platform assumption.
    const offenders = [];
    for (const file of coreFiles()) {
        if (path.basename(file) === 'platform.js') continue;
        if (path.basename(file) === 'fake-platform.js') continue;
        const raw = fs.readFileSync(file, 'utf8');
        if (/\bOPFS\b|\bIndexedDB\b|\bWebCodecs\b/.test(raw)) {
            offenders.push(path.relative(CORE_DIR, file));
        }
    }
    assert.deepStrictEqual(offenders, []);
});
