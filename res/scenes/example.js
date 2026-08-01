// agent: composer-2.5 | 2026-07-28 | example scene using helpers | e1x2a3
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 0b8d0c
function Scene() {}

Scene.prototype.init = function () {
  sceneHelpers.primitive({
    prefix: "example",
    shape: "cube",
    sync: "shared",
    tint: [200, 120, 255],
    view: {
      bg: [24, 12, 36],
      camera: { mode: "fixed", position: [0, 2, 7], target: [0, 0, 0] },
    },
  });
};

Scene.prototype.start = function (session) {
  // agent: composer-2.5 | 2026-07-29 | example spawn transform opts | 0d63bf
  this.cube = global.spawn("example_e", {
    position: { x: 0, y: 0, z: 0 }
  });
};

Scene.prototype.step = function (dt) {
  if (global.get_any_input(KEY_A)) {
    var ry = global.get_rotation_y(this.cube);
    global.set_rotation_y(this.cube, ry - dt);
  }
};

Scene.prototype.stop = function () {
  if (this.cube) {
    global.despawn(this.cube);
    this.cube = null;
  }
};

Scene.prototype.dispose = function () {
  sceneHelpers.disposePrimitive("example");
};

// agent: composer-2.5 | 2026-07-28 | example scene using helpers | e1x2a3
// agent: composer-2.5 | 2026-07-29 | example spawn transform opts | 0d63bf

// agent: composer-2.5 | 2026-08-01 | export scene via global.module | d0182e
global.module(Scene);
// agent: composer-2.5 | 2026-08-01 | export scene via global.module | d0182e
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | 0b8d0c
// agent: composer-2.5 | 2026-08-01 | cube example any input again | 95e2fa
