#!/usr/bin/env node
// Shape this server's egress NIC according to a CSV bandwidth trace. A root
// token-bucket is deliberately used instead of a destination u32 filter: the
// latter can silently miss traffic on a multiqueue NIC, as the previous Quest
// runs demonstrated. This covers ORBIT HTTP and every baseline TCP media path.
const { execFile, execFileSync } = require('child_process');
const fs = require('fs');
const http = require('http');
let activeCleanup = async () => {};
const TBF_BURST_FLOOR_BYTES = 128 * 1024;

function usage(exitCode = 1) {
  console.error(`Usage:
  sudo node quest-trace-player.js --quest-ip <ip> [options] <trace.csv>

Options:
  --interface <nic>       Server egress NIC (auto-detected by default)
  --wait-for-broadcast    Start the trace clock when Quest presses A
  --node-url <url>        Node URL used by --wait-for-broadcast
                          (default: http://127.0.0.1:3000)
  --scale <factor>        Multiply every CSV bandwidth (default: 1)
  --hold                  Keep the final limit until Ctrl-C
  --validate              Validate the CSV without installing tc rules
  --help

CSV format: time,bandwidth, where time is seconds and bandwidth is Mbps.

The old environment variables QUEST_IP, SERVER_INTERFACE, VS4D_TRACE_SCALE,
VS4D_TRACE_HOLD, and VS4D_TRACE_WAIT_FOR_BROADCAST remain supported.`);
  process.exit(exitCode);
}

function parseArgs(argv) {
  const options = {
    questIp: process.env.QUEST_IP || '',
    interfaceName: process.env.SERVER_INTERFACE || '',
    scale: Number(process.env.VS4D_TRACE_SCALE || 1),
    hold: process.env.VS4D_TRACE_HOLD === '1',
    validate: false,
    waitForBroadcast: process.env.VS4D_TRACE_WAIT_FOR_BROADCAST === '1',
    nodeUrl: process.env.VS4D_NODE_URL || 'http://127.0.0.1:3000',
    traceFile: '',
  };
  for (let index = 0; index < argv.length; index++) {
    const value = argv[index];
    const requiredValue = name => {
      if (index + 1 >= argv.length) throw new Error(`${name} requires a value`);
      return argv[++index];
    };
    if (value === '--quest-ip') options.questIp = requiredValue(value);
    else if (value === '--interface') options.interfaceName = requiredValue(value);
    else if (value === '--scale') options.scale = Number(requiredValue(value));
    else if (value === '--node-url') options.nodeUrl = requiredValue(value);
    else if (value === '--wait-for-broadcast') options.waitForBroadcast = true;
    else if (value === '--hold') options.hold = true;
    else if (value === '--validate') options.validate = true;
    else if (value === '--help' || value === '-h') usage(0);
    else if (value.startsWith('-')) throw new Error(`unknown option ${value}`);
    else if (options.traceFile) throw new Error('only one trace CSV may be supplied');
    else options.traceFile = value;
  }
  return options;
}

function validateIpv4(value) {
  const octets = value.split('.');
  return octets.length === 4 && octets.every(part => /^\d+$/.test(part)
    && Number(part) >= 0 && Number(part) <= 255);
}

function loadTrace(filename, scale) {
  const points = [];
  const ignored = [];
  const lines = fs.readFileSync(filename, 'utf8').split(/\r?\n/);
  for (let index = 0; index < lines.length; index++) {
    const line = lines[index].trim();
    if (!line) continue;
    const values = line.split(',').map(value => Number(value.trim()));
    if (values.length < 2 || !Number.isFinite(values[0])
        || !Number.isFinite(values[1])) {
      // The normal header and a trailing duplicate header in an old lte.csv
      // are harmless, but report other malformed rows instead of silently
      // changing an experiment.
      if (line.toLowerCase() !== 'time,bandwidth') ignored.push(index + 1);
      continue;
    }
    const point = { time: values[0], bandwidth: values[1] * scale };
    if (point.time < 0 || point.bandwidth <= 0)
      throw new Error(`invalid trace value on line ${index + 1}: ${line}`);
    if (points.length && point.time < points[points.length - 1].time)
      throw new Error(`trace time moves backwards on line ${index + 1}`);
    points.push(point);
  }
  if (ignored.length)
    console.warn(`[quest-trace] ignored malformed CSV line(s): ${ignored.join(', ')}`);
  if (!points.length) throw new Error(`trace contains no bandwidth points: ${filename}`);
  // The first sample is the rate at t=0, even when a source trace labels it
  // with a non-zero absolute timestamp.
  const origin = points[0].time;
  return points.map(point => ({ ...point, time: point.time - origin }));
}

