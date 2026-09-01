// Purpose: Hybrid first-person dungeon cell; pawns per peer; Y-billboard props.
// Scope in: boot register, hybrid lockstep, albedo models. Scope out: C albedo load, net.
// Related: docs/crawler.md, res/boot.js, res/shaders/tex.fs
// Gateway role: pattern | Scope id: crawler | Flow id: crawler-view + crawler-pawn
//
// crawler-view flow (Scene.step, local only):
// 1) mouse delta → yaw/pitch; set_local_analog(yaw) for lockstep sample
// 2) set_view_camera at local pawn (seat fallback); hide local mesh
// 3) Y-billboard remotes + props + crate card
//
// crawler-pawn flow (hybrid predict + confirmed):
// 1) lockstep_peers() spawn/despawn p{id} (late join / disconnect)
// 2) get_peer_input WASD rotated by get_peer_analog (forward = look yaw)
// 3) set_linear_velocity XZ (keep Y); view Hermite on remotes
//
// agent: grok-4.6 | 2026-08-31 | crawler analog roster billboard | 012689
// agent: grok-4.6 | 2026-08-31 | pawn key plus walk velocity | 02d4f6
// agent: grok-4.6 | 2026-08-31 | crawler camera seat fallback | 7e459e
// agent: grok-4.6 | 2026-08-31 | invert crawler strafe ad | f91375
// agent: grok-4.6 | 2026-08-31 | guard crawler camera roster | c4cd8d
var g_crawler = null;
var g_spawn_key = "";
var EYE = 0.65;
var WALK = 4.5;
var SEATS = [
  { x: -2.5, z: -2.5 },
  { x: 2.5, z: -2.5 },
  { x: -2.5, z: 2.5 },
  { x: 2.5, z: 2.5 },
];

/** Mesh + albedo model; one shared tex shader (graph desc cap 32). @returns {void} */
function texModel(id, mesh, w, h, d, albedo) {
  global.describe("mesh", id + "_m", { shape: mesh, width: w, height: h, depth: d });
  global.describe("model", id + "_mo", {
    mesh: id + "_m",
    shader: "cr_tex_s",
    albedo: albedo,
  });
}

function Prop() {}
Prop.prototype.init = function () {};
Prop.prototype.start = function () {};
Prop.prototype.step = function (dt) {};
Prop.prototype.fixed_step = function (dt) {};
Prop.prototype.stop = function () {};
Prop.prototype.dispose = function () {};

