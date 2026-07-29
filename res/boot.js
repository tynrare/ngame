// agent: composer-2.5 | 2026-07-29 | server-only boot routes to sphere | a3c7e4
function Scene() {}

Scene.prototype.init = function () {
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

Scene.prototype.start = function (session) {
  if (global.is_server()) {
    global.change_scene("sphere");
  }
};

Scene.prototype.step = function (dt) {};

Scene.prototype.stop = function () {};

Scene.prototype.dispose = function () {};

// agent: composer-2.5 | 2026-07-29 | server-only boot routes to sphere | a3c7e4
