// agent: grok-4.6 | 2026-08-30 | fonts JS labels and clock | 1f1153
function Cube() {}
Cube.prototype.init = function () {};
Cube.prototype.start = function () {};
Cube.prototype.step = function (dt) {};
Cube.prototype.stop = function () {};
Cube.prototype.dispose = function () {};

function Label() {}
Label.prototype.init = function () {};
Label.prototype.start = function () {};
Label.prototype.step = function (dt) {};
Label.prototype.stop = function () {};
Label.prototype.dispose = function () {};

function Clock() {}
Clock.prototype.init = function () {
  this.t = 0;
};
Clock.prototype.start = function () {};
Clock.prototype.step = function (dt) {
  this.t += dt;
  global.set_text(this.handle, "t=" + this.t.toFixed(2));
};
Clock.prototype.stop = function () {};
Clock.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "fonts_m", { width: 1, height: 1, depth: 1, shape: "cube" });
  global.describe("shader", "fonts_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 90, b: 120 },
  });
  global.describe("model", "fonts_mo", { mesh: "fonts_m", shader: "fonts_s" });
  global.describe("font", "sans", { src: "fonts/LiberationSans-Regular.ttf", draw: "msdf" });
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
  global.describe("label", "pangram_e", { font: "sans", func: Label, sync: "shared" });
  global.describe("label", "small_e", { font: "sans", func: Label, sync: "shared" });
  global.describe("label", "large_e", { font: "sans", func: Label, sync: "shared" });
  global.describe("label", "outline_e", { font: "sans", func: Label, sync: "shared" });
  global.describe("label", "world_e", { font: "sans", func: Label, sync: "shared" });
  global.describe("label", "clock_e", { font: "sans", func: Clock, sync: "shared" });
};

Scene.prototype.start = function (session) {
  this.cube = global.spawn("fonts_e", {
    key: "main",
    position: { x: 0, y: 0, z: 0 },
    scale: 1,
  });
  this.pangram = global.spawn("pangram_e", {
    key: "pangram",
    space: "screen",
    text: "The quick brown fox 12.5",
    size: 22,
    position: { x: 24, y: 72, z: 0 },
  });
  this.small = global.spawn("small_e", {
    key: "small",
    space: "screen",
    text: "small 14px",
    size: 14,
    tint: { r: 180, g: 220, b: 255 },
    position: { x: 24, y: 108, z: 0 },
  });
  this.large = global.spawn("large_e", {
    key: "large",
    space: "screen",
    text: "large 48px",
    size: 48,
    tint: { r: 255, g: 210, b: 120 },
    position: { x: 24, y: 168, z: 0 },
  });
  this.outlined = global.spawn("outline_e", {
    key: "outline",
    space: "screen",
    text: "outlined",
    size: 28,
    outline: 0.12,
    tint: { r: 120, g: 255, b: 160 },
    position: { x: 24, y: 220, z: 0 },
  });
  this.clock = global.spawn("clock_e", {
    key: "clock",
    space: "screen",
    text: "t=0.00",
    size: 22,
    position: { x: 24, y: 260, z: 0 },
  });
  this.world = global.spawn("world_e", {
    key: "msdf",
    space: "world",
    text: "MSDF",
    size: 0.45,
    position: { x: 0, y: 1.4, z: 0 },
  });
};

Scene.prototype.step = function (dt) {};

Scene.prototype.stop = function () {
  var keys = ["cube", "pangram", "small", "large", "outlined", "clock", "world"];
  for (var i = 0; i < keys.length; i++) {
    var k = keys[i];
    if (this[k]) {
      global.despawn(this[k]);
      this[k] = null;
    }
  }
};

Scene.prototype.dispose = function () {
  global.dispose("mesh", "fonts_m");
  global.dispose("shader", "fonts_s");
  global.dispose("model", "fonts_mo");
  global.dispose("font", "sans");
  global.dispose("entity", "fonts_e");
  global.dispose("label", "pangram_e");
  global.dispose("label", "small_e");
  global.dispose("label", "large_e");
  global.dispose("label", "outline_e");
  global.dispose("label", "world_e");
  global.dispose("label", "clock_e");
};

global.module(Scene);
// agent: grok-4.6 | 2026-08-30 | fonts JS labels and clock | 1f1153
