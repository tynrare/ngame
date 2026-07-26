// agent: composer-2.5 | 2026-07-26 | cube js ES5 duktape scene | a7b8c9
function Cube() {}

Cube.prototype.init = function () {};
Cube.prototype.start = function () {};
Cube.prototype.step = function (dt) {};
Cube.prototype.stop = function () {};
Cube.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "cube_a_m", { width: 1, height: 1, depth: 1 });
  global.describe("shader", "cube_a_s", { fragment: "cube.fs", vertex: "mesh.vs" });
  global.describe("model", "cube_a_mo", { mesh: "cube_a", shader: "cube_a_s" });
  global.describe("entity", "cube_a_e", {
    model: "cube_a_mo",
    func: Cube,
    sync: "shared",
  });
};

Scene.prototype.start = function (session) {
  this.test_cube = global.spawn("cube_a_e", { entity_id: session.entity_id });
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
  global.dispose("model", "cube_a_mo");
  global.dispose("entity", "cube_a_e");
};
