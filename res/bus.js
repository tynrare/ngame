// agent: composer-2.5 | 2026-08-09 | JS CLI tree and help walk | 18f0e8
function ng_bus_parse(line) {
  return line.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
}

/** Expand dotted tokens so `set debug.render.pass` → set,debug,render,pass. */
function ng_cli_expand(args) {
  var out = [];
  for (var i = 0; i < args.length; i++) {
    var parts = String(args[i]).split(".");
    for (var j = 0; j < parts.length; j++) {
      if (parts[j].length > 0) {
        out.push(parts[j]);
      }
    }
  }
  return out;
}

function ng_cli_child_keys(node) {
  var keys = [];
  for (var k in node) {
    if (!Object.prototype.hasOwnProperty.call(node, k)) {
      continue;
    }
    if (k.charAt(0) === "_") {
      continue;
    }
    keys.push(k);
  }
  keys.sort();
  return keys;
}

function ng_cli_walk(parts) {
  var node = ng_cli;
  for (var i = 0; i < parts.length; i++) {
    if (!node || typeof node !== "object" || !node[parts[i]]) {
      return null;
    }
    node = node[parts[i]];
  }
  return node;
}

function ng_cli_help_text(parts) {
  var node = parts.length === 0 ? ng_cli : ng_cli_walk(parts);
  if (!node) {
    return "unknown: ? " + parts.join(" ");
  }
  var lines = [];
  if (node._help) {
    lines.push(node._help);
  }
  var kids = ng_cli_child_keys(node);
  if (kids.length > 0) {
    lines.push("options: " + kids.join(" | "));
  }
  if (node._values && node._values.length > 0) {
    lines.push("values: " + node._values.join(" | "));
    if (typeof ng_render_get === "function") {
      var cur = ng_render_get("debug.render.pass");
      if (cur) {
        lines.push("current: " + cur);
      }
    }
  }
  if (lines.length === 0) {
    lines.push("(no help)");
  }
  return lines.join("\n");
}

function ng_cli_do_set(parts) {
  if (parts.length < 1) {
    ng_bus_reply(ng_cli_help_text(["set"]));
    return true;
  }
  var value = null;
  var path = parts.slice();
  var leaf = ng_cli_walk(["set"].concat(path));
  if (!leaf || typeof leaf._set !== "function") {
    if (path.length >= 1) {
      value = path.pop();
      leaf = ng_cli_walk(["set"].concat(path));
    }
  }
  if (!leaf || typeof leaf._set !== "function") {
    ng_bus_reply("unknown set path: set " + parts.join(" "));
    return true;
  }
  if (value === null) {
    ng_bus_reply(ng_cli_help_text(["set"].concat(path)));
    return true;
  }
  if (leaf._values && leaf._values.indexOf(value) < 0) {
    ng_bus_reply("bad value '" + value + "'; want: " + leaf._values.join(" | "));
    return true;
  }
  leaf._set(value);
  return true;
}

var ng_cli = {
  _help: "console commands — try ? <cmd>",
  scene: {
    _help: "scene <id> — load a registered scene",
  },
  status: {
    _help: "status — gateway / view / render snapshot",
  },
  mcp: {
    _help: "mcp — alias for status",
  },
  set: {
    _help: "set <path> <value> — change runtime settings",
  },
};

function ng_bus_route_cmd(args) {
  var tok = ng_cli_expand(args);
  if (tok.length === 0) {
    return false;
  }

  if (tok[0] === "?") {
    ng_bus_reply(ng_cli_help_text(tok.slice(1)));
    return true;
  }

  if (tok[0] === "status" || tok[0] === "mcp") {
    if (typeof ng_cli_status === "function") {
      ng_bus_reply(ng_cli_status());
    } else {
      ng_bus_reply("status n/a");
    }
    return true;
  }

  if (tok[0] === "set") {
    return ng_cli_do_set(tok.slice(1));
  }

  if (tok[0] === "scene") {
    if (tok.length < 2) {
      ng_bus_reply("usage: scene <id>");
      return true;
    }
    if (typeof ng_bus_cmd === "function") {
      ng_bus_cmd("net", "scene " + tok[1]);
    } else {
      ng_bus_send("sim", "scene", tok[1]);
    }
    return true;
  }

  ng_bus_reply("unknown: " + tok.join(" "));
  return false;
}

function ng_bus_exec_line(line) {
  var args = ng_bus_parse(line);
  if (args.length === 0) {
    return false;
  }
  return ng_bus_route_cmd(args);
}

if (typeof ng_script_load === "function") {
  ng_script_load("cli/debug.js");
}
// agent: composer-2.5 | 2026-08-09 | JS CLI tree and help walk | 18f0e8
