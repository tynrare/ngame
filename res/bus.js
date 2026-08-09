// agent: composer-2.5 | 2026-08-09 | ng_cli commands with _run | dd6c51
var ng_cli = {
  _help: "console commands — try ? <cmd>",

  scene: {
    _help: "scene <id> — load a registered scene",
    _run: function (args) {
      if (args.length < 1) {
        ng_bus_reply("usage: scene <id>");
        return;
      }
      if (typeof ng_bus_cmd === "function") {
        ng_bus_cmd("net", "scene " + args[0]);
      } else {
        ng_bus_send("sim", "scene", args[0]);
      }
    },
  },

  status: {
    _help: "status — gateway / view / render snapshot",
    _run: function () {
      if (typeof ng_cli_status === "function") {
        ng_bus_reply(ng_cli_status());
      } else {
        ng_bus_reply("status n/a");
      }
    },
  },

  mcp: {
    _help: "mcp — alias for status",
    _run: function (args) {
      ng_cli.status._run(args);
    },
  },

  set: {
    _help: "set <path> <value> — change runtime settings",
  },
};

if (typeof ng_script_load === "function") {
  ng_script_load("cli/runtime.js");
  ng_script_load("cli/debug.js");
}
// agent: composer-2.5 | 2026-08-09 | ng_cli commands with _run | dd6c51
