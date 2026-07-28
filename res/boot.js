// agent: composer-2.5 | 2026-07-28 | server boot script not a scene | b1o2o3
function BootSphere() {}

BootSphere.prototype.init = function () {};
BootSphere.prototype.start = function () {};
BootSphere.prototype.step = function (dt) {
  this.phase = (this.phase || 0) + dt;
};
BootSphere.prototype.stop = function () {};
BootSphere.prototype.dispose = function () {};

function Scene() {}

Scene.prototype.init = function () {
  global.describe("mesh", "boot_a_m", { width: 1, height: 1, depth: 1, shape: "sphere" });
  global.describe("shader", "boot_a_s", {
    fragment: "shaders/sphere.fs",
    vertex: "shaders/mesh.vs",
    tint: { r: 89, g: 140, b: 255 },
  });
  global.describe("model", "boot_a_mo", { mesh: "boot_a_m", shader: "boot_a_s" });
  global.describe("scene", "view", {
    bg: { r: 12, g: 20, b: 48 },
    camera: {
      mode: "orbit",
      target: { x: 0, y: 0, z: 0 },
      orbit: { radius: 6, speed: 0.6, height: 2 },
      fovy: 45,
    },
  });
  global.describe("entity", "boot_a_e", {
    model: "boot_a_mo",
    func: BootSphere,
    sync: "server",
  });
};

Scene.prototype.start = function (session) {
  this.sphere = global.spawn("boot_a_e");
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
  global.dispose("entity", "boot_a_e");
  global.dispose("model", "boot_a_mo");
  global.dispose("shader", "boot_a_s");
  global.dispose("mesh", "boot_a_m");
};

// agent: composer-2.5 | 2026-07-28 | server boot script not a scene | b1o2o3
