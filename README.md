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
      "primary":   { "label": "Give Potion", "event": "DeathTimer_GivePotion" },
      "alternate": { "label": "Search",      "event": "DeathTimer_Search" },
      "priority": 50
    }
  ]
}
```

| Field | Meaning |
|---|---|
| `target` | `"npc"` (only NPCs for now) |
| `requires.item` | the NPC must carry this item: `"Plugin.esp\|0xLocalID"` |
| `requires.alive` | `true` / `false` |
| `primary` | label shown, and the SKSE mod event sent on activation (the NPC is the event's sender) |
| `alternate` | optional second action, used while the modifier key is held |
| `priority` | when several rules match, the highest wins |

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
