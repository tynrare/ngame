// agent: composer-2.5 | 2026-07-30 | solar n-body lockstep scene | 2e5f34
// agent: composer-2.5 | 2026-07-30 | solar spheres sync fix | fe8964
// agent: composer-2.5 | 2026-07-30 | solar fixed camera | a37b4e
// Softened Newtonian n-body (Plummer): F = G m_i m_j r / (r^2 + eps^2)^(3/2)
// Circular ICs use the softened central force; COM momentum cancelled; sensor spheres.

function Body() {}
Body.prototype.init = function () {};
Body.prototype.start = function () {};
Body.prototype.step = function (dt) {};
Body.prototype.fixed_step = function (dt) {};
Body.prototype.stop = function () {};
Body.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  // Slow orbits + Plummer softening keep Box3D force integration bound for long runs.
  this.G = 2.0;
  this.eps = 2.0;
  this.eps2 = this.eps * this.eps;

  global.describe("mesh", "sun_m", { width: 2.4, height: 2.4, depth: 2.4, shape: "sphere" });
  global.describe("shader", "sun_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 255, g: 210, b: 64 },
  });
  global.describe("model", "sun_mo", { mesh: "sun_m", shader: "sun_s" });

  global.describe("mesh", "p1_m", { width: 0.7, height: 0.7, depth: 0.7, shape: "sphere" });
  global.describe("shader", "p1_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 180, g: 160, b: 140 },
  });
  global.describe("model", "p1_mo", { mesh: "p1_m", shader: "p1_s" });

  global.describe("mesh", "p2_m", { width: 0.9, height: 0.9, depth: 0.9, shape: "sphere" });
  global.describe("shader", "p2_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 90, g: 150, b: 220 },
  });
  global.describe("model", "p2_mo", { mesh: "p2_m", shader: "p2_s" });

  global.describe("mesh", "p3_m", { width: 0.8, height: 0.8, depth: 0.8, shape: "sphere" });
  global.describe("shader", "p3_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 210, g: 90, b: 70 },
  });
  global.describe("model", "p3_mo", { mesh: "p3_m", shader: "p3_s" });

  global.describe("mesh", "p4_m", { width: 0.6, height: 0.6, depth: 0.6, shape: "sphere" });
  global.describe("shader", "p4_s", {
    fragment: "shaders/flat.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 200, g: 140, b: 90 },
  });
  global.describe("model", "p4_mo", { mesh: "p4_m", shader: "p4_s" });

  // Sphere mass ≈ density * (4/3)π r^3. Sun ≫ planets so orbits stay near-Keplerian.
  // Sun ≈ 2000, planets ≈ 1 / 1.5 / 1.2 / 0.8
  global.describe("shape", "sun_shape", {
    type: "sphere",
    radius: 1.2,
    density: 276.3,
    friction: 0,
    sensor: true,
  });
  global.describe("shape", "p1_shape", {
    type: "sphere",
    radius: 0.35,
    density: 5.57,
    friction: 0,
    sensor: true,
  });
  global.describe("shape", "p2_shape", {
    type: "sphere",
    radius: 0.45,
    density: 3.93,
    friction: 0,
    sensor: true,
  });
  global.describe("shape", "p3_shape", {
    type: "sphere",
    radius: 0.4,
    density: 4.48,
    friction: 0,
    sensor: true,
  });
  global.describe("shape", "p4_shape", {
    type: "sphere",
    radius: 0.3,
    density: 7.07,
    friction: 0,
    sensor: true,
  });

  global.describe("body", "sun_body", { type: "dynamic", shape: "sun_shape" });
  global.describe("body", "p1_body", { type: "dynamic", shape: "p1_shape" });
  global.describe("body", "p2_body", { type: "dynamic", shape: "p2_shape" });
  global.describe("body", "p3_body", { type: "dynamic", shape: "p3_shape" });
  global.describe("body", "p4_body", { type: "dynamic", shape: "p4_shape" });

  // agent: composer-2.5 | 2026-07-30 | solar fixed camera | a37b4e
  global.describe("scene", "view", {
    sim: "lockstep",
    gravity: { x: 0, y: 0, z: 0 },
    bg: { r: 4, g: 6, b: 16 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 22, z: 48 },
      target: { x: 0, y: 0, z: 0 },
      fovy: 45,
    },
  });

  global.describe("entity", "sun_e", {
    model: "sun_mo",
    body: "sun_body",
    func: Body,
    sync: "server",
  });
  global.describe("entity", "p1_e", {
    model: "p1_mo",
    body: "p1_body",
    func: Body,
    sync: "server",
  });
  global.describe("entity", "p2_e", {
    model: "p2_mo",
    body: "p2_body",
    func: Body,
    sync: "server",
  });
  global.describe("entity", "p3_e", {
    model: "p3_mo",
    body: "p3_body",
    func: Body,
    sync: "server",
  });
  global.describe("entity", "p4_e", {
    model: "p4_mo",
    body: "p4_body",
    func: Body,
    sync: "server",
  });
};

