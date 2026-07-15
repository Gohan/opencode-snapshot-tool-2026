# OpenCode Snapshot Tool

A cross-platform Qt 6 desktop application for inspecting OpenCode snapshot storage and safely reclaiming unreachable Git and Git LFS data.

## What it does

- Scans both legacy and project/worktree snapshot layouts.
- Joins snapshot tree hashes to OpenCode session metadata from SQLite.
- Reports actual on-disk bytes, including Git LFS and temporary pack files.
- Retains every snapshot seen within a configurable time window; if none are recent, retains the newest N trees per repository.
- Always protects the current Git index tree.
- Reports missing paths, database failures, and unmapped historical records without hiding partial results.
- Produces a read-only cleanup preview before enabling the cleanup action.
- Presents cleanup as an explicit two-step batch flow across all scanned repositories: batch preview first, then batch clean for the reviewed plan.
- Protects retained trees with private refs before Git GC, fails closed on uncertain LFS reachability, and removes only stale temporary files.

The default paths are discovered from `OPENCODE_DATA_HOME`, `XDG_DATA_HOME`, or the platform's normal OpenCode data directory. Settings are editable and persisted locally.
Configurable cleanup options include the recent retention window, per-repository fallback count, full GC versus prune-only behavior, LFS pruning, and the stale temporary-file threshold.

The interface follows the Bauhaus / Neo-Brutalist system in [`design.md`](design.md): warm paper surfaces, solid geometry, thick borders, explicit high-contrast control colors, and fully custom application window chrome. Space Grotesk and Inter are bundled under the SIL Open Font License.

## Build

Requirements: CMake 3.24+, Ninja, a C++20 compiler, Git, and Qt 6.8+ (`Core`, `Sql`, `Concurrent`, `Quick`, `QuickControls2`, and `Widgets`). GoogleTest is fetched only when tests are enabled.

Windows (MSVC and Qt are discovered; set `QT_ROOT` to override):

```powershell
pwsh -NoProfile -File .\scripts\build.ps1 -Preset dev -Test
pwsh -NoProfile -File .\scripts\build.ps1 -Preset release -Deploy
pwsh -NoProfile -File .\scripts\package-windows.ps1 -Version 0.1.1
```

If the test dependency cannot be downloaded, the GUI can still be built independently:

```powershell
pwsh -NoProfile -File .\scripts\build.ps1 -Preset dev -WithoutTests -Deploy
```

Linux/macOS, with Qt discoverable through `CMAKE_PREFIX_PATH`:

```sh
./scripts/build.sh dev --test
```

Run the development application on Windows from `build/dev/opencode-snapshot-tool.exe`. On other platforms, the executable is under the corresponding preset build directory.

## Safety model

Scanning and previewing are read-only. Cleanup is enabled only after a preview and a confirmation dialog. Close OpenCode or ensure it is idle before cleanup. The tool creates refs under `refs/opencode-snapshot-tool/keep/` for retained trees, then runs Git pruning according to the selected retention age. Cleanup is destructive and cannot be undone from the application.

## Test strategy

The core was developed test-first. Eight discovered tests cover retention boundaries, fallback quotas, current-index protection (including changes made after preview), directory discovery, exact filesystem sizing, actionable path warnings, real SQLite-to-Git tree mapping, distinct-session aggregation, and end-to-end preview/cleanup on an isolated synthetic repository. Real OpenCode data is used only for read-only scan and preview validation.

Keyboard shortcuts: `Ctrl+,` opens settings and `Ctrl+P` starts a read-only cleanup preview.
