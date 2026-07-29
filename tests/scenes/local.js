// agent: composer-2.5 | 2026-07-28 | local sync test fixture | e1d2c3
// agent: composer-2.5 | 2026-07-29 | local spawn transform opts | 2e54c3
function Scene() {}
Scene.prototype.init = function () {
  sceneHelpers.primitive({
    prefix: "local",
    shape: "cube",
    sync: "local",
    tint: [255, 140, 51],
  });
};
Scene.prototype.start = function (session) {
  this.ent = global.spawn("local_e", {
    position: { x: 0, y: 0, z: 0 }
  });
};
Scene.prototype.step = function (dt) {};
Scene.prototype.stop = function () {
  if (this.ent) {
    global.despawn(this.ent);
    this.ent = null;
  }
};
Scene.prototype.dispose = function () {
  sceneHelpers.disposePrimitive("local");
};

// agent: composer-2.5 | 2026-07-28 | local sync test fixture | e1d2c3
// agent: composer-2.5 | 2026-07-29 | local spawn transform opts | 2e54c3