Scene.prototype._bodies = function () {
  return [this.sun, this.p1, this.p2, this.p3, this.p4];
};

Scene.prototype._seed_orbits = function () {
  // Softened circular speed about sun:
  //   v = sqrt(G M r^2 / (r^2 + eps^2)^(3/2))
  // Then cancel total linear momentum so COM does not drift.
  var sun = this.sun;
  var Ms = global.get_mass(sun);
  if (!(Ms > 0)) {
    return;
  }
  var planets = [
    { h: this.p1, r: 10.0 },
    { h: this.p2, r: 16.0 },
    { h: this.p3, r: 24.0 },
    { h: this.p4, r: 34.0 },
  ];
  var px = 0.0;
  var py = 0.0;
  var pz = 0.0;
  var i;
  var eps2 = this.eps2;
  for (i = 0; i < planets.length; i++) {
    var p = planets[i];
    var m = global.get_mass(p.h);
    var r2 = p.r * p.r;
    var soft = r2 + eps2;
    // 0.98× softened circular speed → slight bound bias against integrator energy gain.
    var v = 0.98 * Math.sqrt((this.G * Ms * r2) / (soft * Math.sqrt(soft)));
    global.set_linear_velocity(p.h, { x: 0, y: 0, z: v });
    px += 0.0;
    py += 0.0;
    pz += m * v;
  }
  global.set_linear_velocity(sun, { x: -px / Ms, y: -py / Ms, z: -pz / Ms });
  this.seeded = true;
};

Scene.prototype.start = function (session) {
  this.seeded = false;
  // Stable alphabetical keys for identical create order across peers.
  this.p1 = global.spawn("p1_e", {
    key: "p1",
    position: { x: 10, y: 0, z: 0 },
    scale: 1,
  });
  this.p2 = global.spawn("p2_e", {
    key: "p2",
    position: { x: 16, y: 0, z: 0 },
    scale: 1,
  });
  this.p3 = global.spawn("p3_e", {
    key: "p3",
    position: { x: 24, y: 0, z: 0 },
    scale: 1,
  });
  this.p4 = global.spawn("p4_e", {
    key: "p4",
    position: { x: 34, y: 0, z: 0 },
    scale: 1,
  });
  this.sun = global.spawn("sun_e", {
    key: "sun",
    position: { x: 0, y: 0, z: 0 },
    scale: 1,
  });
  // Late-join: bodies deferred until LOCK_PHYS import — never invent ICs here.
  // Fresh start: seed once when bodies exist on the phys owner.
  var joining = session && (session.syncing || (session.snap_tick && session.snap_tick > 0));
  if (joining) {
    this.seeded = true;
  } else if (global.get_mass(this.sun) > 0) {
    this._seed_orbits();
  }
};

Scene.prototype.step = function (dt) {};

