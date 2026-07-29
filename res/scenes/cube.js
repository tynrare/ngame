// agent: composer-2.5 | 2026-07-28 | cube explicit describes restored | r1c2u3
// agent: composer-2.5 | 2026-07-29 | cube multi spawn opts | e3adac
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
  // agent: codex-5.3 | 2026-07-29 | set transform bandwidth mode | c6f310
  global.describe("entity", "cube_a_e", {
    model: "cube_a_mo",
    func: Cube,
    sync: "shared",
    bandwidth: "transform"
  });
};

Scene.prototype.start = function (session) {
  // agent: composer-2.5 | 2026-07-29 | cube multi spawn opts | e3adac
  this.test_cube = global.spawn("cube_a_e", {
    key: "main",
    position: { x: 0, y: 0, z: 0 },
    rotation: { x: 0, y: 0, z: 0 },
    scale: 1
  });
  this.side_cube = global.spawn("cube_a_e", {
    key: "side",
    position: { x: 2, y: 0, z: 0 },
    scale: 0.75
  });
  this.last_mouse = null;
};

Scene.prototype.step = function (dt) {
  // agent: composer-2.5 | 2026-07-29 | mouse delta only authorship | c3a1f8
  // Only the client whose mouse actually moved authors position.
  var mouse = global.get_mouse_pos();
  if (mouse) {
    if (!this.last_mouse) {
      this.last_mouse = { x: mouse.x, y: mouse.y };
    } else {
      var mdx = mouse.x - this.last_mouse.x;
      var mdy = mouse.y - this.last_mouse.y;
      if (mdx * mdx + mdy * mdy > 0.25) {
        this.last_mouse.x = mouse.x;
        this.last_mouse.y = mouse.y;
        var hit = global.raycast_plane_y(0);
        if (hit && hit.hit) {
          // agent: composer-2.5 | 2026-07-29 | keep position on y=0 plane | 92e977
          global.set_position(this.test_cube, { x: hit.x, y: 0, z: hit.z });
        }
      }
    }
  }

  // agent: codex-5.3 | 2026-07-29 | add second axis on W S | f5e8c2
  if (global.get_input(KEY_A)) {
    var ry = global.get_rotation_y(this.test_cube);
    global.set_rotation_y(this.test_cube, ry - dt * 1.5);
  }
  if (global.get_input(KEY_D)) {
    var ry2 = global.get_rotation_y(this.test_cube);
    global.set_rotation_y(this.test_cube, ry2 + dt * 1.5);
  }
  if (global.get_input(KEY_W)) {
    var rx = global.get_rotation_x(this.test_cube);
    global.set_rotation_x(this.test_cube, rx - dt * 1.5);
  }
  if (global.get_input(KEY_S)) {
    var rx2 = global.get_rotation_x(this.test_cube);
    global.set_rotation_x(this.test_cube, rx2 + dt * 1.5);
  }
};

Scene.prototype.stop = function () {
  if (this.test_cube) {
    global.despawn(this.test_cube);
    this.test_cube = null;
  }
  if (this.side_cube) {
    global.despawn(this.side_cube);
    this.side_cube = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("mesh", "cube_a_m");
  global.dispose("shader", "cube_a_s");
  global.dispose("model", "cube_a_mo");
  global.dispose("entity", "cube_a_e");
};

// agent: composer-2.5 | 2026-07-29 | cube multi spawn opts | e3adac
// agent: composer-2.5 | 2026-07-29 | mouse delta only authorship | c3a1f8
// agent: composer-2.5 | 2026-07-29 | keep position on y=0 plane | 92e977
// agent: codex-5.3 | 2026-07-29 | add second axis on W S | f5e8c2
// agent: codex-5.3 | 2026-07-29 | set transform bandwidth mode | c6f310
// agent: composer-2.5 | 2026-07-28 | cube explicit describes restored | r1c2u3
