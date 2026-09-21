#!/usr/bin/env node
const assert = require("assert");
const {
  offlineTrajectorySamples, offlineTrajectoryPayload, sceneRelativeTrajectoryPayload,
} = require("./server");

const header = "t_s,playback_s,presentation_slot,scene,streaming,broadcast_id,x,y,z,yaw,pitch,roll";
const csv = [
  header,
  "1,,,-,1,old,0,0,0,0,0,0",
  "2,18.7,560,-,1,old,99,0,0,0,0,0",
  "3,19.9,598,-,1,old,99,0,0,0,0,0",
  "4,0.01,0,-,1,capture,1,0,0,0,0,0",
  "5,1.01,30,-,1,capture,3,0,0,0,0,0",
].join("\n");

assert.deepStrictEqual(offlineTrajectorySamples(csv, "capture"), [
  [0.01, 1, 0, 0, 0, 0, 0],
  [1.01, 3, 0, 0, 0, 0, 0],
]);

const restarted = [
  header,
  "1,18.7,560,-,1,capture,99,0,0,0,0,0",
  "2,19.9,598,-,1,capture,99,0,0,0,0,0",
  "3,0.01,0,-,1,capture,1,0,0,0,0,0",
  "4,10.01,300,-,1,capture,2,0,0,0,0,0",
].join("\n");
assert.deepStrictEqual(offlineTrajectorySamples(restarted, "capture"), [
  [0.01, 1, 0, 0, 0, 0, 0],
  [10.01, 2, 0, 0, 0, 0, 0],
]);

assert.deepStrictEqual(offlineTrajectoryPayload(csv, "archived"), {
  sourceBroadcastId: "capture",
  samples: [
    [0.01, 1, 0, 0, 0, 0, 0],
    [1.01, 3, 0, 0, 0, 0, 0],
  ],
});

const identityExtrinsic = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
assert.deepStrictEqual(sceneRelativeTrajectoryPayload({
  schemaVersion: 2,
  coordinateSpace: "open3d-camera-extrinsic-column-major",
  sourceBroadcastId: "p00-capture",
  samples: [[0, ...identityExtrinsic], [1, ...identityExtrinsic]],
}), {
  schemaVersion: 2,
  coordinateSpace: "open3d-camera-extrinsic-column-major",
  sourceBroadcastId: "p00-capture",
  samples: [[0, ...identityExtrinsic], [1, ...identityExtrinsic]],
});
assert.throws(() => sceneRelativeTrajectoryPayload({
  schemaVersion: 2,
  coordinateSpace: "open3d-camera-extrinsic-column-major",
  samples: [[1, ...identityExtrinsic], [0, ...identityExtrinsic]],
}), /invalid scene-relative/);

console.log("offline trajectory tests passed");