Scene.prototype.fixed_step = function (dt) {
  // agent: composer-2.5 | 2026-07-30 | solar spheres sync fix | fe8964
  // Phys owner has bodies (server slot). View under lockstep has mass=0 — no-op.
  var bodies = this._bodies();
  if (!bodies[0]) {
    return;
  }
  var n = bodies.length;
  var pos = [];
  var mass = [];
  var i;
  var j;
  for (i = 0; i < n; i++) {
    var h = bodies[i];
    mass[i] = global.get_mass(h);
    pos[i] = global.get_position(h);
    if (!(mass[i] > 0)) {
      return;
    }
  }
  // Full mutual n-body with Plummer softening (all pairs). Forces via apply_force.
  var G = this.G;
  var eps2 = this.eps2;
  var acc = [];
  for (i = 0; i < n; i++) {
    acc[i] = { x: 0, y: 0, z: 0 };
  }
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      var dx = pos[j].x - pos[i].x;
      var dy = pos[j].y - pos[i].y;
      var dz = pos[j].z - pos[i].z;
      var soft = dx * dx + dy * dy + dz * dz + eps2;
      var inv = 1.0 / Math.sqrt(soft);
      var inv3 = inv * inv * inv;
      var s = G * inv3;
      var fx = s * dx;
      var fy = s * dy;
      var fz = s * dz;
      // a_i += G m_j r_hat / soft^(3/2); F_i = m_i a_i
      acc[i].x += fx * mass[j];
      acc[i].y += fy * mass[j];
      acc[i].z += fz * mass[j];
      acc[j].x -= fx * mass[i];
      acc[j].y -= fy * mass[i];
      acc[j].z -= fz * mass[i];
    }
  }
  for (i = 0; i < n; i++) {
    global.apply_force(bodies[i], {
      x: acc[i].x * mass[i],
      y: acc[i].y * mass[i],
      z: acc[i].z * mass[i],
    });
  }
};

Scene.prototype.stop = function () {
  var bodies = this._bodies();
  var i;
  for (i = 0; i < bodies.length; i++) {
    if (bodies[i]) {
      global.despawn(bodies[i]);
    }
  }
  this.sun = null;
  this.p1 = null;
  this.p2 = null;
  this.p3 = null;
  this.p4 = null;
  this.seeded = false;
};

Scene.prototype.dispose = function () {
  global.dispose("entity", "sun_e");
  global.dispose("entity", "p1_e");
  global.dispose("entity", "p2_e");
  global.dispose("entity", "p3_e");
  global.dispose("entity", "p4_e");
  global.dispose("body", "sun_body");
  global.dispose("body", "p1_body");
  global.dispose("body", "p2_body");
  global.dispose("body", "p3_body");
  global.dispose("body", "p4_body");
  global.dispose("shape", "sun_shape");
  global.dispose("shape", "p1_shape");
  global.dispose("shape", "p2_shape");
  global.dispose("shape", "p3_shape");
  global.dispose("shape", "p4_shape");
  global.dispose("model", "sun_mo");
  global.dispose("model", "p1_mo");
  global.dispose("model", "p2_mo");
  global.dispose("model", "p3_mo");
  global.dispose("model", "p4_mo");
  global.dispose("shader", "sun_s");
  global.dispose("shader", "p1_s");
  global.dispose("shader", "p2_s");
  global.dispose("shader", "p3_s");
  global.dispose("shader", "p4_s");
  global.dispose("mesh", "sun_m");
  global.dispose("mesh", "p1_m");
  global.dispose("mesh", "p2_m");
  global.dispose("mesh", "p3_m");
  global.dispose("mesh", "p4_m");
};
// agent: composer-2.5 | 2026-07-30 | solar n-body lockstep scene | 2e5f34
// agent: composer-2.5 | 2026-07-30 | solar spheres sync fix | fe8964
// agent: composer-2.5 | 2026-07-30 | solar fixed camera | a37b4e
