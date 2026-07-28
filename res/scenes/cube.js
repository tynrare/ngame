// agent: composer-2.5 | 2026-07-28 | cube explicit describes restored | r1c2u3
function Cube() {}

Cube.prototype.init = function () {};
Cube.prototype.start = function () {};
Cube.prototype.step = function (dt) {};
Cube.prototype.stop = function () {};
Cube.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "cube_a_m", { width: 1, height: 1, depth: 1, shape: "cube" });
  global.describe("shader", "cube_a_s", {
    fragment: "shaders/cube.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 255, g: 140, b: 51 },
  });
  global.describe("model", "cube_a_mo", { mesh: "cube_a_m", shader: "cube_a_s" });
  global.describe("scene", "view", {
    bg: { r: 48, g: 24, b: 8 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 2.5, z: 6 },
      target: { x: 0, y: 0, z: 0 },
      fovy: 45,
    },
  });
  global.describe("entity", "cube_a_e", {
    model: "cube_a_mo",
    func: Cube,
    sync: "shared",
  });
};

Scene.prototype.start = function (session) {
  this.test_cube = global.spawn("cube_a_e");
  global.set_position(this.test_cube, { x: 0, y: 0, z: 0 });
  global.set_rotation(this.test_cube, 0, 0, 0);
  global.set_scale(this.test_cube, 1);
};

Scene.prototype.step = function (dt) {
  if (global.get_input(KEY_A)) {
    var ry = global.get_rotation_y(this.test_cube);
    global.set_rotation_y(this.test_cube, ry - dt * 1.5);
  }
  if (global.get_input(KEY_D)) {
    var rot = global.get_rotation(this.test_cube);
    rot.y += dt * 1.5;
    global.set_rotation(this.test_cube, rot);
  }
};

Scene.prototype.stop = function () {
  if (this.test_cube) {
    global.despawn(this.test_cube);
    this.test_cube = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("mesh", "cube_a_m");
  global.dispose("shader", "cube_a_s");
  global.dispose("model", "cube_a_mo");
  global.dispose("entity", "cube_a_e");
};

// agent: composer-2.5 | 2026-07-28 | cube explicit describes restored | r1c2u3
