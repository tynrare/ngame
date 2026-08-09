// agent: composer-2.5 | 2026-08-09 | JS CLI debug pass tree | c12108
/* Loaded by bus.js via ng_script_load — registers set.debug.render.pass */
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
  _help: "gbuffer debug pass (fullscreen)",
  _values: ["final", "albedo", "normal", "glow", "depth"],
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
// agent: composer-2.5 | 2026-08-09 | JS CLI debug pass tree | c12108