function command(program, args) {
  return new Promise((resolve, reject) => execFile(program, args,
    (error, stdout, stderr) => error
      ? reject(new Error(`${program} ${args.join(' ')}: ${(stderr || error.message).trim()}`))
      : resolve(stdout)));
}

function tbfArgs(mbps) {
  const kbit = `${Math.max(1, Math.round(mbps * 1000))}kbit`;
  // The bucket must fit the largest skb presented to the qdisc, not merely an
  // Ethernet MTU. TSO/GSO is enabled on the experiment NIC and commonly hands
  // Linux a ~64 KiB TCP skb. With the former 16-33 KiB bucket, one such skb at
  // the head could never accumulate enough tokens to leave: overlimits kept
  // rising while egress stayed at exactly zero for the rest of the run.
  // 128 KiB safely admits a normal GSO skb while remaining far below the old
  // multi-megabyte/10 ms bursts that distorted short ViVo transfers.
  const burstBytes = Math.max(TBF_BURST_FLOOR_BYTES, Math.ceil(mbps * 125));
  return ['handle', '1:', 'tbf', 'rate', kbit, 'burst', `${burstBytes}b`,
    'latency', '250ms'];
}

function tcByteCount(value, suffix = '') {
  const scale = suffix.toLowerCase() === 'k' ? 1024
    : suffix.toLowerCase() === 'm' ? 1024 * 1024
      : suffix.toLowerCase() === 'g' ? 1024 * 1024 * 1024 : 1;
  return Number(value) * scale;
}

async function readTbfStats(interfaceName) {
  const output = await command('tc', ['-s', 'qdisc', 'show', 'dev', interfaceName]);
  const block = output.split(/\n(?=qdisc )/)
    .find(value => /^qdisc tbf 1: root\b/m.test(value));
  if (!block)
    throw new Error(`root TBF is not active on ${interfaceName}; tc output: ${output.trim()}`);
  const sent = block.match(/\bSent\s+(\d+)\s+bytes\s+(\d+)\s+pkt\b/);
  if (!sent)
    throw new Error(`cannot read root TBF counters on ${interfaceName}: ${block.trim()}`);
  const limits = block.match(/\bdropped\s+(\d+),\s+overlimits\s+(\d+)\b/);
  const backlog = block.match(/\bbacklog\s+(\d+(?:\.\d+)?)([KMG]?)b\s+(\d+)p\b/i);
  const requeues = block.match(/\brequeues\s+(\d+)\b/);
  return {
    bytes: Number(sent[1]),
    packets: Number(sent[2]),
    dropped: limits ? Number(limits[1]) : 0,
    overlimits: limits ? Number(limits[2]) : 0,
    backlogBytes: backlog ? tcByteCount(backlog[1], backlog[2]) : 0,
    backlogPackets: backlog ? Number(backlog[3]) : 0,
    requeues: requeues ? Number(requeues[1]) : 0,
  };
}

function tbfIsWedged(previous, current) {
  return current.bytes === previous.bytes
    && current.overlimits > previous.overlimits;
}

function sleep(milliseconds) {
  return new Promise(resolve => setTimeout(resolve, milliseconds));
}

function getHealth(nodeUrl) {
  const url = new URL('/api/health', nodeUrl);
  return new Promise((resolve, reject) => {
    const request = http.get(url, { timeout: 1500 }, response => {
      let body = '';
      response.setEncoding('utf8');
      response.on('data', chunk => { body += chunk; });
      response.on('end', () => {
        if (response.statusCode < 200 || response.statusCode >= 300)
          return reject(new Error(`Node health returned HTTP ${response.statusCode}`));
        try { resolve(JSON.parse(body)); } catch (error) { reject(error); }
      });
    });
    request.on('timeout', () => request.destroy(new Error('Node health timed out')));
    request.on('error', reject);
  });
}

