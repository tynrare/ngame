// agent: composer-2.5 | 2026-08-09 | setting leaves use _get | 55030b
/* Loaded by bus.js — registers set.debug.render.* */
if (typeof ng_cli === "undefined") {
  throw new Error("cli/debug.js requires bus.js ng_cli");
}

if (!ng_cli.set.debug) {
  ng_cli.set.debug = { _help: "debug visualization" };
}
if (!ng_cli.set.debug.render) {
  ng_cli.set.debug.render = { _help: "render debug" };
}

ng_cli.set.debug.render.pass = {
  _help: "gbuffer / RC debug pass (fullscreen)",
  _values: ["final", "albedo", "normal", "glow", "depth", "irradiance"],
  _get: function () {
    return typeof ng_render_get === "function" ? ng_render_get("debug.render.pass") : null;
  },
  _set: function (v) {
    if (typeof ng_render_set !== "function") {
      ng_bus_reply("render n/a");
      return;
    }
    if (!ng_render_set("debug.render.pass", v)) {
      ng_bus_reply("set failed: debug.render.pass=" + v);
      return;
    }
    ng_bus_reply("debug.render.pass=" + v);
  },
};

ng_cli.set.debug.render.rc_quality = {
  _help: "0/1 forward only; 2 screen-space RC",
  _values: ["0", "1", "2"],
  _get: function () {
    return typeof ng_render_get === "function" ? ng_render_get("debug.render.rc_quality") : null;
  },
  _set: function (v) {
    if (typeof ng_render_set !== "function") {
      ng_bus_reply("render n/a");
      return;
    }
    if (!ng_render_set("debug.render.rc_quality", v)) {
      ng_bus_reply("set failed: debug.render.rc_quality=" + v);
      return;
    }
    ng_bus_reply("debug.render.rc_quality=" + v);
  },
};
// agent: composer-2.5 | 2026-08-09 | setting leaves use _get | 55030b
