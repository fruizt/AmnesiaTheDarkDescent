# Amnesia: The Dark Descent (HPL2) — project notes

Fork of Frictional Games' GPLv3 source release: the HPL2 engine (`HPL2/`) and the
game code (`amnesia/src/game`, the `Lux*` classes). The game data (maps, models,
sounds, shaders in `core/shaders`) is NOT in the repo; it comes from an owned
Steam install and must not be committed or redistributed.

## Goal
A visual remaster: path-traced lighting in an "RTX mode" through the RTX Remix API
(top priority: find out whether it works), the OpenGL renderer kept as "classic
mode", and better assets shipped through the engine so both modes benefit.
Windows x64 build done (developed on a PC with an RTX 4070 Ti Super). Status,
decisions and the plan live in [docs/MODERNIZATION.md](docs/MODERNIZATION.md) —
read it before starting work.

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
- Windows (VS 2026): `.\build-windows.ps1`, then `.\run-windows.ps1`
  (Steam data on `F:\SteamLibrary\...`, override with `-GameDir`). x64 is the
  default (libraries from vcpkg via `vcpkg.json`, built on first configure);
  `-Arch Win32` builds the 32-bit baseline with the bundled libraries.
- Third-party source (AngelScript, Newton) goes in the git-ignored `external/`
  via `cmake -P fetch-deps.cmake`; our fixes to it are `patch_dep` entries there.
