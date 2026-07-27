// agent: composer-2.5 | 2026-07-27 | sphere js ES5 scene | c0d1e2
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
  global.describe("entity", "sphere_a_e", {
    model: "sphere",
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
};
