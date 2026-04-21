# GitHub Copilot Instructions for KTX

## What This Repo Is

- KTX is a QuakeWorld server-side game module built as both a native shared library and a QVM.
- Entry point: `vmMain()` in `src/g_main.c`.
- Entity loading: `G_SpawnEntitiesFromString()` in `src/g_spawn.c`.
- Runtime rule correction and cvar registration mostly live in `src/world.c`.
- If you add a new source file, update `SRC_COMMON` in `CMakeLists.txt` or it will be missing from both native and QVM builds.

## Build And Verify

- Native local build: `cmake -B build . && cmake --build build`
- Cross/QVM builds: `./build_cmake.sh <target>`
- CI builds Linux, Windows, macOS verification, and QVM from `.github/workflows/main.yml`.
- There is no formal unit-test suite in this repo. Compile success and in-game smoke testing are the primary checks.

## Style And Editing Conventions

- Code is C, tab-indented, with Allman braces. See `.clang-format`.
- Prefer small, local changes over large abstractions. This codebase relies heavily on central dispatch plus mode-specific branches.
- Reuse existing helpers such as `find`, `ez_find`, `trap_findradius`, `cvar`, `cvar_fset`, `respawn_model_name`, and the existing mode predicates.
- Main mode predicates are split across files:
  - `isDuel()`, `isTeam()`, `isFFA()`, `isCTF()` in `src/g_utils.c`
  - `isRA()` in `src/arena.c`
  - `isCA()` in `src/clan_arena.c`
  - `isRACE()` in `src/race.c`
  - `isHoonyModeAny()`, `isHoonyModeDuel()`, `isHoonyModeTDM()` in `src/hoonymode.c`

## Core Spawn Architecture

- Player entry flow is:
  - `GAME_PUT_CLIENT_IN_SERVER` in `src/g_main.c`
  - `k_respawn()` in `src/client.c`
  - `PutClientInServer()` in `src/client.c`
- Generic spawn selection flow is:
  - `PutClientInServer()`
  - `SelectSpawnPoint()`
  - `Sub_SelectSpawnPoint()`
- `SP_info_player_deathmatch()` in `src/client.c` is important:
  - assigns `spot->cnt` so spawn indices are stable
  - fixes hanging spawn heights with `droptofloor()`
  - calls `HM_name_map_spawn()`
- Do not bypass normal spawn entity initialization when changing spawn logic.

## What Already Controls Spawning

- `Sub_SelectSpawnPoint()` in `src/client.c` already owns most generic spawn policy:
  - `k_spw` respawn models from `-1` to `4`
  - safety checks using `trap_findradius`
  - recent-spawn timing via `k_1spawn`
  - anti-repeat behavior via `k_lastspawn`
  - yawnmode weighted spawn selection via `self->spawn_weights`
  - wipeout custom radius and line-of-sight behavior through `WO_GetSpawnRadius()`
- `GetEffectiveSpawnRadius()` in `src/client.c` is the narrow hook for map-specific radius overrides.

## Existing Spawn Overrides And Related Systems

- Hoonymode override:
  - `HM_choose_spawn_point()` in `src/hoonymode.c`
  - spawn nomination and assignment also live in `src/hoonymode.c`
- CTF override:
  - `PutClientInServer()` branches on `k_ctf_based_spawn`
  - uses `info_player_team1`, `info_player_team2`, `info_player_team1_deathmatch`, `info_player_team2_deathmatch`, and normal DM spawns
- Rocket Arena override:
  - RA winners/losers use `info_teleport_destination`
  - see `src/arena.c`
- Clan Arena / Wipeout:
  - custom spawn radius tables live in `src/clan_arena.c`
  - generic spawn selection still depends on `SelectSpawnPoint()` for many cases
- Race:
  - race mode hardcodes spawn-related defaults in `race_settings` in `src/race.c`
  - race explicitly forces `k_spw 1`
- Rune placement:
  - `src/runes.c` reuses `SelectSpawnPoint()`
  - generic spawn changes can affect rune placement too
- Spawn visibility and spawnicide:
  - marker logic is in `src/items.c`
  - user-facing toggles are in `src/commands.c`

## If You Are Adding A New Spawn Mode

- Decide first whether the feature is:
  - a new generic respawn algorithm alongside `k_spw`, or
  - a dedicated mode-specific override like hoonymode, RA, or a CTF-specific rule
- For a new generic `k_spw` mode, update all of these together:
  - selection logic in `Sub_SelectSpawnPoint()` in `src/client.c`
  - cycling and bounds in `ToggleRespawns()` in `src/commands.c`
  - display names in `respawn_model_name()` and `respawn_model_name_short()` in `src/g_utils.c`
  - match rules/status text that prints respawn mode in `src/match.c`
  - any hardcoded presets that assume the existing range, especially race defaults in `src/race.c`
- Keep generic spawn rules centralized in `src/client.c`. Do not scatter new `k_spw` checks across unrelated gameplay code unless a specific mode truly needs an override.
- Preserve existing semantics unless the new feature is explicitly meant to change them:
  - `k_1spawn` recent-spawn protection
  - `k_lastspawn` anti-repeat behavior
  - wipeout radius/LOS handling
  - yawnmode weighting
- If the feature is mode-specific, prefer a narrow branch near the top of `PutClientInServer()` instead of complicating the generic selector.

## Map And Entity Metadata

- Spawn-related entity keys are parsed in `fields[]` in `src/g_spawn.c`.
- Hoonymode already extends spawn metadata with fields like:
  - `spawn_items`
  - `spawn_armorvalue`
  - `spawn_ammo_shells`, `spawn_ammo_nails`, `spawn_ammo_rockets`, `spawn_ammo_cells`
  - `spawn_initial_delay`
- If you need new per-entity spawn metadata:
  - add storage to `gedict_t` in `include/progs.h`
  - parse the key in `fields[]` in `src/g_spawn.c`
  - keep the parsing local and avoid broad side effects
- If you need per-map exceptions, follow the wipeout pattern in `src/clan_arena.c`: isolate the table and query it through a small helper.

## Other Files To Review When Spawn Logic Changes

- `src/items.c` for visible spawn markers and spawnicide behavior
- `src/spectate.c` because spectator impulse `1` cycles through deathmatch spawns
- `src/bot_loadmap.c` because bots enumerate spawn entities when loading maps
- `src/runes.c` because rune placement uses the generic spawn selector
- `src/hoonymode.c` because pregame spawn selection and logging depend on stable spawn behavior

## Practical Verification Checklist

- Build with `cmake -B build . && cmake --build build`.
- If spawn selection changed, smoke-test at least:
  - normal DM spawn flow
  - CTF with `k_ctf_based_spawn 1`
  - CTF with `k_ctf_based_spawn 2`
  - hoonymode enabled
  - clan arena / wipeout
  - rune spawning if the generic selector changed
- If you add a new source file, verify both native and QVM builds still include it via `CMakeLists.txt`.