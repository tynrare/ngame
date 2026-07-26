// agent: composer-2.5 | 2026-07-26 | cube scene class lifecycle | c4d5e6
class Cube {
  init() {}
  start() {}
  step(dt) {
    // shared: runs on all clients; server skips entity steps
  }
  stop() {}
  dispose() {}
}

class Scene {
  init() {
    global.describe("mesh", "cube_a_m", { width: 1, height: 1, depth: 1 });
    global.describe("shader", "cube_a_s", { fragment: "cube.fs", vertex: "mesh.vs" });
    global.describe("model", "cube_a_mo", { mesh: "cube_a", shader: "cube_a_s" });
    global.describe("entity", "cube_a_e", {
      model: "cube_a_mo",
      func: Cube,
      sync: "shared",
    });
  }

  start(session) {
    this.test_cube = global.spawn("cube_a_e", { entity_id: session.entity_id });
    global.set_position(this.test_cube, { x: 0, y: 0, z: 0 });
    global.set_rotation(this.test_cube, 0, 0, 0);
    global.set_scale(this.test_cube, 1);
  }

  step(dt) {
    if (global.get_input(KEY_A)) {
      const ry = global.get_rotation_y(this.test_cube);
      global.set_rotation_y(this.test_cube, ry - dt * 1.5);
    }
    if (global.get_input(KEY_D)) {
      const rot = global.get_rotation(this.test_cube);
      rot.y += dt * 1.5;
      global.set_rotation(this.test_cube, rot);
    }
  }

  stop() {
    if (this.test_cube) {
      global.despawn(this.test_cube);
      this.test_cube = null;
    }
  }

  dispose() {
    global.dispose("mesh", "cube_a_m");
    global.dispose("model", "cube_a_mo");
    global.dispose("entity", "cube_a_e");
  }
}
