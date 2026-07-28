// agent: composer-2.5 | 2026-07-28 | shared js helpers not a scene | h1e2l3
var sceneHelpers = {};

sceneHelpers.noopEntity = function () {
  function E() {}
  E.prototype.init = function () {};
  E.prototype.start = function () {};
  E.prototype.step = function (dt) {};
  E.prototype.stop = function () {};
  E.prototype.dispose = function () {};
  return E;
};

sceneHelpers.view = function (opts) {
  opts = opts || {};
  var bg = opts.bg || [0, 0, 0];
  var cam = opts.camera || {};
  var desc = {
    bg: { r: bg[0], g: bg[1], b: bg[2] },
    camera: { fovy: cam.fovy != null ? cam.fovy : 45 },
  };
  if (cam.mode) {
    desc.camera.mode = cam.mode;
  }
  if (cam.position) {
    desc.camera.position = { x: cam.position[0], y: cam.position[1], z: cam.position[2] };
  }
  if (cam.target) {
    desc.camera.target = { x: cam.target[0], y: cam.target[1], z: cam.target[2] };
  }
  if (cam.orbit) {
    desc.camera.orbit = {
      radius: cam.orbit.radius != null ? cam.orbit.radius : 6,
      speed: cam.orbit.speed != null ? cam.orbit.speed : 0.6,
      height: cam.orbit.height != null ? cam.orbit.height : 2,
    };
  }
  global.describe("scene", "view", desc);
};

sceneHelpers.primitive = function (opts) {
  opts = opts || {};
  var p = opts.prefix || "obj";
  var shape = opts.shape || "cube";
  var tint = opts.tint || [255, 255, 255];
  var sync = opts.sync || "server";
  var entFunc = opts.entityFunc || sceneHelpers.noopEntity();
  var m = p + "_m";
  var s = p + "_s";
  var mo = p + "_mo";
  var e = p + "_e";
  global.describe("mesh", m, {
    width: 1,
    height: 1,
    depth: 1,
    shape: shape,
  });
  global.describe("shader", s, {
    fragment: opts.shader || (shape === "sphere" ? "shaders/sphere.fs" : "shaders/cube.fs"),
    vertex: "shaders/mesh.vs",
    tint: { r: tint[0], g: tint[1], b: tint[2] },
  });
  global.describe("model", mo, { mesh: m, shader: s });
  global.describe("entity", e, { model: mo, func: entFunc, sync: sync });
  if (opts.view) {
    sceneHelpers.view(opts.view);
  }
  return { entity: e, mesh: m, shader: s, model: mo };
};

sceneHelpers.disposePrimitive = function (prefix) {
  global.dispose("entity", prefix + "_e");
  global.dispose("model", prefix + "_mo");
  global.dispose("shader", prefix + "_s");
  global.dispose("mesh", prefix + "_m");
};

// agent: composer-2.5 | 2026-07-28 | shared js helpers not a scene | h1e2l3
