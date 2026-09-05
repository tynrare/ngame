# Project instructions

- Keep replies short and changes small; prefer deleting or simplifying code where reasonable.
- C/H hot paths: reuse buffers/scratch instead of allocating; prefer early returns, small helpers, shallow nesting, and tables over long branches. Exempt rare/init code.
- New/edited C/H/JS functions: brief `/** … */` docs with typed params and return (or void). Skip trivial one-liners and vendor code.
- Stamp changed blocks in C/H/JS/HTML/CMakeLists.txt/shell files immediately above the block; one top stamp suffices for new/small files. Format: `agent: <model> | <YYYY-MM-DD> | <summary ≤8 words, no paths> | <first 6 hex of SHA256(summary + filepath)>`. Use `//` for C/H/JS, `#` for CMake/shell, `<!-- … -->` for HTML (outside tags). Repeat exact stamps as one contiguous block within 3 lines after the last code line. Skip formatting-only edits.
- Complex flows (3+ coordinated steps, multiple actors, async/handshakes, or branch matrices): keep one canonical numbered `actor → action → outcome` playbook in the owning file's header, with a flow ID and non-obvious branches/invariants. Elsewhere reference flow ID + stable step number. For linked flows, keep a scope index of owners, ordering, and shared invariants without duplicating steps. Update with behavior changes; skip simple logic.
