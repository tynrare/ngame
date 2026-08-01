// agent: composer-2.5 | 2026-08-01 | sample shooting action migrate | 912cd8
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | b0dd06
// agent: composer-2.5 | 2026-08-01 | shoot key peer tick only | e461f5
// agent: composer-2.5 | 2026-08-01 | propose view only tip overwrite | b8d9cb
// agent: composer-2.5 | 2026-08-01 | drop is_server propose gate | 2574db
// Propose from local input + camera; tip overwrite dedupes dual-heap step.
function SampleShooting() {}

SampleShooting.prototype.init = function () {
  var mesh = (2.0 * 0.25) / 1.5;
  global.describe("mesh", "shoot_ball_m", {
    width: mesh,
    height: mesh,
    depth: mesh,
    shape: "sphere",
  });
  global.describe("shader", "shoot_ball_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 240, g: 220, b: 60 },
  });
  global.describe("model", "shoot_ball_mo", { mesh: "shoot_ball_m", shader: "shoot_ball_s" });
  global.describe("shape", "shoot_ball_shape", {
    type: "sphere",
    radius: 0.25,
    density: 4,
    friction: 0.3,
  });
  global.describe("body", "shoot_ball_body", { type: "dynamic", shape: "shoot_ball_shape" });
  global.describe("entity", "shoot_ball_e", {
    model: "shoot_ball_mo",
    body: "shoot_ball_body",
    sync: "server",
  });
  this.balls = [];
  this.was_f = false;
  global.action_register(this, "action_fire");
};

SampleShooting.prototype.start = function (session) {};

SampleShooting.prototype.step = function (dt) {
  var down = global.get_local_input(global.KEY_F);
  /* Tip overwrite allows both heaps to propose; later step (view) wins camera. */
  if (down && !this.was_f) {
    var cam = global.get_view_camera();
    if (cam && cam.position && cam.target) {
      var dx = cam.target.x - cam.position.x;
      var dy = cam.target.y - cam.position.y;
      var dz = cam.target.z - cam.position.z;
      var len = Math.sqrt(dx * dx + dy * dy + dz * dz);
      if (len > 1e-4) {
        dx /= len;
        dy /= len;
        dz /= len;
        var speed = 20.0;
        var ox = cam.position.x + dx * 2.0;
        var oy = cam.position.y + dy * 2.0;
        var oz = cam.position.z + dz * 2.0;
        global.action(
          "action_fire",
          { x: ox, y: oy, z: oz },
          { x: dx, y: dy, z: dz },
          speed
        );
      }
    }
  }
  this.was_f = down;
};

SampleShooting.prototype.fixed_step = function (dt) {};

SampleShooting.prototype.action_fire = function (ox, oy, oz, dx, dy, dz, speed) {
  var peer = global.action_peer();
  var tick = global.action_tick();
  /* One action/peer/tick — key is fully determined by confirm payload. */
  var key = "sb_" + peer + "_" + tick;
  var h = global.spawn("shoot_ball_e", {
    key: key,
    position: { x: ox, y: oy, z: oz },
    scale: 1,
  });
  if (h) {
    this.balls.push(h);
    global.set_linear_velocity(h, { x: dx * speed, y: dy * speed, z: dz * speed });
  }
};

SampleShooting.prototype.stop = function () {
  var i;
  if (this.balls) {
    for (i = 0; i < this.balls.length; i++) {
      if (this.balls[i]) {
        global.despawn(this.balls[i]);
      }
    }
    this.balls = [];
  }
};

SampleShooting.prototype.dispose = function () {
  global.dispose("entity", "shoot_ball_e");
  global.dispose("body", "shoot_ball_body");
  global.dispose("shape", "shoot_ball_shape");
  global.dispose("model", "shoot_ball_mo");
  global.dispose("shader", "shoot_ball_s");
  global.dispose("mesh", "shoot_ball_m");
};

global.module(SampleShooting);
// agent: composer-2.5 | 2026-08-01 | sample shooting action migrate | 912cd8
// agent: composer-2.5 | 2026-08-01 | rename input get_local_any_peer | b0dd06
// agent: composer-2.5 | 2026-08-01 | shoot key peer tick only | e461f5
// agent: composer-2.5 | 2026-08-01 | propose view only tip overwrite | b8d9cb
// agent: composer-2.5 | 2026-08-01 | drop is_server propose gate | 2574db
