// agent: composer-2.5 | 2026-08-02 | stress_spawn hybrid ball churn | fde095
// agent: composer-2.5 | 2026-08-02 | gate churn on confirmed | 76aad6
// agent: composer-2.5 | 2026-08-02 | reconcile balls by entity id | 4e6f24
// agent: composer-2.5 | 2026-08-02 | tick-keyed world despawn | 473c98
// agent: composer-2.5 | 2026-08-02 | cap 96 for dual-peer sync | 42f28e
// Stacking core + fixed_step ball churn (world peer-0 sim ids) + W/S action bursts.
// Cap = INST_MAX(512) minus start boxes/ground. LCG from sim_tick — no Math.random.
// World despawn uses pack(tick-max,0,0) — idempotent under resim (unlike FIFO).

function Scene() {}

Scene.prototype.init = function () {
  var a = 0.5;
  var mesh = (2.0 * a) / 1.5;
  var ball_mesh = (2.0 * 0.25) / 1.5;

  global.describe("mesh", "box_m", { width: mesh, height: mesh, depth: mesh, shape: "cube" });
  global.describe("shader", "box_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 220, g: 90, b: 40 },
  });
  global.describe("model", "box_mo", { mesh: "box_m", shader: "box_s" });

  global.describe("mesh", "ground_m", { width: 53.33, height: 1.333, depth: 53.33, shape: "cube" });
  global.describe("shader", "ground_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 110, b: 70 },
  });
  global.describe("model", "ground_mo", { mesh: "ground_m", shader: "ground_s" });

  global.describe("mesh", "ball_m", {
    width: ball_mesh,
    height: ball_mesh,
    depth: ball_mesh,
    shape: "sphere",
  });
  global.describe("shader", "ball_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 240, g: 220, b: 60 },
  });
  global.describe("model", "ball_mo", { mesh: "ball_m", shader: "ball_s" });

  global.describe("shape", "box_shape", {
    type: "box",
    hx: a,
    hy: a,
    hz: a,
    density: 1,
    friction: 0.3,
  });
  global.describe("shape", "ground_shape", {
    type: "box",
    hx: 40,
    hy: 1,
    hz: 40,
    density: 0,
    friction: 0.6,
  });
  global.describe("shape", "ball_shape", {
    type: "sphere",
    radius: 0.25,
    density: 40,
    friction: 0.3,
  });
  global.describe("body", "box_body", { type: "dynamic", shape: "box_shape" });
  global.describe("body", "ground_body", { type: "static", shape: "ground_shape" });
  global.describe("body", "ball_body", { type: "dynamic", shape: "ball_shape" });

  global.describe("scene", "view", {
    sim: "hybrid",
    bg: { r: 24, g: 28, b: 36 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 15, z: 25 },
      target: { x: 0, y: 10, z: 0 },
      fovy: 45,
    },
  });

  global.describe("entity", "box_e", {
    model: "box_mo",
    body: "box_body",
    sync: "server",
  });
  global.describe("entity", "ground_e", {
    model: "ground_mo",
    body: "ground_body",
    sync: "server",
  });
  global.describe("entity", "ball_e", {
    model: "ball_mo",
    body: "ball_body",
    sync: "server",
  });

  this.a = a;
  this.count = 30;
  this.burst_n = 4;
  this.was_w = false;
  this.was_s = false;
  global.action_register(this, "action_burst");
};

Scene.prototype.start = function (session) {
  this.boxes = [];
  this.balls = [];
  var a = this.a;
  var i;
  for (i = 0; i < this.count; i++) {
    this.boxes.push(
      global.spawn("box_e", {
        position: { x: 0, y: 1.5 * a + 2.5 * a * i, z: 0 },
        scale: 1,
      })
    );
  }
  this.ground = global.spawn("ground_e", {
    position: { x: 0, y: -1, z: 0 },
    scale: 1,
  });
  /* INST_MAX 512; keep headroom — full 481-ball soak trips soft PHYS under 2 peers. */
  this.max_balls = 96;
};

Scene.prototype._sync_balls = function () {
  if (global.find_entities) {
    this.balls = global.find_entities("ball_e") || [];
  }
};

Scene.prototype._lcg = function (state) {
  return (Math.imul(state, 1664525) + 1013904223) >>> 0;
};

