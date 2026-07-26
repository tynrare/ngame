class Cube {
  init() {
  }
  start() {
  }
  step(dt) {
    // this step depends on enity mode.
    // server mode does not run on client
    // shared mode does not run on server
    // owner mode does not run on other clients
    // local mode runs only on one client
    if (globals.get_input(KEY_W)) {
      const rx = global.get_rotation_x(this.index);
      global.set_rotation_x(rx + dt * 1);
    }
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
     // note: sync options:
     // - server. update complitely runs in server. It has no inputs, skip for now
     // - shared. any client could post entity update
     // - owner. One controller authors; entity exists on all peers
     // - local. Spawn/update only on this client; no wire
     /** @type {number} cube entity id */
     const test_cube_entity = global.describe("entity", "cube_a_e", { model: "cube_a", func: Cube, mode: "shared" });
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
       global.set_rotation_y(this.test_cube, ry + dt * 1);
     }
     if (globals.get_input(KEY_D)) {
       const rot = global.get_rotation(this.test_cube);
       rot.y += dt * 1;
       global.set_rotation(this.test_cube, rot);
     }
     // all changes delta is stored and flushed at step end
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
