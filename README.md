# Modifier Key Framework

An SKSE framework for Skyrim Anniversary Edition (1.6.x and 1.7.x) that lets mods add their own
activation actions to NPCs, declared in simple JSON files, with one shared **modifier key** for a
second action. An alternative to perk-based activation and Dynamic Activation Key: no plugin, no
perk, no script needed just to show a prompt.

- The prompt keeps the vanilla look: the action on the first line, the NPC's name on the second.
- When a second action is available, the first label gets a marker (default `+`):
  `Give Potion +` / `Lydia`. Holding the modifier key switches it to the second action: `Search` / `Lydia`.
- One key for every mod that uses the framework (default Left Shift, Left Shoulder on a gamepad).

**Requirements:** [SKSE64](https://skse.silverlock.org/),
[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).
No plugin (esp) of its own, so it takes no load order slot.

**Download:** [Nexus Mods](https://www.nexusmods.com/skyrimspecialedition/mods/193566)

## For mod authors

### Quick start

1. Put a JSON rule file in `SKSE/Plugins/ModifierKeyFramework/`, named after your mod (comments allowed).
2. Handle its event in Papyrus.

```jsonc
{
  "$schema": "https://raw.githubusercontent.com/eXgamble/Modifier-Key-Framework/master/schema/rules.schema.json",
  "version": 1,
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

A complete, commented example (rule file + Papyrus script, including an MCM-style on/off switch)
is in [examples/FollowerOrders](examples/FollowerOrders): followers keep their normal `Talk`, and
holding the modifier key gives a `Wait Here` order.

Your mod only talks to the framework through JSON and Papyrus, so the framework's GPL license
doesn't apply to it.

### Rule file

| Key | Meaning |
|---|---|
| `$schema` | optional: [the schema](schema/rules.schema.json), for autocomplete and checks in your editor (VS Code and others). Ignored in game |
| `version` | optional: the rule format this file is written for (currently `1`, the default). A file written for a newer format than the installed framework reads is skipped with a message to update the framework |
| `translations` | optional: the translation file for `$Key` labels (see [Translated labels](#translated-labels)) |
| `rules` | the list of rules |

Each rule:

| Key | Meaning |
|---|---|
| `id` | the rule's name: in the log, sent with the event, used by `SetRuleEnabled` |
| `target` | `"npc"` (only NPCs for now) |
| `requires` | optional: conditions that must **all** hold (see [Conditions](#conditions)) |
| `not` | optional: conditions of which **none** may hold (same keys as `requires`) |
| `primary` | `{ "label": ..., "event": ... }`: the label shown, and the SKSE mod event sent on activation. Without an `event`, only the label changes and activation stays vanilla |
| `alternate` | optional second action, used while the modifier key is held |
| `priority` | when several rules match, the highest wins (default 0) |
| `enabled` | optional, default `true`; `false` = off until a script turns it on (see [Papyrus API](#papyrus-api)) |

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

For anything the list doesn't cover, give the NPC a keyword, faction or token item from your own
plugin (with SPID, KID or a script) and require that.

**Optional mods:** if a listed form's plugin isn't installed, that entry is ignored (logged). A
`requires` condition left with no forms can never hold, so the rule is skipped. That makes it safe
to mention optional mods, especially in `not`.

**Mistakes are logged, not guessed at:** an unknown or misspelled key, a value of the wrong type, a
malformed or missing form, or a form of the wrong type skips the rule (or the file) with the reason
in `ModifierKeyFramework.log` (`Documents/My Games/Skyrim Special Edition/SKSE/`).

### When rules compete

Only one rule runs per NPC: the matching rule with the highest `priority`. On a tie, the rule from
the file whose name sorts first wins (alphabetical, ignoring case), then the one listed first in
that file. The result never depends on install order.

The log helps sort out overlaps between mods:

- at startup, every priority shared by rules from different files, in winning order;
- when the player activates an NPC that rules from other files also match, which rule was used and
  which were ignored (each pair once per session).

### Handling the action in Papyrus

Activating sends the action's `event` as an SKSE mod event. Register for it (mod event
registrations don't survive a save load, so register again on every load, e.g. from a player
alias's `OnPlayerLoadGame`):

```papyrus
RegisterForModEvent("MyMod_Action", "OnMyModAction")

Event OnMyModAction(String asEventName, String asRuleId, Float afIsAlternate, Form akTarget)
	Actor target = akTarget As Actor
	If afIsAlternate
		; the alternate action (modifier key held)
	Else
		; the primary action
	EndIf
EndEvent
```

| Parameter | Value |
|---|---|
| `asEventName` | the action's `event` |
| `asRuleId` | the rule's `id`, so one handler can serve several rules |
| `afIsAlternate` | `1.0` for the alternate action, `0.0` for the primary |
| `akTarget` | the NPC |

`primary` and `alternate` may use the same `event`, or different ones.

### Translated labels

A label can be a `$Key` instead of plain text. Keys are looked up in
`Interface/Translations/<name>_<LANGUAGE>.txt` (the usual UTF-16 LE file with a BOM, one
`$Key<TAB>Text` per line, as used by MCM Helper and SkyUI), where `<name>` is the rule file's name
without `.json`. To reuse an existing file, such as your MCM's, name it at the top of the rule file:

```jsonc
{
  "translations": "Death Timer - Immersive Bleedout",  // Interface/Translations/Death Timer - Immersive Bleedout_ENGLISH.txt
  "rules": [
    { "id": "downed", "primary": { "label": "$ANDR_KO_Prompt_GivePotion", "event": "DeathTimer_GivePotion" } }
  ]
}
```

A key that isn't found is shown as is (`$ANDR_KO_Prompt_GivePotion`), with a warning in the log.

### Papyrus API

Script `ModifierKeyFramework` (source in `Scripts/Source/ModifierKeyFramework.psc`):

| Function | Does |
|---|---|
| `Int GetVersion()` | the framework's version: major × 10000 + minor × 100 + patch (`10000` = 1.0.0) |
| `Bool IsModifierHeld()` | whether the modifier key (keyboard or gamepad) is held right now |
| `Bool SetRuleEnabled(String asFile, String asRuleId, Bool abEnabled)` | switches one of your rules on or off; `false` if there's no such rule |
| `Bool IsRuleEnabled(String asFile, String asRuleId)` | whether the rule is on |

- `asFile` is the rule file's name with or without `.json`; file and rule id are case-insensitive.
- A rule starts as its `"enabled"` value in the rule file (default `true`); ship it `false` to turn
  it on from a script.
- The switch is **not saved**: set it again on every game load, typically from your MCM setting in
  `OnPlayerLoadGame`, together with your `RegisterForModEvent` calls. A removed or updated mod
  never leaves stale state behind.
- The prompt updates at once when the NPC under the crosshair is affected.

To check that the framework is installed without depending on its script:
`SKSE.GetPluginVersion("ModifierKeyFramework") > 0`.

### Coming from Dynamic Activation Key

With DAK, a second action on an NPC is a perk: an *Activate* entry point on the player, conditioned
on DAK's global (`GetGlobalValue DynamicActivationKey == 1` while the key is held), a label, and a
script fragment, plus a way to give the perk to the player. The same with this framework is one
rule:

```jsonc
{
  "rules": [{
    "id": "search",
    "requires": { "keyword": "MyMod.esp|0x800", "alive": true },  // the perk's target conditions
    "primary":   { "label": "Talk" },                                  // no event: vanilla talk
    "alternate": { "label": "Search", "event": "MyMod_Search" },      // was: the DAK-conditioned perk entry
    "priority": 20
  }]
}
```

and the perk fragment's code moves into the `MyMod_Search` event handler.

| | Dynamic Activation Key | Modifier Key Framework |
|---|---|---|
| Setup | perk + entry points + conditions in the Creation Kit, perk distribution | a JSON file |
| Plugin | your mod's plugin must master DAK | none; no load order slot |
| Prompt shows the NPC's name | no: the action label replaces the whole prompt (`Search`) | yes: vanilla two lines (`Search` / `Lydia`) |
| Player sees a second action | no | yes: the `+` marker on the prompt |
| Two mods on the same NPC | perk entry priority, silent | priority, deterministic, logged |
| Conditions | the Creation Kit's full condition set | the list above; anything else through a keyword, faction or item |
| Targets | anything a perk entry point can target | NPCs (other objects: planned) |

## Settings

`SKSE/Plugins/ModifierKeyFramework.ini`: the modifier key (DirectX scan code, default Left Shift),
a gamepad button (default Left Shoulder), and the alternate-action marker (empty = no marker).

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

## Repository layout

```
mod/          the mod exactly as installed: DLL, ini, Papyrus script (+ source)
skse/         source of the SKSE plugin
schema/       JSON schema for rule files
examples/     example mod (rule file + Papyrus), not part of the release
```

## License

GPL-3.0-or-later (the plugin statically links CommonLibSSE-NG). See [skse/LICENSE](skse/LICENSE).
Mods that only use the framework through rule files and Papyrus are not affected by it.

## Credits

- **eXgamble**
- **powerofthree**: [Read Or Take SKSE](https://github.com/powerof3/ReadOrTakeBooks), whose prompt
  and modifier-key approach inspired this framework.
- The CommonLibSSE-NG maintainers and contributors.