function publishGroundTruthBandwidth(nodeUrl, broadcastId, elapsedSeconds,
                                     bandwidthMbps) {
  const url = new URL('/api/ground-truth-bandwidth', nodeUrl);
  const body = JSON.stringify({ broadcastId, elapsedSeconds, bandwidthMbps });
  return new Promise((resolve, reject) => {
    const request = http.request(url, {
      method: 'POST', timeout: 1500,
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(body),
      },
    }, response => {
      let responseBody = '';
      response.setEncoding('utf8');
      response.on('data', chunk => { responseBody += chunk; });
      response.on('end', () => {
        if (response.statusCode < 200 || response.statusCode >= 300)
          return reject(new Error(
            `Node ground-truth endpoint returned HTTP ${response.statusCode}: ${responseBody}`));
        resolve();
      });
    });
    request.on('timeout', () => request.destroy(
      new Error('Node ground-truth bandwidth publish timed out')));
    request.on('error', reject);
    request.end(body);
  });
}

async function waitForNextBroadcast(nodeUrl) {
  let health;
  try { health = await getHealth(nodeUrl); }
  catch (error) {
    throw new Error(`cannot use --wait-for-broadcast: ${error.message}`);
  }
  const previous = health.currentBroadcastId || null;
  console.log(`[quest-trace] armed; waiting for Quest A on ${nodeUrl} `
    + `(current broadcast: ${previous || 'none'})`);
  for (;;) {
    await sleep(100);
    health = await getHealth(nodeUrl);
    const current = health.currentBroadcastId || null;
    if (current && current !== previous) {
      console.log(`[quest-trace] broadcast ${current} started; trace clock is running`);
      return current;
    }
  }
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (!options.traceFile || !fs.existsSync(options.traceFile)) usage();
  if (!Number.isFinite(options.scale) || options.scale <= 0)
    throw new Error('--scale must be positive');
  const points = loadTrace(options.traceFile, options.scale);
  if (options.validate) {
    const rates = points.map(point => point.bandwidth);
    console.log(`[quest-trace] valid: ${points.length} points, `
      + `${points[points.length - 1].time.toFixed(1)} s, `
      + `${Math.min(...rates).toFixed(2)}-${Math.max(...rates).toFixed(2)} Mbps`);
    return;
  }
  if (!validateIpv4(options.questIp))
    throw new Error('--quest-ip must be a valid IPv4 address');
  if (process.getuid && process.getuid() !== 0)
    throw new Error('tc requires root; run this command through sudo');
  const interfaceName = options.interfaceName || (() => {
    const route = execFileSync('ip', ['route', 'get', options.questIp], { encoding: 'utf8' });
    const match = route.match(/\bdev\s+(\S+)/);
    if (!match) throw new Error('cannot determine the routed interface for the Quest');
    return match[1];
  })();
  if (!/^[A-Za-z0-9_.:-]+$/.test(interfaceName))
    throw new Error(`invalid interface name ${JSON.stringify(interfaceName)}`);

  let ownsQdisc = false;
  const cleanup = async () => {
    if (!ownsQdisc) return;
    ownsQdisc = false;
    try { await command('tc', ['qdisc', 'del', 'dev', interfaceName, 'root']); }
    catch (_) { /* Cleanup is best effort, including after interface loss. */ }
  };
  activeCleanup = cleanup;
  // This script owns the root qdisc for the duration of one experiment.
  try { await command('tc', ['qdisc', 'del', 'dev', interfaceName, 'root']); }
  catch (_) { /* No replaceable root qdisc is a normal starting state. */ }
  await command('tc', ['qdisc', 'add', 'dev', interfaceName, 'root',
    ...tbfArgs(points[0].bandwidth)]);
  ownsQdisc = true;
  let previousStats = await readTbfStats(interfaceName);
  let previousStatsAt = process.hrtime.bigint();

  const stop = async exitCode => { await cleanup(); process.exit(exitCode); };
  process.on('SIGINT', () => { void stop(0); });
  process.on('SIGTERM', () => { void stop(0); });
  process.on('uncaughtException', error => {
    console.error(`[quest-trace] ${error.stack || error.message}`);
    void stop(1);
  });

  console.log(`[quest-trace] ${options.traceFile} -> Quest on ${interfaceName}`);
  console.log('[quest-trace] root TBF verified; shaping ALL egress on this interface '
    + '(ORBIT HTTP and baseline TCP)');
  console.log('[quest-trace] do not run unrelated bulk transfers on this interface during the experiment');
  const broadcastId = options.waitForBroadcast
    ? await waitForNextBroadcast(options.nodeUrl) : null;

  previousStats = await readTbfStats(interfaceName);
  previousStatsAt = process.hrtime.bigint();
  const started = previousStatsAt;
  let counterBaseBytes = 0;
  if (broadcastId)
    await publishGroundTruthBandwidth(
      options.nodeUrl, broadcastId, 0, points[0].bandwidth);
  console.log(`[quest-trace][0.0s] target ${points[0].bandwidth.toFixed(2)} Mbps; `
    + `burst ${(Math.max(TBF_BURST_FLOOR_BYTES,
      Math.ceil(points[0].bandwidth * 125)) / 1024).toFixed(1)} KiB`);
  for (let index = 1; index < points.length; index++) {
    const dueNanoseconds = BigInt(Math.round(points[index].time * 1e9));
    for (;;) {
      const remaining = dueNanoseconds - (process.hrtime.bigint() - started);
      if (remaining <= 0) break;
      await sleep(Math.min(100, Math.max(1, Number(remaining / 1000000n))));
    }
    const now = process.hrtime.bigint();
    const stats = await readTbfStats(interfaceName);
    const seconds = Number(now - previousStatsAt) / 1e9;
    const byteDelta = stats.bytes >= previousStats.bytes
      ? stats.bytes - previousStats.bytes : 0;
    const overlimitDelta = stats.overlimits >= previousStats.overlimits
      ? stats.overlimits - previousStats.overlimits : 0;
    const droppedDelta = stats.dropped >= previousStats.dropped
      ? stats.dropped - previousStats.dropped : 0;
    const measuredMbps = seconds > 0 ? byteDelta * 8 / seconds / 1e6 : 0;
    const wedged = tbfIsWedged(previousStats, stats);
    if (wedged) {
      // A qdisc whose counter is stationary while overlimits rises has an skb
      // it cannot dequeue. Replacing it drops that queued head packet; TCP
      // retransmits it, which is preferable to losing the entire remaining
      // experiment. Preserve the old byte counter for cumulative diagnostics.
      counterBaseBytes += stats.bytes;
      console.warn(`[quest-trace][${points[index].time.toFixed(1)}s] `
        + `TBF stalled with ${stats.backlogBytes} queued bytes and `
        + `${overlimitDelta} new overlimits; rebuilding qdisc`);
      await command('tc', ['qdisc', 'replace', 'dev', interfaceName, 'root',
        ...tbfArgs(points[index].bandwidth)]);
    } else {
      await command('tc', ['qdisc', 'change', 'dev', interfaceName, 'root',
        ...tbfArgs(points[index].bandwidth)]);
    }
    if (broadcastId)
      await publishGroundTruthBandwidth(
        options.nodeUrl, broadcastId,
        Number(process.hrtime.bigint() - started) / 1e9,
        points[index].bandwidth);
    previousStats = await readTbfStats(interfaceName);
    previousStatsAt = process.hrtime.bigint();
    const elapsed = Number(process.hrtime.bigint() - started) / 1e9;
    console.log(`[quest-trace][${elapsed.toFixed(1)}s] `
      + `target ${points[index].bandwidth.toFixed(2)} Mbps; prior cap `
      + `${points[index - 1].bandwidth.toFixed(2)}, egress ${measuredMbps.toFixed(2)} Mbps, `
      + `overlimits +${overlimitDelta}, dropped +${droppedDelta}, `
      + `backlog ${(stats.backlogBytes / 1024).toFixed(1)} KiB; `
      + `shaped ${((counterBaseBytes + (wedged ? 0 : stats.bytes)) / 1e6).toFixed(1)} MB`);
  }

  if (options.hold) {
    console.log('[quest-trace] trace complete; final rate remains active (Ctrl-C clears it)');
    await new Promise(() => {});
  } else {
    console.log('[quest-trace] trace complete; removing traffic shaping');
    await cleanup();
  }
}

if (require.main === module) {
  main().catch(async error => {
    console.error(`[quest-trace] ${error.stack || error.message}`);
    await activeCleanup();
    process.exit(1);
  });
}

module.exports = {
  TBF_BURST_FLOOR_BYTES,
  loadTrace,
  tbfArgs,
  tcByteCount,
  tbfIsWedged,
};
