// agent: composer-2.5 | 2026-07-28 | owner sync test fixture | f0e1d2
// agent: composer-2.5 | 2026-07-29 | owner spawn transform opts | 3c46d4
function Scene() {}
Scene.prototype.init = function () {
  sceneHelpers.primitive({
    prefix: "owner",
    shape: "cube",
    sync: "owner",
    tint: [255, 140, 51],
  });
};
Scene.prototype.start = function (session) {
  this.ent = global.spawn("owner_e", {
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
  sceneHelpers.disposePrimitive("owner");
};

// agent: composer-2.5 | 2026-07-28 | owner sync test fixture | f0e1d2
// agent: composer-2.5 | 2026-07-29 | owner spawn transform opts | 3c46d4
