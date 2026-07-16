# Repository storage analysis

The repository viewer follows a disk-analyzer model: largest repositories first, then linked views for path weight, individual objects, snapshot records, and reclaim actions. The deep analyzer reports both packed and expanded blob sizes because neither number alone answers every question: packed bytes approximate physical Git payload, while expanded bytes explain the original content weight.

## Accounting model

- **Snapshot storage** is the exact logical size of files below the configured OpenCode snapshot root. It is not advertised as reclaimable space.
- **Current tree** is obtained from the snapshot repository's live Git index with `git write-tree`.
- **Current paths** come from `git rev-list --objects` and are aggregated by the first path component. Only local objects are charged to this snapshot repository; objects supplied through Git alternates are excluded.
- **Protected history** is the union of the current tree and every tree selected by the retention policy.
- **Safe estimate** is packed payload not reachable from any protected tree. Loose objects and Git's future repacking decisions make this deliberately conservative.
- **Current-only estimate** is packed payload not reachable from the live current tree. It describes the upper bound for the advanced history reset, not an automatic cleanup recommendation.

This mirrors established disk-analyzer interaction patterns: size-sorted navigation, a path-weight view, a largest-item list, and an action page that stays linked to the selected repository. The implementation uses the same kind of logical-versus-allocated distinction documented by [WizTree](https://diskanalyzer.com/guide), while the hierarchy is suitable for Qt's official [TreeView](https://doc.qt.io/qt-6/qml-qtquick-treeview.html) if deeper drill-down is added later.

## Why a deleted snapshot may save almost nothing

OpenCode snapshots are Git trees whose blobs can be shared by many trees. Removing one historical tree only frees objects that are unreachable from every retained tree. The viewer therefore does not sum snapshot directory names or database-record counts as if every snapshot were a full copy. It calculates reachability with [`git rev-list --disk-usage`](https://git-scm.com/docs/git-rev-list) semantics and reads pack object sizes through [`git verify-pack`](https://git-scm.com/docs/git-verify-pack).

## OpenCode compatibility and history reset

OpenCode initializes each snapshot store below its data directory, keeps a live index, writes trees for snapshots, and later restores those tree hashes. Its current implementation is visible in the upstream [OpenCode snapshot source](https://github.com/anomalyco/opencode/blob/dev/packages/opencode/src/snapshot/index.ts).

The advanced reset therefore does not delete the snapshot repository. It:

1. previews and verifies the current index tree;
2. requires an explicit project-scoped confirmation;
3. refuses to run while Git lock files are present;
4. re-reads and protects the live current tree immediately before execution;
5. expires unreachable reflogs and runs [`git gc --prune=now`](https://git-scm.com/docs/git-gc);
6. preserves Git configuration, index state, alternates, and the ability to write future snapshot trees.

The tradeoff is intentional and visible: old session Undo entries may reference trees that the reset removes. For this reason history reset is never included in batch cleanup and is not enabled until its dedicated preview succeeds.