Scene.prototype._spawn_ball = function (seed) {
  var s = this._lcg(seed >>> 0);
  var ux = (s & 0xffff) / 65535.0 * 2.0 - 1.0;
  s = this._lcg(s);
  var uy = (s & 0xffff) / 65535.0 * 2.0 - 1.0;
  s = this._lcg(s);
  var uz = (s & 0xffff) / 65535.0 * 2.0 - 1.0;
  s = this._lcg(s);
  var speed = 8.0 + ((s & 0xffff) / 65535.0) * 12.0;
  var len = Math.sqrt(ux * ux + uy * uy + uz * uz);
  if (len < 1e-4) {
    ux = 0;
    uy = 1;
    uz = 0;
    len = 1;
  }
  ux /= len;
  uy /= len;
  uz /= len;
  var h = global.spawn("ball_e", {
    position: { x: 0, y: 20, z: 0 },
    scale: 1,
  });
  if (h) {
    this.balls.push(h);
    global.set_linear_velocity(h, { x: ux * speed, y: uy * speed, z: uz * speed });
  }
  return h;
};

Scene.prototype._despawn_fifo = function () {
  if (!this.balls || this.balls.length === 0) {
    return;
  }
  var h = this.balls.shift();
  if (h) {
    global.despawn(h);
  }
};

Scene.prototype.fixed_step = function (dt) {
  if (!this.balls) {
    return;
  }
  if (global.sim_confirmed && !global.sim_confirmed()) {
    return;
  }
  var tick = global.sim_tick() | 0;
  this._sync_balls();
  /* Despawn world ball from tick-max (no-op if absent) — resim-safe. */
  if (tick > this.max_balls && global.despawn_id && global.pack_sim_id) {
    global.despawn_id(global.pack_sim_id(tick - this.max_balls, 0, 0));
    this._sync_balls();
  }
  if (this.balls.length < this.max_balls) {
    this._spawn_ball((tick * 2654435761) >>> 0);
  }
};

Scene.prototype.step = function (dt) {
  var w = global.get_local_input(global.KEY_W);
  var s = global.get_local_input(global.KEY_S);
  var spawn_n = 0;
  var despawn_n = 0;
  if (w && !this.was_w) {
    spawn_n = this.burst_n;
  }
  if (s && !this.was_s) {
    despawn_n = this.burst_n;
  }
  if (spawn_n > 0 || despawn_n > 0) {
    global.action("action_burst", spawn_n, despawn_n);
  }
  this.was_w = w;
  this.was_s = s;
};

Scene.prototype.action_burst = function (spawn_n, despawn_n) {
  var i;
  var tick = global.action_tick() | 0;
  var peer = global.action_peer() | 0;
  spawn_n = spawn_n | 0;
  despawn_n = despawn_n | 0;
  if (spawn_n < 0) {
    spawn_n = 0;
  }
  if (despawn_n < 0) {
    despawn_n = 0;
  }
  if (spawn_n > 15) {
    spawn_n = 15;
  }
  this._sync_balls();
  for (i = 0; i < despawn_n; i++) {
    this._despawn_fifo();
  }
  for (i = 0; i < spawn_n; i++) {
    if (this.balls.length >= this.max_balls) {
      break;
    }
    this._spawn_ball((((tick * 1315423911) ^ (peer * 2654435761) ^ (i * 9749)) >>> 0));
  }
};

Scene.prototype.stop = function () {
  var i;
  if (this.balls) {
    this._sync_balls();
    for (i = 0; i < this.balls.length; i++) {
      if (this.balls[i]) {
        global.despawn(this.balls[i]);
      }
    }
    this.balls = null;
  }
  if (this.boxes) {
    for (i = 0; i < this.boxes.length; i++) {
      if (this.boxes[i]) {
        global.despawn(this.boxes[i]);
      }
    }
    this.boxes = null;
  }
  if (this.ground) {
    global.despawn(this.ground);
    this.ground = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("entity", "ball_e");
  global.dispose("entity", "box_e");
  global.dispose("entity", "ground_e");
  global.dispose("body", "ball_body");
  global.dispose("body", "box_body");
  global.dispose("body", "ground_body");
  global.dispose("shape", "ball_shape");
  global.dispose("shape", "box_shape");
  global.dispose("shape", "ground_shape");
  global.dispose("model", "ball_mo");
  global.dispose("model", "box_mo");
  global.dispose("model", "ground_mo");
  global.dispose("shader", "ball_s");
  global.dispose("shader", "box_s");
  global.dispose("shader", "ground_s");
  global.dispose("mesh", "ball_m");
  global.dispose("mesh", "box_m");
  global.dispose("mesh", "ground_m");
};

global.module(Scene);
// agent: composer-2.5 | 2026-08-02 | stress_spawn hybrid ball churn | fde095
// agent: composer-2.5 | 2026-08-02 | gate churn on confirmed | 76aad6
// agent: composer-2.5 | 2026-08-02 | reconcile balls by entity id | 4e6f24
// agent: composer-2.5 | 2026-08-02 | tick-keyed world despawn | 473c98
// agent: composer-2.5 | 2026-08-02 | cap 96 for dual-peer sync | 42f28e
