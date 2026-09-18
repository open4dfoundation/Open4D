'use strict';

const assert = require('assert');
const test = require('node:test');

const { mountLinkRate } = require('../system/WebClient/src/link-rate');

function fakeElement() {
    return { textContent: '', className: '', title: '' };
}

/** A fetch that resolves once with the given body. */
function fetchReturning(body, ok = true) {
    return async () => ({ ok, status: ok ? 200 : 503, json: async () => body });
}

test('a shaped link reports its enforced rate', async () => {
    const element = fakeElement();
    const stop = mountLinkRate(element, {
        fetchImpl: fetchReturning({ shaped: true, rateMbps: 12.5, interface: 'eth0' }),
        intervalMs: 1e6
    });
    await new Promise(resolve => setImmediate(resolve));
    stop();
    assert.match(element.textContent, /12\.5 Mbps/);
    // The interface moved to the tooltip so the visible line stays short.
    assert.match(element.title, /eth0/);
    assert.strictEqual(element.className, 'link shaped');
});

test('an unshaped link says so rather than showing nothing', async () => {
    // This wording is load-bearing: an unshaped run makes every adaptive
    // system hold one operating point, which reads as a broken ABR unless the
    // page says the link is flat.
    const element = fakeElement();
    const stop = mountLinkRate(element, {
        fetchImpl: fetchReturning({ shaped: false, interface: 'eth0' }),
        intervalMs: 1e6
    });
    await new Promise(resolve => setImmediate(resolve));
    stop();
    assert.match(element.textContent, /unshaped/);
    assert.strictEqual(element.className, 'link unshaped');
});

test('a failed probe is reported as unknown, not as unshaped', async () => {
    // Conflating "cannot tell" with "flat link" would invite the wrong
    // conclusion about a run.
    const element = fakeElement();
    const stop = mountLinkRate(element, {
        fetchImpl: async () => { throw new Error('boom'); },
        intervalMs: 1e6
    });
    await new Promise(resolve => setImmediate(resolve));
    stop();
    assert.match(element.textContent, /unknown/);
    assert.match(element.title, /boom/);
    assert.strictEqual(element.className, 'link unknown');
});

test('an HTTP error is surfaced, and polling stops when told to', async () => {
    const element = fakeElement();
    let calls = 0;
    const stop = mountLinkRate(element, {
        fetchImpl: async () => { calls++; return { ok: false, status: 503 }; },
        intervalMs: 1
    });
    await new Promise(resolve => setImmediate(resolve));
    assert.match(element.title, /HTTP 503/);
    stop();
    const seen = calls;
    await new Promise(resolve => setTimeout(resolve, 20));
    assert.strictEqual(calls, seen, 'stop() must end the polling');
});

test('mounting against a missing element is a no-op, not a crash', () => {
    assert.doesNotThrow(() => mountLinkRate(null));
    assert.strictEqual(typeof mountLinkRate(null), 'function');
});