function Pawn() {}
Pawn.prototype.init = function () {};
/** Bind this pawn to spawn key p{id}. @returns {void} */
Pawn.prototype.start = function () {
  this.peer = 0;
  var k = this.key || g_spawn_key || "";
  if (k.charAt(0) === "p") {
    this.peer = parseInt(k.slice(1), 10) | 0;
  }
};
Pawn.prototype.step = function (dt) {};
/** Camera-relative WASD force for this pawn's peer. @param {number} dt @returns {void} */
Pawn.prototype.fixed_step = function (dt) {
  var peer = this.peer | 0;
  if (!peer) {
    return;
  }
  var mx = 0;
  var mz = 0;
  // agent: grok-4.6 | 2026-08-31 | invert crawler strafe ad | f91375
  if (global.get_peer_input(global.KEY_W, peer)) {
    mz += 1;
  }
  if (global.get_peer_input(global.KEY_S, peer)) {
    mz -= 1;
  }
  if (global.get_peer_input(global.KEY_A, peer)) {
    mx += 1;
  }
  if (global.get_peer_input(global.KEY_D, peer)) {
    mx -= 1;
  }
  if (mx === 0 && mz === 0) {
    return;
  }
  var yaw = global.get_peer_analog(peer);
  var sy = Math.sin(yaw);
  var cy = Math.cos(yaw);
  var fx = mx * cy + mz * sy;
  var fz = -mx * sy + mz * cy;
  var v = global.get_linear_velocity(this.handle);
  var vy = v && v.y !== undefined ? v.y : 0;
  global.set_linear_velocity(this.handle, { x: fx * WALK, y: vy, z: fz * WALK });
};
Pawn.prototype.stop = function () {};
Pawn.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  /* Visual dim = phys full / 1.5 (GenMeshCube width*1.5). */
  global.describe("shader", "cr_tex_s", {
    fragment: "shaders/tex.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 255, g: 255, b: 255 },
  });
  texModel("cr_floor", "cube", 8, 0.133, 8, "textures/crawler/floor.png");
  texModel("cr_wns", "cube", 8.267, 2, 0.267, "textures/crawler/wall.png");
  texModel("cr_wew", "cube", 0.267, 2, 8.267, "textures/crawler/wall.png");
  texModel("cr_crate", "cube", 0.53, 1.07, 0.04, "textures/crawler/crate.png");
  texModel("cr_prop", "cube", 0.53, 1.07, 0.04, "textures/crawler/prop.png");
  texModel("cr_pawn", "cube", 0.53, 1.07, 0.04, "textures/crawler/player.png");

  global.describe("shape", "cr_floor_sh", {
    type: "box",
    hx: 6,
    hy: 0.1,
    hz: 6,
    density: 0,
    friction: 0.8,
  });
  global.describe("shape", "cr_wns_sh", {
    type: "box",
    hx: 6.2,
    hy: 1.5,
    hz: 0.2,
    density: 0,
    friction: 0.6,
  });
  global.describe("shape", "cr_wew_sh", {
    type: "box",
    hx: 0.2,
    hy: 1.5,
    hz: 6.2,
    density: 0,
    friction: 0.6,
  });
  global.describe("shape", "cr_crate_sh", {
    type: "box",
    hx: 0.4,
    hy: 0.4,
    hz: 0.4,
    density: 0,
    friction: 0.5,
  });
  global.describe("shape", "cr_pawn_sh", {
    type: "box",
    hx: 0.3,
    hy: 0.8,
    hz: 0.3,
    density: 4,
    friction: 0.4,
  });
  global.describe("body", "cr_floor_b", { type: "static", shape: "cr_floor_sh" });
  global.describe("body", "cr_wns_b", { type: "static", shape: "cr_wns_sh" });
  global.describe("body", "cr_wew_b", { type: "static", shape: "cr_wew_sh" });
  global.describe("body", "cr_crate_b", { type: "static", shape: "cr_crate_sh" });
  global.describe("body", "cr_pawn_b", { type: "dynamic", shape: "cr_pawn_sh", lock_rot: true });

  global.describe("entity", "cr_floor_e", {
    model: "cr_floor_mo",
    body: "cr_floor_b",
    func: Prop,
    sync: "server",
  });
  global.describe("entity", "cr_wns_e", {
    model: "cr_wns_mo",
    body: "cr_wns_b",
    func: Prop,
    sync: "server",
  });
  global.describe("entity", "cr_wew_e", {
    model: "cr_wew_mo",
    body: "cr_wew_b",
    func: Prop,
    sync: "server",
  });
  global.describe("entity", "cr_crate_e", {
    model: "cr_crate_mo",
    body: "cr_crate_b",
    func: Prop,
    sync: "server",
  });
  global.describe("entity", "cr_prop_e", {
    model: "cr_prop_mo",
    func: Prop,
    sync: "shared",
  });
  global.describe("entity", "cr_pawn_e", {
    model: "cr_pawn_mo",
    body: "cr_pawn_b",
    func: Pawn,
    sync: "server",
  });

  global.describe("scene", "view", {
    sim: "hybrid",
    gravity: { x: 0, y: -10, z: 0 },
    bg: { r: 18, g: 14, b: 12 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 1.4, z: 4 },
      target: { x: 0, y: 1, z: 0 },
      fovy: 70,
    },
  });
};

Scene.prototype.start = function (session) {
  g_crawler = this;
  this.me = session && session.your_id ? session.your_id | 0 : 1;
  this.yaw = 0;
  this.pitch = 0;
  this.last_mouse = null;
  this.pawns = {};
  this.props = [];
  this.cell = [];
  this.crate = global.spawn("cr_crate_e", { key: "crate", position: { x: 2.2, y: 0.4, z: -1.5 } });
  this.cell.push(this.crate);
  this.cell.push(global.spawn("cr_floor_e", { key: "floor", position: { x: 0, y: -0.1, z: 0 } }));
  this.cell.push(global.spawn("cr_floor_e", { key: "ceil", position: { x: 0, y: 3.0, z: 0 } }));
  this.props.push(
    global.spawn("cr_prop_e", { key: "torch_a", position: { x: -3.5, y: 1.1, z: -3.5 } })
  );
  this.props.push(
    global.spawn("cr_prop_e", { key: "torch_b", position: { x: 3.5, y: 1.1, z: 3.5 } })
  );
  this.cell.push(global.spawn("cr_wew_e", { key: "wall_e", position: { x: 6.2, y: 1.5, z: 0 } }));
  this.cell.push(global.spawn("cr_wns_e", { key: "wall_n", position: { x: 0, y: 1.5, z: -6.2 } }));
  this.cell.push(global.spawn("cr_wns_e", { key: "wall_s", position: { x: 0, y: 1.5, z: 6.2 } }));
  this.cell.push(global.spawn("cr_wew_e", { key: "wall_w", position: { x: -6.2, y: 1.5, z: 0 } }));
};

