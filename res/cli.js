// agent: composer-2.5 | 2026-07-25 | CLI parse and forward | 2f6b8c
function ng_cli_parse(line) {
  return line.trim().split(/\s+/).filter(function (s) { return s.length > 0; });
}

function ng_cli_exec_line(line) {
  var args = ng_cli_parse(line);
  if (args.length === 0) return false;
  ng_cli_dispatch.apply(null, args);
  return true;
}
