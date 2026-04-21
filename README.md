# KTX: a QuakeWorld server modification
![KTX Logo](https://raw.githubusercontent.com/QW-Group/ktx/master/resources/logo/ktx.png)

**[KTX][ktx]** (Kombat Teams eXtreme) is a popular **QuakeWorld** server modification, adding numerous features to the core features of the server.

Although it had been developed to be **Quakeworld** server agnostic, it has over the years been developed very close to **[MVDSV][mvdsv]** to which it has become an extent, thus compatibility with other **Quakeworld** servers might not have been maintained.

## What This Repo Is

KTX is the server-side gameplay module for **QuakeWorld**, most commonly used with **[MVDSV][mvdsv]**. It owns match flow, respawn rules, admin commands, bots, stats/logging, and mode-specific gameplay such as duel, teamplay, CTF, race, and arena variants.

## Random Surface Respawns

Setting `k_spw` to `5` enables the random surface respawn mode for supported duel and teamplay games. Instead of respawning only on the map's authored deathmatch spawn entities, KTX builds a pool of valid floor positions at match start and spawns players from that generated pool.

The generator only keeps points where the player hull fits and where the spot is usable in normal play. If a map cannot produce enough safe points, KTX automatically falls back to normal deathmatch spawns instead of leaving the server in a broken state.

For a ready-to-use example config, see [resources/example-configs/ktx/ktx_random_respawns.cfg](resources/example-configs/ktx/ktx_random_respawns.cfg).

### Random Respawn Cvars

| Cvar | Description |
| --- | --- |
| `k_spw` | Set this to `5` to enable random surface respawns. |
| `k_spw_rnd_min_points` | Minimum number of valid generated points required before random surface respawns stay enabled on the current map. If generation cannot reach this number, KTX falls back to normal deathmatch spawns. |
| `k_spw_rnd_target_count` | Exact pool size to aim for. Set it to `0` to let KTX choose automatically from the map's original deathmatch spawn count. |
| `k_spw_rnd_target_scale` | Auto-scaling multiplier used when `k_spw_rnd_target_count` is `0`. Higher values create a larger random spawn pool relative to the map's original spawn count. |
| `k_spw_rnd_attempts_per_point` | How hard the generator searches for valid spawn locations. Higher values improve coverage on awkward maps, but also increase match-start preparation work. |
| `k_spw_rnd_min_distance` | Minimum spacing between generated random spawn points. Raise it if you want the pool itself to be spread out more physically. |
| `k_spw_rnd_area_mode` | Soft anti-repeat rule for consecutive spawns. `0` disables the bias, `1` prefers spots farther from your last random spawn origin, and `2` prefers spots associated with a different nearest authored deathmatch-spawn area. |
| `k_spw_rnd_spread` | Strength of the area bias. `0` turns it off, `1` is low, `2` is medium, and `3` is high. Higher values make repeat spawns in the same area less likely without turning the rule into a hard ban. |

## Getting Started

The following instructions will help you get **[KTX][ktx]** installed on a running **[MVDSV][mvdsv]** server using prebuilt binaries. Details on how to compile your own **[KTX][ktx]** binary will also be included to match specific architectures or for development purposes.

## Supported architectures

The following architectures are fully supported by **[KTX][ktx]** and are available as prebuilt binaries:
* Linux amd64 (Intel and AMD 64-bits processors)
* Linux i686 (Intel and AMD 32-bit processors)
* Linux aarch (ARM 64-bit processors)
* Linux armhf (ARM 32-bit processors)
* Windows x64 (Intel and AMD 64-bits processors)
* Windows x86 (Intel and AMD 32-bit processors)

## Prebuilt binaries

You can find the prebuilt binaries on [this download page][ktx-builds].

## Prerequisites

**[KTX][ktx]** is a server mod and won't run without a proper **Quakeworld** server set up. **[MVDSV][mvdsv]** is the recommended one, but **[FTE][fte]** might work as well (unconfirmed with current code).

## Installing

For more detailed information we suggest looking at the [nQuake server][nquake-linux], which uses **[MVDSV][mvdsv]** and **[KTX][ktx]** as **QuakeWorld** server.

## Building binaries

### Build from source with CMake

Assuming you have installed essential build tools and ``CMake``
```bash
mkdir build && cmake -B build . && cmake --build build
```
Build artifacts would be inside ``build/`` directory, for unix like systems it would be ``qwprogs.so``.

You can also use ``build_cmake.sh`` script, it mostly suitable for cross compilation
and probably useless for experienced CMake user.
Some examples:
```
./build_cmake.sh linux-amd64
```
should build KTX for ``linux-amd64`` platform, release version, check [cross-cmake](tools/cross-cmake) directory for all platforms

```
B=Debug ./build_cmake.sh linux-amd64
```
should build KTX for linux-amd64 platform with debug

```
V=1 B=Debug ./build_cmake.sh linux-amd64
```
should build KTX for linux-amd64 platform with debug, verbose (useful if you need validate compiler flags)

```
V=1 B=Debug BOT_SUPPORT=OFF ./build_cmake.sh linux-amd64
```

same as above but compile without bot support

```
G="Unix Makefiles" ./build_cmake.sh linux-amd64
```

force CMake generator to be unix makefiles

```
./build_cmake.sh linux-amd64 qvm
```

build KTX for ``linux-amd64`` and ``QVM`` version, you can provide
any platform combinations.

## Versioning

For the versions available, see the [tags on this repository][ktx-tags].

## Authors

(Listed by last name alphabetic order)

* **Ivan** *"qqshka"* **Bolsunov**
* **Dominic** *"oldman"* **Evans**
* **Foobar**
* **Anton** *"tonik"* **Gavrilov**
* **Andrew** *"ult"* **Grondalski**
* **Paul Klumpp**
* **Niclas** *"empezar"* **Lindström**
* **Dmitry** *"disconnect"* **Musatov**
* **Peter** *"meag"* **Nicol**
* **Andreas** *"molgrum"* **Nilsson**
* **Alexandre** *"deurk"* **Nizoux**
* **Tero** *"Renzo"* **Parkkonen**
* **Joseph** *"bogojoker"* **Pecoraro**
* **Michał** *"\_KaszpiR\_"* **Sochoń**
* **Jonny** *"dimman"* **Svärd**
* **Vladimir** *"VVD"* **Vladimirovich**
* **Florian** *"Tuna"* **Zwoch**

## Code of Conduct

We try to stick to our code of conduct when it comes to interaction around this project. See the [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) file for details.

## License

This project is licensed under the GPL-2.0 License - see the [LICENSE.md](LICENSE.md) file for details.

## Acknowledgments

* Thanks to kemiKal, Cenobite, Sturm and Fang for Kombat teams 2.21 which has served as a base for **[KTX][ktx]**.
* Thanks to **Jon "bps" Cednert** for the **[KTX][ktx]** logo.
* Thanks to the fine folks on [Quakeworld Discord][discord-qw] for their support and ideas.

[ktx]: https://github.com/QW-Group/ktx
[ktx-tags]: https://github.com/QW-Group/ktx/tags
[ktx-builds]: https://builds.quakeworld.nu/ktx
[mvdsv]: https://github.com/QW-Group/mvdsv
[nquake-linux]: https://github.com/nQuake/server-linux
[discord-qw]: http://discord.quake.world/
