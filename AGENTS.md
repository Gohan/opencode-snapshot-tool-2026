# Agent instructions — OpenCode Snapshot Tool

## Product

Cross-platform Qt desktop application for inspecting and safely cleaning OpenCode's
Git-backed snapshot stores.

## Locked stack and boundaries

- C++20, Qt 6 Quick/QML, CMake + Ninja, GoogleTest.
- `libs/core` owns scanning, SQLite parsing, retention, Git/LFS cleanup and settings.
- QML is presentation only; no retention or filesystem logic in JavaScript.
- The app never mutates `opencode.db`.
- Cleanup defaults to preview. Destructive execution requires explicit GUI confirmation.
- Retained trees are protected by `refs/opencode-snapshot-tool/keep/*` before GC.

## TDD

Core changes follow red → green → refactor. Tests must cover retention policy, filesystem
accounting, Git mapping, cleanup preview and execution. Default tests cannot need network
or a user's OpenCode data; use temporary repositories and SQLite fixtures.

## Verification

Run:

```powershell
. .\scripts\dev-env.ps1
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build\dev --output-on-failure
```

GUI claims require launching the built executable and checking the real UI.
