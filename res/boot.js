// agent: composer-2.5 | 2026-08-01 | boot registers scene modules | f6ec11
global.register("sphere", "scenes/sphere.js");
global.register("cube", "scenes/cube.js");
global.register("physics", "scenes/physics.js");
global.register("lockstep", "scenes/lockstep.js");
global.register("solar", "scenes/solar.js");
global.register("stacking", "scenes/stacking.js");
global.register("example", "scenes/example.js");

function Boot() {}

Boot.prototype.init = function () {
  global.describe("scene", "view", {
    bg: { r: 8, g: 12, b: 20 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 2, z: 8 },
      target: { x: 0, y: 0, z: 0 },
      fovy: 45,
    },
  });
};

Boot.prototype.start = function (session) {
  if (global.is_server()) {
    global.change_scene("sphere");
  }
};

Boot.prototype.step = function (dt) {};

Boot.prototype.stop = function () {};

Boot.prototype.dispose = function () {};

global.module(Boot);
// agent: composer-2.5 | 2026-08-01 | boot registers scene modules | f6ec11
