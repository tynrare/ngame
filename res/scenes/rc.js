// agent: composer-2.5 | 2026-08-09 | rc Phase 0 test scene | 57f1f6
// agent: composer-2.5 | 2026-08-09 | rc scene render rc opt-in | c4d815
// agent: composer-2.5 | 2026-08-09 | rc mouse drag orbit cam | 14f14d
// agent: composer-2.5 | 2026-08-09 | hotter glow props rc | 82c10e
function Prop() {}
Prop.prototype.init = function () {};
Prop.prototype.start = function () {};
Prop.prototype.step = function (dt) {};
Prop.prototype.stop = function () {};
Prop.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  /* Floor */
  global.describe("mesh", "rc_floor_m", { shape: "cube", width: 12, height: 0.2, depth: 12 });
  global.describe("shader", "rc_floor_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 72, g: 78, b: 88 },
    roughness: 0.85,
    metalness: 0.0,
  });
  global.describe("model", "rc_floor_mo", { mesh: "rc_floor_m", shader: "rc_floor_s" });
  global.describe("entity", "rc_floor_e", { model: "rc_floor_mo", func: Prop, sync: "shared" });

  /* Back wall (bounce surface) */
  global.describe("mesh", "rc_wall_m", { shape: "cube", width: 12, height: 4, depth: 0.25 });
  global.describe("shader", "rc_wall_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 180, g: 55, b: 48 },
    roughness: 0.7,
    metalness: 0.0,
  });
  global.describe("model", "rc_wall_mo", { mesh: "rc_wall_m", shader: "rc_wall_s" });
  global.describe("entity", "rc_wall_e", { model: "rc_wall_mo", func: Prop, sync: "shared" });

  /* Side occluder */
  global.describe("mesh", "rc_occ_m", { shape: "cube", width: 0.4, height: 2.4, depth: 3.0 });
  global.describe("shader", "rc_occ_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 48, g: 52, b: 60 },
    roughness: 0.6,
    metalness: 0.05,
  });
  global.describe("model", "rc_occ_mo", { mesh: "rc_occ_m", shader: "rc_occ_s" });
  global.describe("entity", "rc_occ_e", { model: "rc_occ_mo", func: Prop, sync: "shared" });

  /* Matte cube */
  global.describe("mesh", "rc_matte_m", { shape: "cube", width: 1, height: 1, depth: 1 });
  global.describe("shader", "rc_matte_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 90, g: 160, b: 110 },
    roughness: 0.9,
    metalness: 0.0,
  });
  global.describe("model", "rc_matte_mo", { mesh: "rc_matte_m", shader: "rc_matte_s" });
  global.describe("entity", "rc_matte_e", { model: "rc_matte_mo", func: Prop, sync: "shared" });

  /* Metal sphere */
  global.describe("mesh", "rc_metal_m", { shape: "sphere", width: 0.7, height: 0.7, depth: 0.7 });
  global.describe("shader", "rc_metal_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 200, g: 205, b: 220 },
    roughness: 0.18,
    metalness: 0.95,
  });
  global.describe("model", "rc_metal_mo", { mesh: "rc_metal_m", shader: "rc_metal_s" });
  global.describe("entity", "rc_metal_e", { model: "rc_metal_mo", func: Prop, sync: "shared" });

  /* Rough sphere */
  global.describe("mesh", "rc_rough_m", { shape: "sphere", width: 0.55, height: 0.55, depth: 0.55 });
  global.describe("shader", "rc_rough_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 70, g: 110, b: 190 },
    roughness: 0.95,
    metalness: 0.0,
  });
  global.describe("model", "rc_rough_mo", { mesh: "rc_rough_m", shader: "rc_rough_s" });
  global.describe("entity", "rc_rough_e", { model: "rc_rough_mo", func: Prop, sync: "shared" });

  /* Glow props — same shader, high glow uniform */
  global.describe("mesh", "rc_glow_a_m", { shape: "sphere", width: 0.35, height: 0.35, depth: 0.35 });
  global.describe("shader", "rc_glow_a_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 40, g: 40, b: 40 },
    // agent: composer-2.5 | 2026-08-09 | hotter glow props rc | 82c10e
    glow: { r: 255, g: 160, b: 50 },
    roughness: 0.5,
    metalness: 0.0,
  });
  global.describe("model", "rc_glow_a_mo", { mesh: "rc_glow_a_m", shader: "rc_glow_a_s" });
  global.describe("entity", "rc_glow_a_e", { model: "rc_glow_a_mo", func: Prop, sync: "shared" });

  global.describe("mesh", "rc_glow_b_m", { shape: "cube", width: 0.4, height: 0.4, depth: 0.4 });
  global.describe("shader", "rc_glow_b_s", {
    fragment: "shaders/rc.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 30, g: 40, b: 60 },
    // agent: composer-2.5 | 2026-08-09 | hotter glow props rc | 82c10e
    glow: { r: 100, g: 190, b: 255 },
    roughness: 0.4,
    metalness: 0.1,
  });
  global.describe("model", "rc_glow_b_mo", { mesh: "rc_glow_b_m", shader: "rc_glow_b_s" });
  global.describe("entity", "rc_glow_b_e", { model: "rc_glow_b_mo", func: Prop, sync: "shared" });

  // agent: composer-2.5 | 2026-08-09 | rc scene render rc opt-in | c4d815
  global.describe("scene", "view", {
    render: "rc",
    bg: { r: 18, g: 20, b: 28 },
    camera: {
      mode: "fixed",
      position: { x: 5.5, y: 3.2, z: 7.5 },
      target: { x: 0, y: 1.0, z: 0 },
      fovy: 45,
    },
  });
};

