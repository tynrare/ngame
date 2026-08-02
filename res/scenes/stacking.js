// agent: composer-2.5 | 2026-08-01 | stacking wires sample-shooting | 76e351
// Port of box3d samples/sample_stacking.cpp BoxStack.
// No entity funcs (static debris). Keyless spawn — checksum order is create order
// when keys are empty (stable across peers with identical Scene.start loops).
// count=30 (+ground) stays under NG_SESSION_SPAWN_MAX(32).

global.register("sample-shooting", "../modules/sample_shooting.js");

function Scene() {}

Scene.prototype.init = function () {
  global.wire("sample-shooting");

  var a = 0.5;
  var mesh = (2.0 * a) / 1.5;

  global.describe("mesh", "box_m", { width: mesh, height: mesh, depth: mesh, shape: "cube" });
  global.describe("shader", "box_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 220, g: 90, b: 40 },
  });
  global.describe("model", "box_mo", { mesh: "box_m", shader: "box_s" });

  // AddGroundBox(40): half (40,1,40) at y=-1, top at 0.
  global.describe("mesh", "ground_m", { width: 53.33, height: 1.333, depth: 53.33, shape: "cube" });
  global.describe("shader", "ground_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 110, b: 70 },
  });
  global.describe("model", "ground_mo", { mesh: "ground_m", shader: "ground_s" });

  global.describe("shape", "box_shape", {
    type: "box",
    hx: a,
    hy: a,
    hz: a,
    density: 1,
    friction: 0.3,
  });
  global.describe("shape", "ground_shape", {
    type: "box",
    hx: 40,
    hy: 1,
    hz: 40,
    density: 0,
    friction: 0.6,
  });
  global.describe("body", "box_body", { type: "dynamic", shape: "box_shape" });
  global.describe("body", "ground_body", { type: "static", shape: "ground_shape" });

  global.describe("scene", "view", {
    sim: "hybrid",
    bg: { r: 24, g: 28, b: 36 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 15, z: 25 },
      target: { x: 0, y: 10, z: 0 },
      fovy: 45,
    },
  });

  global.describe("entity", "box_e", {
    model: "box_mo",
    body: "box_body",
    sync: "server",
  });
  global.describe("entity", "ground_e", {
    model: "ground_mo",
    body: "ground_body",
    sync: "server",
  });

  this.a = a;
  this.count = 30;
};

Scene.prototype.start = function (session) {
  this.boxes = [];
  var a = this.a;
  var i;
  for (i = 0; i < this.count; i++) {
    this.boxes.push(
      global.spawn("box_e", {
        position: { x: 0, y: 1.5 * a + 2.5 * a * i, z: 0 },
        scale: 1,
      })
    );
  }
  this.ground = global.spawn("ground_e", {
    position: { x: 0, y: -1, z: 0 },
    scale: 1,
  });
};

Scene.prototype.step = function (dt) {};
Scene.prototype.fixed_step = function (dt) {};

Scene.prototype.stop = function () {
  var i;
  if (this.boxes) {
    for (i = 0; i < this.boxes.length; i++) {
      if (this.boxes[i]) {
        global.despawn(this.boxes[i]);
      }
    }
    this.boxes = null;
  }
  if (this.ground) {
    global.despawn(this.ground);
    this.ground = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("entity", "box_e");
  global.dispose("entity", "ground_e");
  global.dispose("body", "box_body");
  global.dispose("body", "ground_body");
  global.dispose("shape", "box_shape");
  global.dispose("shape", "ground_shape");
  global.dispose("model", "box_mo");
  global.dispose("model", "ground_mo");
  global.dispose("shader", "box_s");
  global.dispose("shader", "ground_s");
  global.dispose("mesh", "box_m");
  global.dispose("mesh", "ground_m");
};

global.module(Scene);
// agent: composer-2.5 | 2026-08-01 | stacking wires sample-shooting | 76e351
