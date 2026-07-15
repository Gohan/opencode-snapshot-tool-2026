# Architecture

The project separates policy and storage mutation from presentation:

- `libs/core`: C++ domain types, Git process adapter, SQLite scanner, retention policy, settings, and cleaner.
- `apps/snapshot-tool`: asynchronous controller and Qt Quick presentation.
- `tests`: policy, filesystem, SQLite/Git integration, and destructive synthetic cleanup tests.

The QML layer contains layout and bindings only. Long-running scans, previews, and cleanup run through `QtConcurrent` and return immutable result values to the UI thread.

## Cleanup sequence

1. Scan actual directory bytes and map database snapshot hashes to repositories.
2. Apply the retention window or per-repository fallback.
3. Preview retained/released trees, stale temporary bytes, and unreferenced LFS objects without writes.
4. Re-read the current index tree at execution time, then create or update all private keep refs atomically with `git update-ref --stdin`; inability to protect the current tree aborts cleanup.
5. Remove only LFS objects proven absent from every retained tree and temporary files older than the configured threshold.
6. Expire eligible reflogs and run Git GC/prune using the configured age.
7. Recalculate actual directory bytes and rescan the UI.

Unknown Git/LFS responses preserve data. Tests execute destructive behavior only in `QTemporaryDir` repositories.

Repository measurement and current-index discovery use bounded four-way concurrency. Database records first map by exact project/worktree identity; only ambiguous legacy records invoke Git object verification. Partial scan problems are returned as warnings and displayed by the controller.
