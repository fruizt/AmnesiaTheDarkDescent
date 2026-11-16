# Amnesia: The Dark Descent (HPL2) — project notes

Fork of Frictional Games' GPLv3 source release: the HPL2 engine (`HPL2/`) and the
game code (`amnesia/src/game`, the `Lux*` classes). The game data (maps, models,
sounds, shaders in `core/shaders`) is NOT in the repo; it comes from an owned
Steam install and must not be committed or redistributed.

## Goal
Revitalize the game: modern 64-bit builds (Windows x64 first, macOS arm64), then a
Vulkan rendering backend, then hardware ray tracing (developed on a Windows PC
with an RTX 4070 Ti Super). Status, findings and the plan live in
[docs/MODERNIZATION.md](docs/MODERNIZATION.md) — read it before starting work.

## Working conventions
- Never add Claude/AI attribution to commits or PRs (no `Co-Authored-By`, no
  "Generated with" lines). Commits are authored by the user alone.
- Work directly on `master`; don't create branches unless asked.
- Commit or push only when the user asks.
- Every build rewrites the tracked `BuildID_*.h` headers (a build counter).
  Revert them with `git checkout -- '*BuildID_*.h'`; never commit them.
- Match HPL2's code style: PascalCase functions; Hungarian prefixes
  (`a` argument, `m` member, `g` global; `l` int, `f` float, `b` bool, `s` string,
  `v` vector, `p` pointer); type prefixes `c` class, `i` interface, `e` enum,
  `t` typedef.

## About the user
Comes from TypeScript and is learning C++ through this project. Explain C++
concepts with TypeScript analogies and, where useful, tiny compiled demos.

## Building
- macOS (x86_64 via Rosetta): `./build-macos.sh`, then `./run-macos.sh`
  (uses the Steam game data). The script unpacks `HPL2/dependencies.zip`
  and builds SDL 2.32.10 from source.
- Windows (32-bit, VS 2026): `.\build-windows.ps1`, then `.\run-windows.ps1`
  (Steam data on `F:\SteamLibrary\...`, override with `-GameDir`). 64-bit is
  in progress on the `win64` branch; see the plan.
- Third-party source (AngelScript, Newton) goes in the git-ignored `external/`
  via `cmake -P fetch-deps.cmake`; our fixes to it are `patch_dep` entries there.
