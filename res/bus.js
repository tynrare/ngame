// agent: composer-2.5 | 2026-07-25 | JS bus routing layer | 8f1c4d
function ng_bus_parse(line) {
  return line.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
}

function ng_bus_route_cmd(args) {
  if (args[0] === "scene" && args.length >= 2) {
    ng_bus_send("sim", args[0], args[1]);
    return true;
  }
  ng_bus_reply("unknown: " + args.join(" "));
  return false;
}

function ng_bus_exec_line(line) {
  var args = ng_bus_parse(line);
  if (args.length === 0) {
    return false;
  }
  return ng_bus_route_cmd(args);
}