/** Spawn/despawn pawns to match lockstep roster. @returns {void} */
Scene.prototype._sync_roster = function () {
  var ids = global.lockstep_peers();
  if (!ids || ids.length === 0) {
    ids = this.me ? [this.me] : [];
  }
  var want = {};
  var i;
  for (i = 0; i < ids.length; i++) {
    want[ids[i]] = true;
  }
  var k;
  for (k in this.pawns) {
    if (!this.pawns.hasOwnProperty(k)) {
      continue;
    }
    if (!want[k] && !want[k | 0]) {
      global.despawn(this.pawns[k]);
      delete this.pawns[k];
    }
  }
  for (i = 0; i < ids.length; i++) {
    var id = ids[i] | 0;
    if (this.pawns[id]) {
      // agent: grok-4.6 | 2026-08-31 | guard crawler camera roster | c4cd8d
      var gp = global.get_position(this.pawns[id]);
      if (gp && typeof gp.x === "number") {
        continue;
      }
      delete this.pawns[id];
    }
    var seat = SEATS[(id - 1) % SEATS.length];
    g_spawn_key = "p" + id;
    this.pawns[id] = global.spawn("cr_pawn_e", {
      key: g_spawn_key,
      position: { x: seat.x, y: 0.85, z: seat.z },
    });
  }
};

Scene.prototype._billboard = function (handle, cam) {
  if (!handle || !cam || !cam.position) {
    return;
  }
  var p = global.get_position(handle);
  if (!p) {
    return;
  }
  var dx = cam.position.x - p.x;
  var dz = cam.position.z - p.z;
  global.set_rotation_y(handle, Math.atan2(dx, dz));
};

// agent: grok-4.6 | 2026-08-31 | crawler camera seat fallback | 7e459e
Scene.prototype.step = function (dt) {
  var mouse = global.get_mouse_pos();
  if (mouse) {
    if (this.last_mouse) {
      this.yaw -= (mouse.x - this.last_mouse.x) * 0.006;
      this.pitch -= (mouse.y - this.last_mouse.y) * 0.006;
      if (this.pitch > 1.2) {
        this.pitch = 1.2;
      } else if (this.pitch < -1.2) {
        this.pitch = -1.2;
      }
    }
    this.last_mouse = { x: mouse.x, y: mouse.y };
  }
  global.set_local_analog(this.yaw);

  // agent: grok-4.6 | 2026-08-31 | guard crawler camera roster | c4cd8d
  var h = this.pawns[this.me];
  var pos = h ? global.get_position(h) : null;
  if (!pos || typeof pos.x !== "number") {
    pos = null;
  }
  var seat = SEATS[(this.me - 1 + SEATS.length) % SEATS.length];
  var ex = pos ? pos.x : seat.x;
  var ey = (pos ? pos.y : 0.85) + EYE;
  var ez = pos ? pos.z : seat.z;
  var cp = Math.cos(this.pitch);
  var sp = Math.sin(this.pitch);
  var sy = Math.sin(this.yaw);
  var cy = Math.cos(this.yaw);
  global.set_view_camera({
    position: { x: ex, y: ey, z: ez },
    target: { x: ex + cp * sy, y: ey + sp, z: ez + cp * cy },
  });

  var cam = global.get_view_camera();
  var k;
  for (k in this.pawns) {
    if (!this.pawns.hasOwnProperty(k)) {
      continue;
    }
    var ph = this.pawns[k];
    if (!ph) {
      continue;
    }
    if ((k | 0) === this.me) {
      global.set_scale(ph, 0.001);
    } else {
      global.set_scale(ph, 1);
      this._billboard(ph, cam);
    }
  }
  var i;
  for (i = 0; i < this.props.length; i++) {
    this._billboard(this.props[i], cam);
  }
  this._billboard(this.crate, cam);
};

/** Confirmed roster sync. @param {number} dt @returns {void} */
Scene.prototype.fixed_step = function (dt) {
  this._sync_roster();
};

Scene.prototype.stop = function () {
  var k;
  for (k in this.pawns) {
    if (this.pawns.hasOwnProperty(k) && this.pawns[k]) {
      global.despawn(this.pawns[k]);
    }
  }
  var i;
  for (i = 0; i < this.props.length; i++) {
    global.despawn(this.props[i]);
  }
  for (i = 0; i < this.cell.length; i++) {
    global.despawn(this.cell[i]);
  }
  this.pawns = {};
  this.props = [];
  this.cell = [];
  this.crate = 0;
  g_crawler = null;
};

Scene.prototype.dispose = function () {
  var ids = ["cr_pawn", "cr_prop", "cr_crate", "cr_floor", "cr_wns", "cr_wew"];
  var i;
  for (i = 0; i < ids.length; i++) {
    global.dispose("entity", ids[i] + "_e");
    global.dispose("body", ids[i] + "_b");
    global.dispose("shape", ids[i] + "_sh");
    global.dispose("model", ids[i] + "_mo");
    global.dispose("mesh", ids[i] + "_m");
  }
  global.dispose("shader", "cr_tex_s");
};

global.module(Scene);
// agent: grok-4.6 | 2026-08-31 | crawler analog roster billboard | 012689
// agent: grok-4.6 | 2026-08-31 | pawn key plus walk velocity | 02d4f6
// agent: grok-4.6 | 2026-08-31 | crawler camera seat fallback | 7e459e
// agent: grok-4.6 | 2026-08-31 | invert crawler strafe ad | f91375
// agent: grok-4.6 | 2026-08-31 | guard crawler camera roster | c4cd8d

