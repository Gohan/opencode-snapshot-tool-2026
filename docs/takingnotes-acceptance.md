# takingNotes 真实数据验收记录

日期：2026-07-16  
状态：基线完成，等待关闭 OpenCode 后执行  

## 目标

使用用户明确允许清理的两个 `takingNotes` snapshot store，验证：

1. Safe GC 的预估与真实磁盘回收量；
2. Reset 后 live index tree 保留、旧 tree 不再可用；
3. Purge 后目标 store 被完整删除，worktree 与 `opencode.db` 不受影响；
4. OpenCode 后续 snapshot 操作能重新初始化被 Purge 的当前 store。

## 只读基线

Snapshot 根目录：`C:/Users/BZS-AIBOX/.local/share/opencode/snapshot`

### Legacy store（优先做 Safe GC → Reset → Purge）

- 相对路径：`6e9349edad66e415de70d7823daaf3f3951f8552/c833cb917bb4f8bb8f4239ba4002ec4d678a06ff`
- 实际大小：1,802,724,437 B
- 文件数：37
- Git objects：20,153 packed objects，2 packs，约 1.67 GiB
- index SHA-256：`F8D3D0AE39852F18C038A527ADC345D7E7FC64877C0E3D2285B60998EFD723D8`
- active lock：无
- 数据库 12 个历史 tree 中有 8 个实际存在于此 store。
- 对应 project 行已不在当前数据库中，但 worktree 仍指向 `takingNotes`。

### Current project store（做 Safe GC → Purge → 重建）

- 相对路径：`a13c42b93cc8a662d41920bf02fa9e4ae800e1c4/c833cb917bb4f8bb8f4239ba4002ec4d678a06ff`
- 实际大小：1,795,137,396 B
- 文件数：36
- Git objects：16,120 packed objects，1 pack，约 1.66 GiB
- index SHA-256：`4FF6AA98C6674260AC6FB9795700CCDCF2F1F498782751585DE33723E556F9A0`
- active lock：无
- 数据库 12 个历史 tree 中只有最新 tree `b58a0293780e587c90f83fd2e1334257fc3082f8` 实际存在于此 store。
- 当前数据库 project ID 为 `a13c42b93cc8a662d41920bf02fa9e4ae800e1c4`。

## 盘点中发现并修复的问题

旧扫描器只要 project ID 与 worktree 唯一匹配，就会把数据库 tree 直接显示在该 store 下，没有验证 tree 对象是否真的存在。因此 Viewer 曾把 12 条记录全部显示在 current project store 上，但其中 11 条并不在那个 Git store 中。

修复后：

- 每条数据库 snapshot tree 都必须通过 `git cat-file` 验证真实存在；
- 如果当前 project store 不含该 tree，会按相同 worktree 回查 legacy store；
- 新增 `SnapshotScanner.FindsMigratedProjectTreeInLegacyStoreForSameWorktree` 回归测试；
- 完整测试现为 13/13 通过。

## 执行顺序

### 阶段 A：所有 OpenCode 进程关闭后重新记录执行基线

- 再次确认 `opencode.exe` 数量为 0；
- 确认两个 store 没有 `.lock` 文件；
- 记录 `opencode.db` 大小、修改时间与 SHA-256；
- 记录 `takingNotes` worktree 的 Git status；
- 对两个 store 执行 Preview，并保存应用显示的预估值。

### 阶段 B：Legacy store

1. 执行 Safe GC，记录 before/after bytes 和应用测得的回收量；
2. 执行 Reset，确认 index SHA-256/live tree 保留，并检查旧 tree 是否已不可达；
3. 执行 Purge，确认只有该 legacy store 被删除；
4. 确认 current project store、worktree 与数据库仍存在且未被工具修改。

### 阶段 C：Current project store

1. 执行 Safe GC，记录真实回收量；
2. 执行 Purge，确认 snapshot store 消失，但 worktree 和数据库保持；
3. 启动 OpenCode 并触发一次正常 snapshot 操作；
4. 确认 `a13c42…/c833…` store 被重新创建、Git index 有效且后续 snapshot 可生成。

## 当前阻塞

基线采集时发现 5 个 `opencode.exe` 进程仍在运行。按照工具自身的安全要求，在这些进程全部关闭前不会执行任何 Safe GC、Reset 或 Purge。

