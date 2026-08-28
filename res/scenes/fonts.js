// agent: grok-4.6 | 2026-08-28 | fonts test scene module | 4a9d41
function Cube() {}

Cube.prototype.init = function () {};
Cube.prototype.start = function () {};
Cube.prototype.step = function (dt) {};
Cube.prototype.stop = function () {};
Cube.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "fonts_m", { width: 1, height: 1, depth: 1, shape: "cube" });
  global.describe("shader", "fonts_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 90, b: 120 },
  });
  global.describe("model", "fonts_mo", { mesh: "fonts_m", shader: "fonts_s" });
  global.describe("scene", "view", {
    render: "simple",
    bg: { r: 12, g: 14, b: 22 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 1.6, z: 5 },
      target: { x: 0, y: 0.6, z: 0 },
      fovy: 45,
    },
  });
  global.describe("entity", "fonts_e", {
    model: "fonts_mo",
    func: Cube,
    sync: "shared",
  });
};

Scene.prototype.start = function (session) {
  this.cube = global.spawn("fonts_e", {
    key: "main",
    position: { x: 0, y: 0, z: 0 },
    scale: 1,
  });
};

Scene.prototype.step = function (dt) {};

Scene.prototype.stop = function () {
  if (this.cube) {
    global.despawn(this.cube);
    this.cube = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("mesh", "fonts_m");
  global.dispose("shader", "fonts_s");
  global.dispose("model", "fonts_mo");
  global.dispose("entity", "fonts_e");
};

global.module(Scene);
// agent: grok-4.6 | 2026-08-28 | fonts test scene module | 4a9d41
