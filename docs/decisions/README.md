# Decision records

One file per big decision (architecture, technology, direction), numbered in
order. A record is not rewritten after it's accepted: if the decision changes,
a new record replaces it, and the old one's status says so. The living plan and
status stay in [MODERNIZATION.md](../MODERNIZATION.md).

Each record has: **Status**, **Context** (the problem and what we knew),
**Options considered** (including the rejected ones, and why), **Decision**,
**Consequences** (what it makes easy, what it costs, what stays open) and
**Evidence** (commits, logs, tests).

| # | Decision | Status | Date |
|---|---|---|---|
| [0001](0001-remix-frame-shown-through-opengl.md) | RTX mode: Remix renders off-screen, OpenGL shows its frame | Accepted | 2026-11-16 |
| [0002](0002-renderer-remix.md) | `cRendererRemix`, a renderer of its own for RTX mode | Accepted | 2026-11-16 |
