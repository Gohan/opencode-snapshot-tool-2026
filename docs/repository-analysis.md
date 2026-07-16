# Repository storage analysis

The repository viewer follows a disk-analyzer model: largest repositories first, then linked views for drillable path weight, file types, individual objects, snapshot records, and reclaim actions. Clicking a directory moves one level deeper; clicking a file or file type opens the object list with that filter applied. The deep analyzer reports both packed and expanded blob sizes because neither number alone answers every question: packed bytes approximate physical Git payload, while expanded bytes explain the original content weight.

## Accounting model

- **Snapshot storage** is the exact logical size of files below the configured OpenCode snapshot root. It is not advertised as reclaimable space.
- **Current tree** is obtained from the snapshot repository's live Git index with `git write-tree`.
- **Current paths** come from `git rev-list --objects` and are aggregated by the first path component. Only local objects are charged to this snapshot repository; objects supplied through Git alternates are excluded.
- **Path drill-down** aggregates every directory prefix and file leaf, allowing the same packed payload to be followed from a top-level folder to the exact blob path.
- **File types** normalize extensions case-insensitively and separately group files without an extension.
- **Protected history** is the union of the current tree and every tree selected by the retention policy.
- **Reachability classes** split local payload into current-only, shared by current and retained history, retained-history-only, and unprotected objects. Current-only means required by the live state, not reclaimable.
- **Safe estimate** is packed payload not reachable from any protected tree. Loose objects and Git's future repacking decisions make this deliberately conservative.
- **Current-only estimate** is packed payload not reachable from the live current tree. It describes the upper bound for the advanced history reset, not an automatic cleanup recommendation.

This mirrors established disk-analyzer interaction patterns: size-sorted navigation, hierarchical path drill-down, type grouping, a largest-item list, and an action page that stays linked to the selected repository. The implementation uses the same kind of logical-versus-allocated distinction documented by [WizTree](https://diskanalyzer.com/guide), with a lightweight linked-list hierarchy rather than forcing a dense desktop tree into the current layout. Qt's official [TreeView](https://doc.qt.io/qt-6/qml-qtquick-treeview.html) remains a reference for keyboard and hierarchy semantics.

## Why a deleted snapshot may save almost nothing

OpenCode snapshots are Git trees whose blobs can be shared by many trees. Removing one historical tree only frees objects that are unreachable from every retained tree. The viewer therefore does not sum snapshot directory names or database-record counts as if every snapshot were a full copy. It calculates reachability with [`git rev-list --disk-usage`](https://git-scm.com/docs/git-rev-list) semantics and reads pack object sizes through [`git verify-pack`](https://git-scm.com/docs/git-verify-pack).

## OpenCode compatibility, history reset, and full purge

OpenCode initializes each snapshot store below its data directory, keeps a live index, writes trees for snapshots, and later restores those tree hashes. Its current implementation is visible in the upstream [OpenCode snapshot source](https://github.com/anomalyco/opencode/blob/dev/packages/opencode/src/snapshot/index.ts).

The advanced reset therefore does not delete the snapshot repository. It:

1. previews and verifies the current index tree;
2. requires an explicit project-scoped confirmation;
3. refuses to run while Git lock files are present;
4. re-reads and protects the live current tree immediately before execution;
5. expires unreachable reflogs and runs [`git gc --prune=now`](https://git-scm.com/docs/git-gc);
6. preserves Git configuration, index state, alternates, and the ability to write future snapshot trees.

The tradeoff is intentional and visible: old session Undo entries may reference trees that the reset removes. For this reason history reset is never included in batch cleanup and is not enabled until its dedicated preview succeeds.

Full-store purge is a separate, stronger operation for the case where the live snapshot itself accounts for nearly all storage and the user explicitly wants to reclaim the entire selected store. Upstream OpenCode creates the snapshot directory recursively and initializes Git whenever its expected snapshot gitdir is missing. The tool therefore removes only that selected gitdir and lets OpenCode own the later reinitialization instead of constructing a possibly stale replacement itself.

Before full purge, the tool:

1. canonicalizes both the configured snapshot root and selected gitdir and requires the target to be a strict child of the root;
2. validates that the live index can still be written;
3. produces a project-only preview using the measured complete directory size;
4. requires OpenCode-closed acknowledgement and exact project-name entry;
5. repeats path, Git-lock, and live-index validation immediately before removal.

The live-index validation uses `git write-tree`. It does not change worktree files or release history during preview, but Git may materialize the current tree object in the snapshot object database; the UI therefore treats it as a non-destructive validation preview rather than promising zero filesystem writes.

The worktree and `opencode.db` are outside the deletion target and are never mutated. All current and historical Undo hashes for the removed store become unavailable. An isolated integration test removes a store, verifies the path boundary and lock refusal, then recreates the missing gitdir and writes a future tree using the same initialization shape as OpenCode.
