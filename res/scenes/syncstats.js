// agent: grok-4.6 | 2026-08-30 | syncstats eight labels | 553a45
function statFunc(kind, mode) {
  function F() {}
  F.prototype.init = function () {};
  F.prototype.start = function () {};
  F.prototype.step = function () {
    var t = kind === "L" ? global.now() : global.server_time();
    global.set_text(this.handle, kind + " " + mode + " " + t.toFixed(1));
  };
  F.prototype.stop = function () {};
  F.prototype.dispose = function () {};
  return F;
}

var Lserver = statFunc("L", "server");
var Lshared = statFunc("L", "shared");
var Lowner = statFunc("L", "owner");
var Llocal = statFunc("L", "local");
var Sserver = statFunc("S", "server");
var Sshared = statFunc("S", "shared");
var Sowner = statFunc("S", "owner");
var Slocal = statFunc("S", "local");

function Scene() {}

Scene.prototype.init = function () {
  global.describe("font", "sans", { src: "fonts/LiberationSans-Regular.ttf", draw: "msdf" });
  global.describe("scene", "view", {
    render: "simple",
    bg: { r: 12, g: 14, b: 22 },
    camera: {
      mode: "fixed",
      position: { x: 0, y: 1.6, z: 5 },
      target: { x: 0, y: 0.6, z: 0 },
      fovy: 45,
    },
  });
  global.describe("label", "l_server_e", { font: "sans", func: Lserver, sync: "server" });
  global.describe("label", "l_shared_e", { font: "sans", func: Lshared, sync: "shared" });
  global.describe("label", "l_owner_e", { font: "sans", func: Lowner, sync: "owner" });
  global.describe("label", "l_local_e", { font: "sans", func: Llocal, sync: "local" });
  global.describe("label", "s_server_e", { font: "sans", func: Sserver, sync: "server" });
  global.describe("label", "s_shared_e", { font: "sans", func: Sshared, sync: "shared" });
  global.describe("label", "s_owner_e", { font: "sans", func: Sowner, sync: "owner" });
  global.describe("label", "s_local_e", { font: "sans", func: Slocal, sync: "local" });
};

Scene.prototype.start = function () {
  var modes = ["server", "shared", "owner", "local"];
  var y0 = 80;
  for (var i = 0; i < 4; i++) {
    var m = modes[i];
    var y = y0 + i * 36;
    this["l_" + m] = global.spawn("l_" + m + "_e", {
      key: "l_" + m,
      space: "screen",
      text: "L " + m + " 0.0",
      size: 22,
      position: { x: 24, y: y, z: 0 },
    });
    this["s_" + m] = global.spawn("s_" + m + "_e", {
      key: "s_" + m,
      space: "screen",
      text: "S " + m + " 0.0",
      size: 22,
      position: { x: 420, y: y, z: 0 },
    });
  }
};

Scene.prototype.step = function (dt) {};

Scene.prototype.stop = function () {
  var modes = ["server", "shared", "owner", "local"];
  var cols = ["l", "s"];
  for (var c = 0; c < cols.length; c++) {
    for (var i = 0; i < modes.length; i++) {
      var k = cols[c] + "_" + modes[i];
      if (this[k]) {
        global.despawn(this[k]);
        this[k] = null;
      }
    }
  }
};

Scene.prototype.dispose = function () {
  var modes = ["server", "shared", "owner", "local"];
  for (var i = 0; i < modes.length; i++) {
    global.dispose("label", "l_" + modes[i] + "_e");
    global.dispose("label", "s_" + modes[i] + "_e");
  }
  global.dispose("font", "sans");
};

global.module(Scene);
// agent: grok-4.6 | 2026-08-30 | syncstats eight labels | 553a45
