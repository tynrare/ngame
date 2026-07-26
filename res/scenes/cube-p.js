class Cube {
  init() {
  }
  start() {
  }
  step(dt) {
    // e.g. global.set_position_x(this.index, 1)
  }
  stop() {
  }
  dispose() {
  }
}

class Scene {
	init() {
     /** @type {number} cube mesh id, optional */
     const test_cube_mesh = global.describe("mesh", "cube_a_m", { width: 1, height: 1, depth: 1 });
     /** @type {number} cube shader id */
     const test_cube_shader = global.describe("shader", "cube_a_s", { fragment: "cube.fs", vertex: "mesh.vs" });
     /** @type {number} cube model id */
     const test_cube_model = global.describe("model", "cube_a_mo", { mesh: "cube_a", shader: "cube_a_s"});

     // note: mode optional
     /** @type {number} cube entity id */
     const test_cube_entity = global.describe("entity", "cube_a_e", { model: "cube_a", mode: Cube });
	}
	start() {
     /** @type {number} cube entity index */
     // note: spawn accepts entity name or id
     this.test_cube = global.spawn("cube_a_e");

     // note: accepts object, three arguments, or one argument
     global.set_position(this.test_cube, { x: 0, y: 0, z: 0 });
     global.set_rotation(this.test_cube, 0, 0, 0);
     global.set_scale(this.test_cube, 1);
	}
	step(dt) {
     if (globals.get_input(KEY_A)) {
       const ry = global.get_rotation_y(this.test_cube);
       global.set_rotation_y(ry + dt * 1);
     }
     if (globals.get_input(KEY_D)) {
       const rot = global.get_rotation(this.test_cube);
       rot.y += dt * 1;
       global.set_rotation(this.test_cube, rot);
     }
	}
	stop() {
     // note: may be skipped, if C deletes scene complitely
     global.despawn(this.test_cube); 
     this.test_cube = null;
	}
	dispose() {
     global.dispose("mesh", "cube_a_m");
     //global.dispose("shader", "cube_a_m"); // may be skipped - C cleanups if scene disposed complitely
     global.dispose("model", "cube_a_mo");
     global.dispose("entity", "cube_a_e");
	}
}
