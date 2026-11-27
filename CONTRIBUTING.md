# Contributing

## Changes

- One change per contribution, with a short description of what it does and why.
- C++17. Format with clang-format 22 and the repository's `.clang-format`; keep `.clang-tidy` checks clean.
- Name things like the surrounding code; keep headers self-contained.
- Public headers (`include/`), messages users see and documentation are in English.
- Add or update tests with the change: Catch2 tests next to the code, declared with `add_giga_edge_test`
  ([docs/writing-a-module.md](docs/writing-a-module.md#tests)). Cover the error paths and edge cases, not only the
  happy path; for changes across nodes, test the whole graph.
- When you add or change a node, update its descriptions in `REGISTER_NODE` and in the port macros
  ([docs/writing-a-node.md](docs/writing-a-node.md)).

## Before sending

```bash
./tools/scripts/build_giga_edge.sh --target-os <os> --preset opensource --build-tests
ctest --test-dir build_<os>/giga_edge
```

A change to the public API or to the graph and config contract follows [docs/versioning.md](docs/versioning.md).

Report security issues privately, as described in [SECURITY.md](SECURITY.md), not in public issues.
