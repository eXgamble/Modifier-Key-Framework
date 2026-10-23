# Modifier Key Framework

An SKSE framework for Skyrim Special Edition and Anniversary Edition (1.5.97, 1.6.x and 1.7.x; one DLL for all) that lets mods add their own
activation actions to NPCs and creatures, living or dead (bodies too), and (1.4.0) to books, items,
doors and containers, declared in simple JSON files, with one shared **modifier key** for a
second action. An alternative to perk-based activation and Dynamic Activation Key: no plugin, no
perk, no script needed just to show a prompt.

- The prompt keeps the vanilla look: the action on the first line, the target's name on the second.
- When a second action is available, the modifier key's button appears in front of the Activate
  button, the same images the HUD uses for its own buttons: `[LB] | [A] Give Potion` / `Lydia`
  (keyboard: `[Shift] | [E] Give Potion`).
  Holding the modifier key switches it to the second action: `[A] Search` / `Lydia`.
- One key for every mod that uses the framework (default Left Shift, Left Shoulder on a gamepad).
- Up to three actions on the modifier key: while it's held, the prompt shows one line per action,
  stacked like a controller's face buttons (Y on top, then X, then A, then the name). The buttons
  belong to positions, not to mods, so every mod looks and plays the same.

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
| `version` | optional: the rule format this file is written for (`1`, the default; `2`: optional `primary`, framework 1.1.0+; `3`: a list of `alternate` actions, framework 1.2.0+; `4`: dialogue topics, framework 1.3.0+; `5`: objects and `"do"` actions, framework 1.4.0+; `6`: `"info": "none"`, framework 1.5.0+; `7`: `"exclusive"`, framework 1.6.0+; `8`: the `drink` type, `playerHasWeapon` and `knockedDown`, framework 1.8.0+). A file written for a newer format than the installed framework reads is skipped with a message to update the framework |
| `translations` | optional: the translation file for `$Key` labels (see [Translated labels](#translated-labels)) |
| `rules` | the list of rules |

Each rule:

| Key | Meaning |
|---|---|
| `id` | the rule's name: in the log, sent with the event, used by `SetRuleEnabled` |
| `target` | `"npc"` (the default), or `"object"` (format `5`): books, items, doors and containers in the world (see [Objects](#objects)) |
| `requires` | optional: conditions that must **all** hold (see [Conditions](#conditions)) |
| `not` | optional: conditions of which **none** may hold (same keys as `requires`) |
| `primary` | `{ "label": ..., "event": ... }`: the label shown, and the SKSE mod event sent on activation. Without an `event`, only the label changes and activation stays vanilla. Without a `label`, the prompt keeps the game's own text (`Search` on a dead body, `Talk`, in the player's language). Format `2`: `primary` itself is optional, `"primary": {}` or leaving it out keeps the activation fully vanilla |
| `alternate` | optional action used while the modifier key is held; needs a `label`. Format `3`: a list of up to 3 actions, in order of importance (see [More than one modifier action](#more-than-one-modifier-action)) |

An action can pick a dialogue topic instead of sending an event (format `4`, see
[Dialogue topics](#dialogue-topics)), or do one of the game's own things (format `5`, see
[Objects](#objects)).
| `priority` | when several rules match, the highest one's primary is used and its modifier actions come first (default 0; see [When rules meet](#when-rules-meet)) |
| `exclusive` | optional, format `7`: `true` keeps the target to this rule alone, no other mod's actions merged in (see [When rules meet](#when-rules-meet)) |
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
| `knockedDown` | is knocked down, ragdolled or paralyzed (format `8`): e.g. thrown by a shout, paralyzed by a spell, or downed by a mod that doesn't use the game's bleedout |
| `topic` | offers the dialogue topic right now (format `4`): its quest runs and one of its lines passes its conditions, e.g. a merchant whose shop is open (see [Dialogue topics](#dialogue-topics)) |

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

### When rules meet

Since 1.6.0 rules from several mods share a target instead of one taking it over:

- **Activate** (the primary) comes from the matching rule with the highest `priority`.
- **The modifier actions** of every matching rule go onto the buttons together, highest priority
  first, each rule's own in its order: up to 3 (A, X, Y). A merchant that eXrim can pickpocket and
  Merchants and Trainers can trade with gets `[A] Buy / Sell` and `[X] Pickpocket`. An action whose
  label is already there shows once; actions past the third are left out.
- Each action still sends its own mod's event with its own number (`afAction`: its place in its own
  rule), whichever button it lands on, so scripts don't change.
- `"exclusive": true` (format `7`) keeps a target to one rule: on top, only its actions show; below
  another rule, it's left out. For a rule whose actions make no sense next to others (a moment where
  only that mod's choice should be on screen).

On a tie in `priority`, the rule from the file whose name sorts first comes first (alphabetical,
ignoring case), then the one listed first in that file. The result never depends on install order.

The log helps sort out overlaps between mods:

- at startup, every priority shared by rules from different files, in order;
- when the player activates a target several rules match, which rule gave Activate, which action
  landed on which button, and what was left out and why (once per combination of rules per session).

### Handling the action in Papyrus

Activating sends the action's `event` as an SKSE mod event. Register for it (mod event
registrations don't survive a save load, so register again on every load, e.g. from a player
alias's `OnPlayerLoadGame`):

```papyrus
RegisterForModEvent("MyMod_Action", "OnMyModAction")

Event OnMyModAction(String asEventName, String asRuleId, Float afAction, Form akTarget)
	Actor target = akTarget As Actor
	If afAction == 0.0
		; the primary action
	ElseIf afAction == 1.0
		; the (first) modifier action: Activate with the modifier key held
	ElseIf afAction == 2.0
		; the second modifier action (X)
	Else
		; the third modifier action (Y)
	EndIf
EndEvent
```

| Parameter | Value |
|---|---|
| `asEventName` | the action's `event` |
| `asRuleId` | the rule's `id`, so one handler can serve several rules |
| `afAction` | which action: `0.0` the primary, `1.0` to `3.0` the modifier actions in order (`1.0` is the single alternate, as before 1.2.0, so `If afIsAlternate` scripts keep working) |
| `akTarget` | the NPC |

All actions may use the same `event`, or different ones.

The framework only takes the player's own Activate presses (1.6.1+). When your handler activates the
target itself, e.g. `akTarget.Activate(Game.GetPlayer())` to start a conversation after setting
something up, the game's own activation runs, even while the modifier key is still held.

### More than one modifier action

`"alternate"` can be a list of up to three actions (rule file `"version": 3`). Your mod only lists
them in order of importance; the framework gives out the buttons by position, the same for every mod:

| Actions | Buttons while the modifier key is held |
|---|---|
| 1 | Activate (A / E) |
| 2 | Activate, X |
| 3 | Activate, X, Y |

X and Y are the game controls in the framework's ini (default Ready Weapon and Jump: X and Y on a
controller, R and Space on a keyboard), so the player's own key bindings apply. Without the modifier
key the prompt looks as always (`[LB] | [A] Search` / `Deer`); while it's held, the actions stack up like a
controller's face buttons:

```
 [Y]  Skin Animal
 [X]  Harvest Meat
 [A]  Pick Up Carcass
      Deer
```

```jsonc
{
  "version": 3,
  "rules": [{
    "id": "carcass",
    "requires": { "alive": false, "keyword": "Skyrim.esm|0x13798" },  // dead animals
    "primary": {},                                                    // Search, as vanilla
    "alternate": [
      { "label": "Pick Up Carcass", "event": "MyMod_Action" },        // Activate
      { "label": "Harvest Meat",    "event": "MyMod_Action" },        // X
      { "label": "Skin Animal",     "event": "MyMod_Action" }         // Y
    ]
  }]
}
```

The X and Y controls do their normal job (drawing the weapon, jumping) everywhere else: the framework
only takes them while the modifier key is held over a target with that many actions. With several
mods on one target, their actions share the buttons (see [When rules meet](#when-rules-meet)).

### Objects

Format `5` (framework 1.4.0+). With `"target": "object"` a rule is for things in the world instead of
NPCs: books, weapons, armor, potions, food, poisons, ingredients, scrolls, torches, ammo, misc items,
keys, soul gems, doors and containers. Object rules use their own conditions:

| Condition | Holds when the object… |
|---|---|
| `type` | is one of these kinds: `book`, `weapon`, `armor`, `potion`, `food`, `poison`, `ingredient`, `scroll`, `torch`, `ammo`, `misc`, `key`, `soulGem`, `door`, `container`, `drink` (one or a list). A `drink` (format `8`) is food that is drunk, not eaten (ale, mead, wine, milk: the potion sound when used); it is `food` too, so "food but not drinks" is `"requires": { "type": "food" }, "not": { "type": "drink" }` |
| `base` | is one of these base objects (`"Plugin.esp\|0xID"`) |
| `keyword` | has the keyword |
| `read` | is a book the player has read |
| `spellTome` | is a spell tome |
| `takeable` | can be picked up (not a book flagged can't-take, not a door or container) |
| `owned` | taking it is a crime |
| `locked` | is locked |
| `requiresKey` | is locked and can only be opened with its key |
| `playerHasKey` | is locked and the player carries its key |
| `destinationKeyword` | is a door whose other side is in a location with this keyword (e.g. `LocTypeHouse`) |
| `playerSneaking` | the player is sneaking (NPC rules can use this one too) |
| `interior` | is in an interior cell (NPC rules can use this one too) |
| `playerHasWeapon` | the player holds a weapon in either hand, one a poison can go on (a staff doesn't count; format `8`, NPC rules can use this one too) |

Instead of an `event`, an action can `"do"` one of the game's own things:

| `do` | What happens | Label if none is given |
|---|---|---|
| `activate` | the vanilla activation (Read, Open, Unlock, Talk, ...) | the game's own |
| `take` | picked up the game's way (a crime where the game says so) | the game's `Take` / red `Steal` |
| `equip` | picked up and equipped (ammo: the whole stack) | needs a label |
| `equipAndDraw` | picked up, equipped and drawn | needs a label |
| `use` | picked up and used: drunk, eaten, applied (things that can't be used are equipped) | needs a label |

When taking would be a crime, the labels of `take`, `equip`, `equipAndDraw` and `use` show red, like
the game's own `Steal`. NPC rules can only `"do": "activate"`.

An action can also say what the prompt's last line shows while it's the action on Activate: `"info":
"relationship"` puts how the people behind the target feel about the player (Lover, Ally, Confidant, Friend,
Acquaintance, Rival, Foe, Enemy, Archnemesis) where the game shows a lock's level. That's the NPC itself; for a
door, whoever owns the place it leads to (for a household, the friendliest grown-up); for anything else, its owner.
Nobody (no owner, or they're all dead): the game's own line stays. The rank names can be translated in
`Interface/Translations/ModifierKeyFramework_<LANGUAGE>.txt`. Cost: on an NPC that's a single look-up; for a door
or an object the framework finds the people behind it by going through every actor in the game, about 1 ms for a
household and 3 ms the first time after a load (measured with ~3,300 actors on a fast PC; the result is then kept
10 s per owner), so weigh it on big lists and slower PCs.

The nine rank names say more than most mods do with them, so a rule can use its own words instead: `"infoLabels"`
lists a word per rank range, and the first entry whose `fromRank` the rank reaches is shown (4 Lover, 3 Ally,
2 Confidant, 1 Friend, 0 Acquaintance, -1 Rival, -2 Foe, -3 Enemy, -4 Archnemesis; an entry without `fromRank`
covers every rank). A rank below every entry keeps the game's own line.

```jsonc
"alternate": {
  "label": "Knock", "event": "MySimplyKnock_Knock",
  "info": "relationship",
  "infoLabels": [
    { "fromRank": 1, "label": "Friend" },   // Friend and better
    { "label": "Unwelcome" }                // everyone else
  ]
}
```

Format `6` (framework 1.5.0+): `"info": "none"` shows nothing below the name instead, e.g. a Knock without the
lock's level: `{ "label": "Knock", "event": "MySimplyKnock_Knock", "info": "none" }`.

```jsonc
{
  "version": 5,
  "rules": [{
    "id": "weapons",
    "target": "object",
    "requires": { "type": "weapon" },
    "primary": {},                                              // Take / Steal, as vanilla
    "alternate": [
      { "label": "Equip",          "do": "equip" },             // Activate
      { "label": "Equip and Draw", "do": "equipAndDraw" }       // X
    ]
  }]
}
```

Events work on objects too: `akTarget` is the object's reference.

### Dialogue topics

Format `4` (framework 1.3.0+). An action can name dialogue topics (`DIAL` records) instead of an
event: activating starts the real conversation and picks the first listed topic the NPC offers, as
if the player had chosen it. The NPC turns to the player, says the topic's line and does what that
line does (open the barter menu, the training menu, ...), so nothing is skipped and the NPC stays in
the conversation.

With `"endDialogue": true` the player never sees the dialogue menu (1.7.0+): no topic list, no
mouse cursor, only the line's subtitle when subtitles are on. When the menu the topic opened
closes, the conversation closes the way the player's Tab does, so the NPC says goodbye. Without
`endDialogue` the dialogue menu shows as usual and the player stays in the conversation, with its
topic list, once the topic's menu closes. If the topic never turns up, or its line opens no menu,
the conversation shows and the player goes on by hand.

```jsonc
{
  "version": 4,
  "rules": [{
    "id": "merchants",
    "requires": { "topic": "Skyrim.esm|0x7F6BB" },   // offers "What have you got for sale?"
    "alternate": { "label": "Buy / Sell", "topic": "Skyrim.esm|0x7F6BB", "endDialogue": true }
  }]
}
```

- An action has an `event` or a `topic`, not both.
- A modifier action with topics shows only while the NPC offers one of them; the actions after it
  move up a button. A shop that is closed for the night drops its Buy / Sell, and an NPC who both
  sells and trains gets both actions while both are on offer. The `afAction` number an event action
  sends stays its place in the list, whichever button it lands on.
- The `topic` condition answers the same question for the whole rule.
- MKF - Merchants and Trainers (its own mod) is a
  complete example; MKF - Read Or Take and MKF - Use Or Take are examples for objects.

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
- The switch is **not saved**: every new game and every load starts from the rule file's `"enabled"` value, so set
  it again on every game load, typically from your MCM setting in
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
  "version": 2,
  "rules": [{
    "id": "search",
    "requires": { "keyword": "MyMod.esp|0x800", "alive": true },  // the perk's target conditions
    "primary":   {},                                                   // vanilla talk, vanilla label (format 2)
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
| Player sees a second action | no | yes: the modifier key's button on the prompt |
| Two mods on the same NPC | perk entry priority, silent | both mods' actions on the buttons, ordered by priority, logged |
| Conditions | the Creation Kit's full condition set | the list above; anything else through a keyword, faction or item |
| Targets | anything a perk entry point can target | NPCs and creatures; books, items, doors and containers (1.4.0) |

## Settings

`SKSE/Plugins/ModifierKeyFramework.ini`: the modifier key (DirectX scan code, default Left Shift),
a gamepad button (default Left Shoulder), whether the prompt shows the modifier key's button
(`bModifierIcon`, default on, with `sModifierSeparator`, default `|`) or a text marker
(`sAlternateMarker`, default `+`, also the fallback for a HUD without that button's image), and the
game controls for the second and third modifier action (`sSlot2Control`, default Ready Weapon;
`sSlot3Control`, default Jump; also possible: Sneak, Shout, Auto-Move, Toggle Always Run).

## Compatibility

- **HUD and UI mods:** the buttons are drawn into the game's own HUD at runtime (the HUD's
  `SetCrosshairTarget` and `RefreshActivateButtonArt` are wrapped, no swf is replaced), so any HUD
  that keeps the vanilla activation prompt works; tested with the vanilla HUD, SkyHUD (default
  settings) and Oathvein UI. The modifier key's button is placed from what the skin draws, with the
  skin's own gap, so it stays beside the Activate button wherever a HUD puts it (left of the label, or
  on a row of its own). The images are the HUD's own (`360_X.png`, `R.png`, ...) and follow its
  keyboard/controller switch. A HUD without an image for a button, or a prompt MKF can't measure,
  gets the text marker instead.
- **Controller layouts with LB combos** (e.g. Kontrol): actions they put on Left Shoulder + X or Y
  still happen alongside the second and third modifier action. Move them, or pick another modifier.
- **Dynamic Activation Key:** give it a different key. With the same key, its perk-based action acts
  before the NPC's own activation and wins.
- **QuickLoot-style mods** take over activating dead bodies and containers, so actions on dead NPCs
  may not be reachable while their window shows.
- **Read Or Take / Use Or Take (powerofthree):** fine with mods that only use NPC rules. MKF - Read
  Or Take and MKF - Use Or Take do the same job on this framework; don't use both versions together.

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

GPL-3.0-or-later (the plugin statically links CommonLibSSE-NG). See [LICENSE](LICENSE).
Mods that only use the framework through rule files and Papyrus are not affected by it.

## Credits

- **eXgamble**
- **powerofthree**: [Read Or Take SKSE](https://github.com/powerof3/ReadOrTakeBooks), whose prompt
  and modifier-key approach inspired this framework.
- The CommonLibSSE-NG maintainers and contributors.