Scene.prototype.start = function (session) {
  global.spawn("rc_floor_e", {
    key: "floor",
    position: { x: 0, y: -0.1, z: 0 },
  });
  global.spawn("rc_wall_e", {
    key: "wall",
    position: { x: 0, y: 2.0, z: -5.5 },
  });
  global.spawn("rc_occ_e", {
    key: "occ",
    position: { x: -2.2, y: 1.2, z: -1.5 },
  });
  global.spawn("rc_matte_e", {
    key: "matte",
    position: { x: -0.8, y: 0.75, z: -1.0 },
  });
  global.spawn("rc_metal_e", {
    key: "metal",
    position: { x: 1.2, y: 0.7, z: -0.4 },
  });
  global.spawn("rc_rough_e", {
    key: "rough",
    position: { x: 0.2, y: 0.55, z: 1.2 },
  });
  global.spawn("rc_glow_a_e", {
    key: "glow_a",
    position: { x: -1.6, y: 0.9, z: 0.6 },
  });
  global.spawn("rc_glow_b_e", {
    key: "glow_b",
    position: { x: 2.4, y: 0.5, z: -2.0 },
  });

  // agent: composer-2.5 | 2026-08-09 | rc mouse drag orbit cam | 14f14d
  var cam = global.get_view_camera();
  var tx = cam && cam.target ? cam.target.x : 0;
  var ty = cam && cam.target ? cam.target.y : 1;
  var tz = cam && cam.target ? cam.target.z : 0;
  var px = cam && cam.position ? cam.position.x : 5.5;
  var py = cam && cam.position ? cam.position.y : 3.2;
  var pz = cam && cam.position ? cam.position.z : 7.5;
  var dx = px - tx;
  var dy = py - ty;
  var dz = pz - tz;
  var radius = Math.sqrt(dx * dx + dy * dy + dz * dz);
  if (!(radius > 0.01)) {
    radius = 9.5;
  }
  this._orbit = {
    yaw: Math.atan2(dx, dz),
    pitch: Math.atan2(dy, Math.sqrt(dx * dx + dz * dz)),
    radius: radius,
    target: { x: tx, y: ty, z: tz },
    last: null,
  };
};

/** Apply yaw/pitch/radius to view camera. */
Scene.prototype._orbit_apply = function () {
  var o = this._orbit;
  if (!o) {
    return;
  }
  var cp = Math.cos(o.pitch);
  var sp = Math.sin(o.pitch);
  var sy = Math.sin(o.yaw);
  var cy = Math.cos(o.yaw);
  var r = o.radius;
  var t = o.target;
  global.set_view_camera({
    position: {
      x: t.x + r * cp * sy,
      y: t.y + r * sp,
      z: t.z + r * cp * cy,
    },
    target: t,
  });
};

Scene.prototype.step = function (dt) {
  var o = this._orbit;
  if (!o) {
    return;
  }
  var mouse = global.get_mouse_pos();
  if (!mouse) {
    return;
  }
  if (mouse.left) {
    if (!o.last) {
      o.last = { x: mouse.x, y: mouse.y };
      return;
    }
    var mdx = mouse.x - o.last.x;
    var mdy = mouse.y - o.last.y;
    o.last.x = mouse.x;
    o.last.y = mouse.y;
    o.yaw -= mdx * 0.005;
    o.pitch += mdy * 0.005;
    if (o.pitch > 1.2) {
      o.pitch = 1.2;
    } else if (o.pitch < -0.2) {
      o.pitch = -0.2;
    }
    this._orbit_apply();
  } else {
    o.last = null;
  }
};
Scene.prototype.stop = function () {};
Scene.prototype.dispose = function () {};

global.module(Scene);
// agent: composer-2.5 | 2026-08-09 | rc Phase 0 test scene | 57f1f6
// agent: composer-2.5 | 2026-08-09 | rc scene render rc opt-in | c4d815
// agent: composer-2.5 | 2026-08-09 | rc mouse drag orbit cam | 14f14d
// agent: composer-2.5 | 2026-08-09 | hotter glow props rc | 82c10e
