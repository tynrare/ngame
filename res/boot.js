// agent: composer-2.5 | 2026-08-01 | boot registers scene modules | f6ec11
// agent: composer-2.5 | 2026-08-02 | register stress_spawn scene | adb274
// agent: composer-2.5 | 2026-08-09 | register rc scene | fbdaa0
// agent: grok-4.6 | 2026-08-28 | register fonts scene id | fc17e8
// agent: grok-4.6 | 2026-08-30 | register syncstats scene | f26d47
// agent: grok-4.6 | 2026-08-31 | register crawler scene id | 32add4
global.register("sphere", "scenes/sphere.js");
global.register("cube", "scenes/cube.js");
global.register("physics", "scenes/physics.js");
global.register("lockstep", "scenes/lockstep.js");
global.register("solar", "scenes/solar.js");
global.register("stacking", "scenes/stacking.js");
global.register("stress_spawn", "scenes/stress_spawn.js");
global.register("rc", "scenes/rc.js");
global.register("example", "scenes/example.js");
global.register("fonts", "scenes/fonts.js");
global.register("syncstats", "scenes/syncstats.js");
// agent: gpt-6-astra | 2026-09-05 | register shared draw API demo | 005756
global.register("crawler", "scenes/crawler.js");
global.register("draw", "scenes/draw.js");

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
// agent: gpt-6-astra | 2026-09-05 | register shared draw API demo | 005756
// agent: composer-2.5 | 2026-08-01 | boot registers scene modules | f6ec11
// agent: composer-2.5 | 2026-08-02 | register stress_spawn scene | adb274
// agent: composer-2.5 | 2026-08-09 | register rc scene | fbdaa0
// agent: grok-4.6 | 2026-08-28 | register fonts scene id | fc17e8
// agent: grok-4.6 | 2026-08-30 | register syncstats scene | f26d47
// agent: grok-4.6 | 2026-08-31 | register crawler scene id | 32add4
