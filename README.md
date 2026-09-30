# Modifier Key Framework

An SKSE framework for Skyrim Anniversary Edition (1.6.x and 1.7.x) that lets mods add their own
activation actions to NPCs, declared in simple JSON files, with one shared **modifier key** for a
second action. Replaces per-mod perk activation tricks and Dynamic Activation Key.

- The prompt keeps the vanilla look: the action on the first line, the NPC's name on the second.
- When a second action is available, the first label gets a marker (default `+`):
  `Give Potion +` / `Lydia`. Holding the modifier key switches it to the second action: `Search` / `Lydia`.

> **Status: in development (v0.1).** Rules, prompt text, actions (SKSE mod events) and the
> modifier key work; used by Death Timer - Immersive Bleedout.

## For mod authors: rule files

Put one JSON file per mod in `SKSE/Plugins/ModifierKeyFramework/` (comments allowed):

```jsonc
{
  "rules": [
    {
      "id": "downed",
      "target": "npc",
      "requires": { "item": "Death Timer - Immersive Bleedout.esp|0x807", "alive": true },
      "not":      { "faction": "Skyrim.esm|0x2BE3B" },  // e.g. never for guards (not in Death Timer's real file)
      "primary":   { "label": "Give Potion", "event": "DeathTimer_GivePotion" },
      "alternate": { "label": "Search",      "event": "DeathTimer_Search" },
      "priority": 50
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `id` | a name for the rule, used in log messages |
| `target` | `"npc"` (only NPCs for now) |
| `requires` | optional: conditions that must **all** hold (see below) |
| `not` | optional: conditions of which **none** may hold (same keys as `requires`) |
| `primary` | label shown, and the SKSE mod event sent on activation (the NPC is the event's sender). Without an `event`, only the label changes and activation stays vanilla |
| `alternate` | optional second action, used while the modifier key is held |
| `priority` | when several rules match, the highest wins (default 0) |

### When rules compete

Only one rule runs per NPC: the matching rule with the highest `priority`. On a tie, the rule from
the file whose name sorts first wins (alphabetical, ignoring case), then the one listed first in
that file. The result never depends on install order.

The log (`ModifierKeyFramework.log`) helps sort out overlaps between mods:

- at startup, every priority shared by rules from different files, in winning order;
- when the player activates an NPC that rules from other files also match, which rule was used and
  which were ignored (each pair once per session).

### Conditions

Forms are written `"Plugin.esp|0xLocalID"` (light plugins too). A form condition also takes a list,
which means *any one of them*.

| Condition | Holds when the NPC… |
|---|---|
| `item` | carries the item |
| `keyword` | has the keyword (on the NPC or its race) |
| `faction` | is in the faction. With a minimum rank: `{ "form": "Plugin.esp\|0xID", "minRank": 1 }` |
| `race` | is of the race |
| `npc` | is this NPC (the base NPC record, not a placed reference) |
| `alive` | `true`: is alive / `false`: is dead |
| `teammate` | is the player's teammate (a follower) |
| `essential` | is essential |
| `protected` | is protected |
| `bleedingOut` | is in bleedout |
| `unconscious` | is unconscious |
| `sitting` | is sitting (furniture, or riding) |
| `sleeping` | is asleep in a bed |
| `inCombat` | is in combat |

The `true`/`false` conditions compare with the value given: `"teammate": false` in `requires`
means *not* a teammate.

**Optional mods:** if a listed form's plugin isn't installed, that entry is ignored (logged). A
`requires` condition left with no forms can never hold, so the rule is skipped. That makes it safe
to mention optional mods, especially in `not`.

**Mistakes are logged, not guessed at:** a misspelled condition, a malformed or missing form, or a
form of the wrong type skips the rule with the reason in `ModifierKeyFramework.log`
(`Documents/My Games/Skyrim Special Edition/SKSE/`).

## Settings

`SKSE/Plugins/ModifierKeyFramework.ini`: the modifier key (DirectX scan code, default Left Shift),
a gamepad button, and the alternate-action marker (empty = no marker).

## Known issues

- **Better Third Person Selection (BTPS)**: with the keyboard/mouse button icon, long labels overlap
  the icon. This is BTPS's prompt layout (it happens with any mod's long label, including Dynamic
  Activation Key and Use Or Take), not this framework; the gamepad icon is unaffected.

## Building

Same toolchain as the author's other plugins: Visual Studio 2022/2026 Build Tools (C++ workload,
with CMake, Ninja and vcpkg) and Git. [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG)
is a submodule pinned to v10.0.0.

```
git clone --recursive <this repository>
cd skse
build.bat
```

Paths in `skse/CMakePresets.json`, `skse/build.bat` and `DEPLOY_DIR` in `skse/CMakeLists.txt` are set
for the author's machine; adjust them for yours. The author's MO2 mod folder is a directory junction
to `mod/`.

## License

GPL-3.0-or-later (the plugin statically links CommonLibSSE-NG). See [skse/LICENSE](skse/LICENSE).

## Credits

- **eXgamble**
- **powerofthree**: [Read Or Take SKSE](https://github.com/powerof3/ReadOrTakeBooks), whose prompt
  and modifier-key approach inspired this framework.
- The CommonLibSSE-NG maintainers and contributors.
