// agent: composer-2.5 | 2026-08-09 | pass under debug.render | d0d7e2
// agent: composer-2.5 | 2026-08-10 | cli debug probes grid atlas | bc502d
// agent: composer-2.5 | 2026-08-11 | CLI culling debug pass | 7e1962
// agent: composer-2.5 | 2026-08-12 | cli probes-lod debug pass | a668a1
/* Loaded by bus.js — registers set.debug.render.pass */
if (typeof ng_cli === "undefined") {
  throw new Error("cli/debug.js requires bus.js ng_cli");
}

if (!ng_cli.set.debug) {
  ng_cli.set.debug = { _help: "debug visualization" };
}
if (!ng_cli.set.debug.render) {
  ng_cli.set.debug.render = { _help: "render debug views" };
}

ng_cli.set.debug.render.pass = {
  _help: "fullscreen gbuffer / RC debug pass",
  _values: [
    "final",
    "albedo",
    "normal",
    "glow",
    "depth",
    "irradiance",
    "uvw",
    "probes",
    "probes-lod",
    "grid",
    "atlas",
    "culling",
  ],
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
// agent: composer-2.5 | 2026-08-09 | pass under debug.render | d0d7e2
// agent: composer-2.5 | 2026-08-10 | cli debug probes grid atlas | bc502d
// agent: composer-2.5 | 2026-08-11 | CLI culling debug pass | 7e1962
// agent: composer-2.5 | 2026-08-12 | cli probes-lod debug pass | a668a1
