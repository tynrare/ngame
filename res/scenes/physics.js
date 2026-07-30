// agent: composer-2.5 | 2026-07-29 | physics lockstep scene js | c3e03d
// agent: composer-2.5 | 2026-07-30 | lockstep physics sync note | e2ce57
// agent: composer-2.5 | 2026-07-30 | physics sim flag note only | d4ecc2
function Box() {}
Box.prototype.init = function () {};
Box.prototype.start = function () {};
Box.prototype.step = function (dt) {};
Box.prototype.fixed_step = function (dt) {};
Box.prototype.stop = function () {};
Box.prototype.dispose = function () {};

function Ground() {}
Ground.prototype.init = function () {};
Ground.prototype.start = function () {};
Ground.prototype.step = function (dt) {};
Ground.prototype.fixed_step = function (dt) {};
Ground.prototype.stop = function () {};
Ground.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  // Visual full sizes match box3d HelloWorld: cube half=1, ground half=(50,10,50).
  // GenMeshCube uses width*1.5 — set dims so drawn size ~= physics full size.
  global.describe("mesh", "box_m", { width: 1.333, height: 1.333, depth: 1.333, shape: "cube" });
  global.describe("shader", "box_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 220, g: 90, b: 40 },
  });
  global.describe("model", "box_mo", { mesh: "box_m", shader: "box_s" });

  global.describe("mesh", "ground_m", { width: 66.67, height: 13.33, depth: 66.67, shape: "cube" });
  global.describe("shader", "ground_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 110, b: 70 },
  });
  global.describe("model", "ground_mo", { mesh: "ground_m", shader: "ground_s" });

  // Physics: HelloWorld — gravity -10, ground at y=-10 with hy=10 (top at y=0),
  // dynamic cube half-extent 1 at y=4 (rests near y=1).
  global.describe("shape", "box_shape", {
    type: "box",
    hx: 1,
    hy: 1,
    hz: 1,
    density: 1,
    friction: 0.3,
  });
  global.describe("shape", "ground_shape", {
    type: "box",
    hx: 50,
    hy: 10,
    hz: 50,
    density: 0,
    friction: 0.6,
  });
  global.describe("body", "box_body", { type: "dynamic", shape: "box_shape" });
  global.describe("body", "ground_body", { type: "static", shape: "ground_shape" });

  // sim: physics-only (bodies). Bodiless entities still use entity.sync (cube-compatible).
  // Under lockstep, body entity sync is ignored; transforms for bodies are not streamed.
  global.describe("scene", "view", {
    sim: "lockstep",
    bg: { r: 24, g: 28, b: 36 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 12, z: 24 },
      target: { x: 0, y: 1, z: 0 },
      fovy: 45,
    },
  });

  // agent: composer-2.5 | 2026-07-30 | physics sim flag note only | d4ecc2
  // sync on bodies unused under sim:lockstep; kept for sim:server compatibility.
  global.describe("entity", "box_e", {
    model: "box_mo",
    body: "box_body",
    func: Box,
    sync: "server",
  });
  global.describe("entity", "ground_e", {
    model: "ground_mo",
    body: "ground_body",
    func: Ground,
    sync: "server",
  });
};

Scene.prototype.start = function (session) {
  // Stable key order: box then ground (alphabetical) for identical body create order.
  this.box = global.spawn("box_e", {
    key: "box",
    position: { x: 0.5, y: 10, z: 0 },
    scale: 1,
  });
  this.ground = global.spawn("ground_e", {
    key: "ground",
    position: { x: 0, y: -10, z: 0 },
    scale: 1,
  });
};

Scene.prototype.step = function (dt) {};
Scene.prototype.fixed_step = function (dt) {};

Scene.prototype.stop = function () {
  if (this.box) {
    global.despawn(this.box);
    this.box = null;
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

// agent: composer-2.5 | 2026-07-29 | physics lockstep scene js | c3e03d
// agent: composer-2.5 | 2026-07-30 | lockstep physics sync note | e2ce57
// agent: composer-2.5 | 2026-07-30 | physics sim flag note only | d4ecc2
