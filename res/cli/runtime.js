// agent: composer-2.5 | 2026-08-09 | tree-driven CLI runtime | 43a91e
// agent: composer-2.5 | 2026-08-09 | CLI expand comment update | 277128
// agent: composer-2.5 | 2026-08-09 | CLI keep numeric tokens | b6d653
/** Parse a console line into whitespace tokens. */
function ng_bus_parse(line) {
  return line.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
}

/**
 * Expand dotted path tokens (`render.rc.quality` → render,rc,quality).
 * Keep numeric literals intact so `0.5` is not split into `0` `5`.
 */
function ng_cli_expand(args) {
  var out = [];
  for (var i = 0; i < args.length; i++) {
    var s = String(args[i]);
    if (/^-?\d+(\.\d+)?$/.test(s)) {
      out.push(s);
      continue;
    }
    var parts = s.split(".");
    for (var j = 0; j < parts.length; j++) {
      if (parts[j].length > 0) {
        out.push(parts[j]);
      }
    }
  }
  return out;
}

/** Non-_ child keys of a CLI node, sorted. */
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

/** Walk ng_cli by path parts; null if missing. */
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

/** Help text for a path (or root when parts empty). */
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
  }
  if (typeof node._get === "function") {
    var cur = node._get();
    if (cur !== null && cur !== undefined && cur !== "") {
      lines.push("current: " + cur);
    }
  }
  if (lines.length === 0) {
    lines.push("(no help)");
  }
  return lines.join("\n");
}

/**
 * Dispatch expanded tokens against ng_cli (_run / _set).
 * @returns {boolean} true if handled
 */
function ng_bus_route_cmd(args) {
  var tok = ng_cli_expand(args);
  if (tok.length === 0) {
    return false;
  }

  if (tok[0] === "?") {
    ng_bus_reply(ng_cli_help_text(tok.slice(1)));
    return true;
  }

  var node = ng_cli;
  var path = [];
  var i = 0;
  while (i < tok.length) {
    var key = tok[i];
    if (!node || typeof node !== "object" || !Object.prototype.hasOwnProperty.call(node, key) || key.charAt(0) === "_") {
      break;
    }
    node = node[key];
    path.push(key);
    i++;
  }
  var rest = tok.slice(i);

  if (path.length === 0) {
    ng_bus_reply("unknown: " + tok.join(" "));
    return false;
  }

  if (typeof node._run === "function") {
    node._run(rest);
    return true;
  }

  if (typeof node._set === "function") {
    if (rest.length === 0) {
      ng_bus_reply(ng_cli_help_text(path));
      return true;
    }
    if (rest.length !== 1) {
      ng_bus_reply("usage: " + path.join(" ") + " <value>");
      return true;
    }
    var value = rest[0];
    if (node._values && node._values.indexOf(value) < 0) {
      ng_bus_reply("bad value '" + value + "'; want: " + node._values.join(" | "));
      return true;
    }
    node._set(value);
    return true;
  }

  if (rest.length === 0) {
    ng_bus_reply(ng_cli_help_text(path));
    return true;
  }

  ng_bus_reply("unknown: " + tok.join(" "));
  return false;
}

/** Entry point from C: run one console line. */
function ng_bus_exec_line(line) {
  var args = ng_bus_parse(line);
  if (args.length === 0) {
    return false;
  }
  return ng_bus_route_cmd(args);
}
// agent: composer-2.5 | 2026-08-09 | tree-driven CLI runtime | 43a91e
// agent: composer-2.5 | 2026-08-09 | CLI expand comment update | 277128
// agent: composer-2.5 | 2026-08-09 | CLI keep numeric tokens | b6d653
