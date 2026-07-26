(function (global) {
  var state = { entity_id: 0, rot_y: 0, is_controller: false };

  global.scene_on_session = function (entity_id, is_controller) {
    state.entity_id = entity_id | 0;
    state.is_controller = !!is_controller;
    if (!state.is_controller) {
      return;
    }
    state.rot_y = 0;
  };

  global.scene_tick = function (dt, buttons, yaw_delta) {
    if (!state.is_controller) {
      return;
    }
    if (buttons & 1) {
      state.rot_y -= 1.5 * dt;
    }
    if (buttons & 2) {
      state.rot_y += 1.5 * dt;
    }
    state.rot_y += yaw_delta;
  };

  global.scene_flush = function () {
    if (!state.is_controller || !state.entity_id) {
      return null;
    }
    return { entity_id: state.entity_id, rot_y: state.rot_y };
  };

  global.scene_apply_remote = function (entity_id, rot_y) {
    if ((entity_id | 0) === state.entity_id) {
      state.rot_y = rot_y;
    }
  };

  global.scene_get_rot_y = function () {
    return state.rot_y;
  };
})(this);
