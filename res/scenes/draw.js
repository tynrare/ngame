// agent: gpt-6-astra | 2026-09-05 | demonstrate shared physics and user drawing | 23e93c
function Scene() {}

/** Describe reusable physics geometry and view scopes. @return {void} */
Scene.prototype.init = function () {
  global.describe("shape", "draw_box", { type: "box", hx: 0.5, hy: 0.5, hz: 0.5 });
  global.describe("shape", "draw_ball", { type: "sphere", radius: 0.5 });
  global.describe("font", "draw_sans", { src: "fonts/LiberationSans-Regular.ttf" });
  global.describe("scope", "world", {
    render: "simple", bg: { r: 12, g: 20, b: 32 },
    camera: { mode: "fixed", position: { x: 0, y: 2, z: 7 },
      target: { x: 0, y: 0.3, z: 0 }, fovy: 45 }
  });
  global.describe("scope", "screen", { camera: { mode: "ortho" } });
  global.describe("scene", "view", { scopes: ["world", "screen"] });
  global.describe("body", "draw_static", { type: "static", shape: "draw_box" });
  global.describe("entity", "draw_body", { body: "draw_static", sync: "shared" });
};

/** Spawn a body whose collider supplies its visual geometry. @param {Object} session session data. @return {void} */
Scene.prototype.start = function (session) {
  this.body = global.spawn("draw_body", { key: "body", position: { x: 0, y: 0, z: 0 } });
};

/** Submit transient shapes and labels once per rendered frame. @return {void} */
Scene.prototype.draw = function () {
  var angle = global.now();
  global.draw_shape("draw_box", { position: { x: -1.6 }, rotation: { y: angle },
    tint: { r: 90, g: 170, b: 255 } });
  global.draw_shape("draw_ball", { position: { x: 1.6 },
    tint: { r: 255, g: 175, b: 90 } });
  global.draw_label("draw_sans", "draw_shape + draw_label", {
    scope: "world", position: { x: -1.9, y: 1.4 }, size: 0.3
  });
  global.draw_label("draw_sans", "One frame queue: C / JS / physics", {
    scope: "screen", position: { x: 24, y: 92 }, size: 24
  });
  global.draw_label("draw_sans", "Blue: immediate box   White: physics body   Orange: immediate sphere", {
    scope: "screen", position: { x: 24, y: 124 }, size: 16,
    tint: { r: 170, g: 195, b: 220 }
  });
};

/** @return {void} */
Scene.prototype.stop = function () { global.despawn(this.body); };
global.module(Scene);
// agent: gpt-6-astra | 2026-09-05 | demonstrate shared physics and user drawing | 23e93c
