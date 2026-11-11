# EDOPro Team Play

A 2v2 mode for [EDOPro](https://github.com/edo9300/edopro) where **both teammates play at the same
time**. Each duelist has their own hand, Deck and Extra Deck. The field, GY, banished zone and LP
are shared by the team.

- Rules: [TEAMPLAY_RULES.md](TEAMPLAY_RULES.md)
- Engine source (ocgcore fork): https://github.com/itsprofoak26-code/ygopro-core/tree/teamplay
- Client source (EDOPro fork): https://github.com/itsprofoak26-code/edopro/tree/teamplay

## Layout

This repo holds the rules, scripts and tests. The source trees are separate checkouts next to it:

| Folder | What |
|---|---|
| `ocgcore/` | ygopro-core fork, branch `teamplay` |
| `edopro/` | EDOPro fork, branch `teamplay` (`edopro/ocgcore` is a junction to `../ocgcore`) |
| `CardScripts/` | ProjectIgnis/CardScripts (unchanged) |
| `run/` | ProjectIgnis/Distribution asset pack, plus our build (`run/script` is a junction to `CardScripts/`) |
| `deps/` | premake, vcpkg, DirectX SDK |

## Build and test (Windows, Visual Studio 2022)

```powershell
.\build.ps1                        # Debug build, copied into run/
.\build.ps1 -Release               # Release build
powershell -ExecutionPolicy Bypass -File .\tests\run_core_test.ps1   # headless rules tests
```

## Licence

EDOPro and ygopro-core are licensed under the GNU AGPL v3. This mode's changes are in the forks
linked above, under the same licence.
