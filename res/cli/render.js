// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | 278105
// agent: composer-2.5 | 2026-08-09 | pass under debug.render | 1bf08c
// agent: composer-2.5 | 2026-08-09 | CLI quality GPU cascade cost | cb3917
/* Loaded by bus.js — registers set.render.* / set.render.rc.* */
if (typeof ng_cli === "undefined") {
  throw new Error("cli/render.js requires bus.js ng_cli");
}

if (!ng_cli.set.render) {
  ng_cli.set.render = { _help: "render settings (scale, rc)" };
}

ng_cli.set.render.scale = {
  _help: "internal render resolution scale (0.25..2, default 1)",
  _get: function () {
    return typeof ng_render_get === "function" ? ng_render_get("render.scale") : null;
  },
  _set: function (v) {
    if (typeof ng_render_set !== "function") {
      ng_bus_reply("render n/a");
      return;
    }
    if (!ng_render_set("render.scale", v)) {
      ng_bus_reply("set failed: render.scale=" + v);
      return;
    }
    ng_bus_reply("render.scale=" + v);
  },
};

if (!ng_cli.set.render.rc) {
  ng_cli.set.render.rc = { _help: "radiance cascades knobs" };
}

ng_cli.set.render.rc.quality = {
  _help: "GPU RC cost (0≈direct..4 denser cascades/dirs); same pipeline",
  _values: ["0", "1", "2", "3", "4"],
  _get: function () {
    return typeof ng_render_get === "function" ? ng_render_get("render.rc.quality") : null;
  },
  _set: function (v) {
    if (typeof ng_render_set !== "function") {
      ng_bus_reply("render n/a");
      return;
    }
    if (!ng_render_set("render.rc.quality", v)) {
      ng_bus_reply("set failed: render.rc.quality=" + v);
      return;
    }
    ng_bus_reply("render.rc.quality=" + v);
  },
};

ng_cli.set.render.rc.gi_strength = {
  _help: "RC GI multiply (0..8)",
  _get: function () {
    return typeof ng_render_get === "function" ? ng_render_get("render.rc.gi_strength") : null;
  },
  _set: function (v) {
    if (typeof ng_render_set !== "function") {
      ng_bus_reply("render n/a");
      return;
    }
    if (!ng_render_set("render.rc.gi_strength", v)) {
      ng_bus_reply("set failed: render.rc.gi_strength=" + v);
      return;
    }
    ng_bus_reply("render.rc.gi_strength=" + v);
  },
};
// agent: composer-2.5 | 2026-08-09 | CLI set render.rc tree | 278105
// agent: composer-2.5 | 2026-08-09 | pass under debug.render | 1bf08c
// agent: composer-2.5 | 2026-08-09 | CLI quality GPU cascade cost | cb3917
