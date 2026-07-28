// agent: composer-2.5 | 2026-07-28 | sphere explicit describes restored | r1s2p3
function Sphere() {}

Sphere.prototype.init = function () {};
Sphere.prototype.start = function () {};
Sphere.prototype.step = function (dt) {
  this.phase = (this.phase || 0) + dt;
};
Sphere.prototype.stop = function () {};
Sphere.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "sphere_a_m", { width: 1, height: 1, depth: 1, shape: "sphere" });
  global.describe("shader", "sphere_a_s", {
    fragment: "shaders/sphere.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 89, g: 140, b: 255 },
  });
  global.describe("model", "sphere_a_mo", { mesh: "sphere_a_m", shader: "sphere_a_s" });
  global.describe("scene", "view", {
    bg: { r: 12, g: 20, b: 48 },
    camera: {
      mode: "orbit",
      target: { x: 0, y: 0, z: 0 },
      orbit: { radius: 6, speed: 0.6, height: 2 },
      fovy: 45,
    },
  });
  global.describe("entity", "sphere_a_e", {
    model: "sphere_a_mo",
    func: Sphere,
    sync: "server",
  });
};

Scene.prototype.start = function (session) {
  this.sphere = global.spawn("sphere_a_e");
  global.set_position(this.sphere, { x: 0, y: 0, z: 0 });
  global.set_scale(this.sphere, 1);
};

Scene.prototype.step = function (dt) {};

Scene.prototype.stop = function () {
  if (this.sphere) {
    global.despawn(this.sphere);
    this.sphere = null;
  }
};

Scene.prototype.dispose = function () {
  global.dispose("entity", "sphere_a_e");
  global.dispose("model", "sphere_a_mo");
  global.dispose("shader", "sphere_a_s");
  global.dispose("mesh", "sphere_a_m");
};

// agent: composer-2.5 | 2026-07-28 | sphere explicit describes restored | r1s2p3
