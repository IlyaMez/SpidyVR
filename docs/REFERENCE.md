# Swinging reference and game research

## Blaze's Spider-Man

Reference: the user-supplied `Spiderman Pack (Acrobatics 1.2.1 beta)` ZIP, and the
[author's mod page](https://www.nexusmods.com/bladeandsorcery/mods/12564).
The archive was read in place. Its manifest credits Blazeventura and ANTIOPSVR;
the spell configuration points to `BlazesSpiderManSpell.SpiderManSpell`.
Compiled metadata exposes web-distance, yank-speed, yank-distance, zip-strength,
point-launch, and optional acrobatics settings.

The design cues used here are separately aimed hand webs, physical pulls whose
strength affects zipping, a short landing-to-jump window, and deliberate gesture
thresholds. These were independently implemented in C++. The reference's Unity
assemblies, assets, sounds, textures, and source implementations are not included
in Spidy. We have not watched or replicated the exact acrobatics animation.

## Spider-Man executable

Locally checked on 2026-10-03:

- Steam app 1817070, build 23986256.
- `Spider-Man.exe` file version 4.0630.0.0, size 121,901,008 bytes.
- SHA-256 `e297d4d94f1ffe4febf289745e79e7b6fa233a788e7a00f480fc77c55db81ad1`.

Steam's branches, from Steam's app info on 2026-10-08 (api.steamcmd.net):

- `public`: build 23986256, built June 30, 2026 and published July 8, 2026;
  its depot manifests (1817071 `3826860471854644257`, 1817072
  `2734256798601145338`, 1817074 `2767115330843299188`) match this PC's
  install. `Spider-Man.exe`'s version resource reads 4.630.0.0 and the game
  logs `Build: v4.630.0.0` (`tools/inspect_game.py` `EXPECTED_VERSION`).
- `previous_version` ("Rollback to previous version"): build 12423814, built
  October 12, 2023.
- `previous_version2` ("Rollback to v1.1212.0.0"): build 10131361, built
  December 12, 2022.
- Language depots carry no `Spider-Man.exe`: six of them keep one manifest on
  all three branches while the executable differs between them. A Steam copy
  on `public`, fully updated, is therefore this exact file.

Public leads came from [ArkWeb](https://github.com/luki-1/ArkWeb/tree/17ec697bd431fce96a60fff1075f7e399d296058),
specifically its `recon/PHASE0.md`, `src/sm_guest/main.cpp`, and `combat.h`.
The source checkout is research material under ignored `.research/`; none of
its implementation is compiled into Spidy.

The local PE inspection confirmed these byte/address facts:

| Candidate | RVA | Verification |
|---|---:|---|
| Camera-related update | `0x897d30` | 16 entry bytes match; live `this` is an AimContextSwing object |
| Havok world ray query | `0x2e67010` | 15 entry bytes match; six live read-only rays returned city surfaces |
| Native ray worker | `0x1818070` | `CastRayVsWorld` vtable `0x3d0a9c8`; live query return `0x18180fe` |
| Havok pre-collision step | `0x2e54300` | 16 entry bytes match in executable section |
| HeroLocal vtable | `0x38a93c8` | RTTI resolves to `Hero::HeroLocal` |
| HeroCameraManager vtable | `0x38b1dd0` | RTTI resolves to `Hero::HeroCameraManager` |
| Observed update-object vtable | `0x38b1d08` | RTTI resolves to `Hero::AimContextSwing` |
| Component registry resolver | `0x16798f0` | Live lookup validates slot and object handle generations |
| MoverStandard vtable | `0x4f70168` | Local ownership verified through HeroMoverManager + `0xdb4` |
| Native displacement request | `0x1fc2e10` | One airborne 4 cm request changed actual player position |
| Native pre-query movement | `0x1fbe360` | 610 live steps; 22 accepted a bounded velocity command |
| Native gravity update | `0x1fbda50` | Scoped target correction during those same 22 steps |
| Native requested-target setter | `0x1fb88d0` | Decoded finite checks and request-ready bookkeeping |
| Component timestep | `0x16769f0` | Global game delta multiplied by component time scale |

For the player's body and punching (October 6; [BODY.md](BODY.md)), checked
in this executable and measured in the running game:

| Fact | RVA | Verification |
|---|---:|---|
| Pose writer (out joints, pose job) | `0x1601290` | 20 entry bytes match; returns a float its callers read; one hero call per render frame |
| Local-to-model joint conversion (out, local, joints, count) | `0x1600770` | Builds the hero's rest pose; parents before children; roots read the matrix before the first joint |
| Rig: count +2, joints +8 (16 B: parent, hash +8, name +0xc), rest pose +0x18 | job `+0x28` | 237 joints, every name's hash matches |
| Name hash: CRC-32 table steps seeded with `0xedb88320`, no final inversion | `0x1bb88d0` | Table at `0x4f232f0` is the standard reflected one |
| DamageSystem (static object; vtable `0x4f5db58`) | `0x62a4ec0` | Its vtable is stored in the image |
| Direct damage request (system, target, direction, point, normal) | `0x1eb6d60` | A bot's and the hero's health fell by the requested amount |
| Request processing; drops an unresolved Damager | `0x1eb8cf0` | Disassembly |
| Health::OnDamage; hit points at Health `+0x88` | `0x1f15a10` | Hit points read before and after each blow |
| DamageRequest fields and Knockback / DamageType names | reflection at `0x6a13090`, enum names at `0x5e08560`, `0x5e07b50` | Field offsets, docs and value names read from the image |

ArkWeb's `mirror.h` was the lead for the pose writer and the hero's joint
array at the instance's `+0xd8`; Spidy reads the joint tree and names from the
rig itself and shares no code with it.

For the web shooter (October 7; `include/spidy/game_shooter.hpp`), found from
RTTI and traced in the running game with a research DLL while the game fired
its own gadget, then fired by Spidy:

| Fact | RVA | Verification |
|---|---:|---|
| `WeaponWebShooter` (0x6b8 bytes), a component of the hero's weapon actor | vtable `0x391c950` | One registered in free roam, 0.5 m from the hero |
| Fire event (weapon, event): stores its aim at +0x670, fires | `0xe3bd80` (vtable +0x108) | Called on the main thread by `HeroWeaponStateFiringLocal`; Spidy's events through it spawned shots |
| The web shooter's own part of an event (+0x3c, +0x40) | `0xe55310` (vtable +0x100) | Disassembly; fills from +0x6ac, +0x6a8 |
| Fire handler, index +0xa, kind +0xb | `0x2150460` | Disassembly; kind 1 calls SpawnShot |
| SpawnShot (weapon, index, event) | `0x2150830` (vtable +0x110) | Traced; spawns through +0x2f8 (`0x215c190`), shot handle at +0x518 + 4 * index |
| Muzzle (weapon, out matrix, index): emitter at +0x1b8 + 0x38 * index | `0x2150c40` (vtable +0x160) | Index 0 the right wrist, 1 the left; overridden, the shot started at the given point |
| New shot id from the shot table at `0x656d9f0` | `0x215f040` | Disassembly; the id indexes a ring of 1024 slots a player |
| Register a shot id, despawning an older slot's shots | `0x215f840` | Disassembly |
| Actor reference from an actor record | `0x1f7b8e0` | Disassembly; 0 for a pedestrian, measured |
| Actor handle to record (actor table `[7a44380]`, 0xc0 a record) | `0x15a0560` | Disassembly |
| Camera manager's update, once a frame on the main thread | `0x897d30` | Spidy fires its shots after it; 1,692 calls during the probe |
| `ShotWebShooter`, the shot | vtable `0x3907d30` | Flew 52-61 m/s to its aim point, ended there, at a surface or after 1 s |

For what web balls and web pulls do to thugs (October 8;
`include/spidy/game_shooter.hpp`, `include/spidy/game_grab.hpp`,
`include/spidy/native_bodies.hpp`), found from RTTI, reflection and
disassembly, then traced and tried at a street crime with a research DLL:

| Fact | RVA | Verification |
|---|---:|---|
| `ShotWebShooter` event handler (shot, name hash, event); Collision `0xd930bcb2` | `0xd2a570` (vtable `0x3907d30` +0xd8) | Hooked: Spidy's shots collided with their target thug |
| Collision fields HitActor `0x21eb297b` (8), HitPosition `0x568973e3` (12), HitNormal `0xc3272bc7` (12): index by name hash, read, actor reference to record | `0x1bcf3e0`, `0x1f9db60`, `0x1f7b760` | Decoded the struck thug's and trash can's records |
| A shot's id and registry handle | ShotWebShooter +0x104, +0x14 | Equal to the fired id and the weapon's shot handle |
| `ShotActionDamage::Apply`, its collision entry (skips an action with no damage, knockback or custom data) | `0x20b7200`, `0x20b6d70` | Never called for Spidy's shots (all-zero DamageData); called for the game's Impact Web |
| Web shooter options from the hero's melee web shots (`HeroAnimWebShooterFireEvent`: FireFromRightHand, TargetLimb, ShotEffect) set at weapon +0x6ac/+0x6a8, filled into the event +0x3c/+0x40 | `0xe55800`, `0xe55310` | Options 3 + limb 0 made a hit spawn a `WebBlanket` (`0xd4ea30`) |
| A request's status entry (request, amount, type, duration, count) | `0x1ed20e0` | kWebImpact + type 19 at 25-100 webbed thugs (BotStateWebStruggle) |
| `StatusEffectTrackerWebbed`: amount +0x50, threshold +0x54 (30 for street thugs), adds webbing | vtable `0x390bf28`, `0xd5b860` | Read during the hits; decays to 0 within seconds |
| DamageType names (0 kNone ... 7 kKinetic ... 19 kWebEncase, 20 kWebImpact ...) | names at `0x5e07b50` | Read from the image |
| `BotStateFlungLocal`, the flight's velocity +0x94 | vtable `0x386c448`; machine +0x98 | Written every frame, it steered the flight |
| Thug, civilian, breakable components | `ThugBot` `0x384b010`, `CivilianBot` `0x383c480`, `BreakableSystemComponent` `0x3871018` | Registry scans at the crime and near the save |
| Bot mover managers, all derived from `BotMoverManager`: `BotMoverManagerGame`, `HammerheadBotMoverManager` and `MechaHammerheadMoverManager` (from it), `HoverMoverManager`, `DocOckMoverManager` (from it) | vtables `0x38533e0`, `0x3853558`, `0x385c618`, `0x384ce18`, `0x384cfa0` | RTTI class hierarchies (October 9); `BotMoverManagerGame` bots scanned in the game, no hover bot seen yet |
| `MoverManager`'s mover handle, +0xdb4, for the hero's and every bot's mover manager | read by `0x1fb9c60`, `0x1fb9cb0`, `0x1fb9d60`, `0x1fb9dd0`, `0x1fba3d0` | Disassembly |
| `MoverManager`'s live body, a `MoverBodySize` at +0xdb8: lower and upper sphere centres above the transform +0xdc0, +0xdc4, radius +0xdc8, the upper one scaled by +0xdf8; the mover's capsule from them | vtable `0x500e870`; capsule `0x1fbba90` | Read in the game: Fisk thugs 0.85, 1.15, 0.45 (0.4 m to 1.6 m), the hero 0.86, 1.3, 0.4 |
| `MoverConfig` (0x78 bytes) with a `MoverBodySize` at +0x58 (`BodyBottom`, `BodyTop`, `BodyRadius`) | vtable `0x500e950`; constructor `0x22c9300` | A thug's mover manager names his at +0x48: the same values |
| MSVC x64 RTTI: complete object locator at vtable -8 (signature 1, offset, type descriptor, class hierarchy, its own image offset); the hierarchy's base count +8 and base array +0xc; each base names its type descriptor first, whose name is at +0x10 | — | Spidy's scan in the game counted the same 74 props and bots as the same rules read from the executable |
| `Team` component (red and blue teams) | vtable `0x4f62ec0` | No actor had one in the running game: no hostility test there |
| Bot states in which something else moves the bot: `BotStateAttachToActor`, `BotStateCarried`, `BotStateNull` (with `BotStatePlayCinematic` and its Game version), `BotStatePlaySyncedAnimGuest` (and Game), `BotStateSlottedInteract`, `BotStateFrozen`, `BotStateRequestTakedownYank`; riding: `Ridable` (seats kDriver, kPassenger), `RidableRider`, `VehicleAttach`, `CarTakedownManager2`, `CarWebYankTarget` | layer-0 state at the state machine's +0x70 | RTTI names read from the exe (October 18); which of them a car's passengers and the hurt officers of a crime are in is not yet seen in the running game. `game_targets::held` takes the first list for "not free to fight" |
| `SimpleAllegiance` (kNeutral, kBlueTeam = the hero, kPolice, kFisk, kMaggia, kSableEnemy ...) and `Target::TargetData` (kMeleeTarget, kWebLineTarget ... a mask) | vtables `0x4f89dc0`, `0x4f89f28`; names at `0x39470d8`, `0x391ed74` | RTTI and strings only (October 18); field offsets unknown |
| A record's component of a type: through its table at +0x80 when it has one (size at +0x88), else its list at +0x58; a type holds its name at +0x60 | `0x16799a0`, `0x167ab40` | Disassembly; the game's own lookups (`0x3437f0`) |
| `BotAttackResponseManager` (type `0x6c74b40`): a damage modifier in the bot's `CommonComponentStash`; its response to a request's damager reads the bot's `AwarenessDB` (type `0x7bf4e30`) through the handle at +0xa4 unchecked | vtable `0x3836d88`; slot 11 `0x313420`, `0x314c70`, set-up `0x313150` | The October 11 minidump: a null read at `0x314d3e` for a kinetic blow with the hero as damager; 9 such blows sent without a damager in the next session, no crash |

For the SPIDY VR tab in the game's Settings (October 7;
`include/spidy/game_menu.hpp`), found from the executable's strings, RTTI and
reflection tables, then traced in the running game with a research DLL that
logged the Settings' Flash calls and callbacks and added a test tab. The
Settings are Scaleform (AS3) screens fed from `configs/uiconfig/uisystemmenu.config`:

| Fact | RVA | Verification |
|---|---:|---|
| Settings system (`UISettingsMenuSystem`, static); +0x20 its config (`UISystemMenuConfig`) | `0x5d96dd0` | The config's tabs at +0x168 (0x40 bytes each), +0x170 counts them: 9, ids 2-10 |
| Per-setting records, 0x28 bytes from +0x420, 123 of them (+0x1758 follows) | settings 0-122 | Getters index them unchecked; set (`0x730ee0`) and reset (`0x730ae0`) skip numbers above 0x7a |
| Tab (`UISystemMenu`, vtable `0x3a7da68`): +8 id, +0x10 name tag (pointer, length, hash), +0x20 heading tag, +0x30 items, +0x38 count | 0x40 bytes | Read from the GAME tab (id 4, 15 items) |
| Item (`UISystemMenuItem`, vtable `0x3a7d988`): +8 setting, +0x10 title tag, +0x20 help tag, +0x48 option type, +0x70 choices (0x50 bytes; +0x8 name tag, +0x18 help tag, +0x28 preview), +0x78 count | 0x88 bytes | No choices: the game's OFF/ON list at `0x5d9a310` |
| Option types: setting, heading | vtables `0x3a7d6e8`, `0x3a7d838` | RTTI |
| Builds every tab, calls Flash's `OpenOptions` (tabs array, flag) | `0x7dd9f0` | Logged through the UI's call wrapper (`0x1d1cf30`, name literal at `0x389f4c0`) |
| Builds one tab's Flash object (out value, movie, tab) | `0x807740` | Built a tab of copied items; its rows, ids above 0x200, drew and changed |
| A row (`0x80a220`, `0x809e20`), a setting's choices and value (`0x80b950`) | | Disassembly; per-setting cases only for 4-122 |
| The UI movie Flash objects are made in | `0x1d29690` | Disassembly |
| A setting's kind (0 list, 1-2 slider, 3 colour, 4 command, 5 heading) / value / changeable / default / default choice | `0x72d640` / `0x72d650` / `0x72e1d0` / `0x72d5f0` / `0x72d610` | Disassembly; hooked for numbers 0x200-0x20f |
| The pause menu's Settings callbacks (ui, name hash, args, count) | `0x7dc400` | Logged: `UpdateOption` (setting, value), `ResetOption`, `ResetAllOptionsForCurrentOptionsMenu` (tab id; ignored above 10, hash at `0x6d8ed88`) |
| Text by hash (hash, fallback); whether there is one | `0x1749970`, `0x1749ae0` | Hashes are CRC-32 of tags (`0x1749a20` hashes a tag string) |
| The pause screen's vtable: frame update `0x7dbdc0` (slot at `0x389f2b8`), open `0x7dbc80` (`0x389f2c0`, ends `jmp 0x7dd7c0`, the first page), callbacks `0x7dc400` (`0x389f2d0`) | vtable around `0x389f2b8` | Read from the exe (November 22) |
| The update's Settings countdown: screen +0x174 above 0 is counted down a frame at a time, at 0 the Settings are built (`0x7dd9f0`) and it goes to -1; ApplyWithheldChangedDisplaySettings and RevertPreAppliedChanges set it to 2, the open sets -1 | `0x7dbe87` (`8b 87 74 01 00 00 85 c0 78 37 75 2f`) | In the game, November 22: set from the first page it fills the Settings, but Flash stays on the page (its own move to the Settings comes only with A on SETTINGS); not used |
| `Select(n)` (Flash's callback for the first page's item n): a jump table over 0-0x1d; 5 calls `0x7dd9f0` (SETTINGS), 0 calls `0x7dd270` (RESUME: Flash's `CloseMenu`) | `0x7de150` (table bytes `0x7de3c8`, targets `0x7de39c`) | Disassembly |
| Callback name hashes, set at start-up by `0x1d297a0` (name): `close` `0x6d8ed40`, `Select` `0x6d8ed44`, `UpdateOption` `0x6d8ed74`, `CloseOptions` `0x6d8ed7c` (the native side then invokes Flash's `CloseOptions`), `ResetAllOptionsForCurrentOptionsMenu` `0x6d8ed88`, `onReturnToRootMenu` `0x6d8ed94`; 32 branches in `0x7dc400` | | Each static initializer's string and global paired (`lea rcx, name; call 0x1d297a0; mov [global], eax`) |
| Scaleform's `GFx::Value::ObjectInterface` vtable: 2 ObjectRelease, 10 GetArraySize, 11 SetArraySize, 15 PushBack | | 2 and 15 used since October 7; 10 and 11 by the first-time setup (it empties the Settings' tab list; in the game, November 22) |
| The title screen's Settings (`GUI::LobbyOptionsUI` in `LobbyShellUI`, `ui/export/LobbyShell.gfx`): one screen, its data set as Flash members (`7ab080`: lobbyData, saveData, optionsData, difficultyData); a page's items built by `7aa9d0` (and listed again by `7ad940`) into +0x4d8 (8 bytes each, at most 0x40, count +0x6d8), page +0x4c8 (1 the root), cursor +0x4cc, the way back {page, cursor} from +0x484 with its top's index at +0x4c4 | `0x7aa9d0`, `0x7ad940` | Disassembly; in the game: SPIDY VR SETUP's page built and changed there (November 22) |
| The root (page 1): the platform's menu (config +0x88, +0xc0 or +0xf8: START, SETTINGS heading, PSN) then config +0x110 (12 entries here: TEXT LANGUAGE, ENGLISH VO and 0x4b by reference, then GAME 0x33, SUBTITLES 0x1b, AUDIO 0x34, DISPLAY 0x35, ACCESSIBILITY 0x36, GAMEPAD 0x65, MOUSE 0x66, KEY BINDING 0x64, CONNECTIONS 0x74). A menu: +8 entries (0x18 bytes: +8 the item or a reference {setting, tab}, +0x10 a type whose +8 is 0xa37411c7 for an item, 0xd57865b3 for a reference), +0x10 count. A page's entry: option type `0x3a7d7c8`, its page id at option +8 (GAME 4, SUBTITLES 6, AUDIO 3, DISPLAY 2, ACCESSIBILITY 5, GAMEPAD 9, MOUSE 8, KEY BINDING 7, CONNECTIONS 10) | config +0x110 | Read in the game |
| Rows on a title page need item +0x82 set; a setting number past 0x7a is visible (`80cdd0`'s default). The page's title is its tab's heading (+0x2c hash) | `0x7aad53` | Disassembly |
| The title's callbacks (`7a8980`, as `7dc400`): OptionsAccept (a page entry: the LAST item of the page with the setting number Flash sends, its option's page id; the way back pushed), OptionsBack (pop), OnOptionsClose (`0x6d88414`, B on the first level: a refresh, the page left as it is), optionsClosed, continue, loadGame, newGame, firstTimeBootDone, adjustBrightness, onPressStartScreen and 26 more | `0x7a8980` | Names from the start-up hash pairs |
| The title's first-run flow: `7aca10` sets Flash's firstTimeBoot from the player's preferences (`ce6840` of `19c2600(&7d80ea8)`+0x34c) +0xed == 0, and bootSequence, lastPlayedSlot, firstTimeLoad (screen +0x16a: 1 from the constructor `7a7f10` until the first load), startLocked. With +0xed cleared the game showed, after the Marvel intro, Sony's GAME DATA consent (FULL, LEARN MORE, LIMITED) over SELECT PROFILE (userprefs keys BootFlowShown, StartupGammaShown, AcceptedSIEAnalytics) | `0x7aca10` | In the game, November 22 (the byte put back from the save's backup) |
| The main menu (CONTINUE, LOAD GAME, SETTINGS, VIEW CREDITS, CONNECT TO PSN, QUIT GAME) is Flash's own list (`lobby.menus.TitleMenu`, `Title.gfx` in `g00s019`); up wraps from the top. The game lists it from the lobby config (`7aa7a0`: `[5d96dd0+0x20]`+0x28, items at +0x30 of 0x88 bytes, count +0x38, shown when `80cdc0`; id at +8, SETTINGS 58). TitleMenu's public `OnMouseEvent(MouseEvent, index, 0)` with a CLICK selects that item and presses it (the base `Menu.OnMouseEvent` needs `mouseChildren`); `GetMode` (1 lobby, 6 Settings), `GetAnimating`, `GetAllowOptionInput`, `GetOpeningSave` say whether it takes input. The title screen's update `7a8320` (its vtable `389b0c0` +0x18) runs every frame it shows; its host's movie is the UI movie (`1d1eee0` jumps to `1d29690`), whose `CreateObject` is `319f7d0` (vtable +0x170). Flash's OnSaveDataLoaded (`7ad240`, name `0x389b348`) as a profile's main menu comes up. The first press of a pad after keyboard input only switches the game's prompts to the pad | | In the game |

For slow motion (October 8; `include/spidy/game_time.hpp`), found from the
executable's strings (`TimeScaleSystem Update`, the channel names
`kHeroMelee` ... `kHeroMeleeKill`) and disassembly, then measured in the
running game with `tools/probe_slow_motion.py`:

| Fact | RVA | Verification |
|---|---:|---|
| `TimeScaleSystem` update (system): 23 channels, the winner by priority (+0x1ec + 4i; a tie to the lower scale), blended linearly at its rate (+0xd8 + 4i, 30/s by default) per real second | `0x19bb430` | Hooked; the game's own values restored before each update; 16 entry bytes and 6 instructions checked |
| The system's scale (+0x18), physics scale (+0x1c), flag "physics follows" (+0x7d0 bit 0); channel targets +0x20 + 4i, per-frame requests +0x47c + 0x24i | | Disassembly; the flag was set in free roam |
| Set a channel (system, channel, scale, rate, reason, kind) / clear one | `0x19bbaa0` / `0x19bbb40` | Disassembly; not used by Spidy |
| Linear blend (from, to, rate, seconds) | `0x1c477e0` | Disassembly |
| The clock's time scale (double): a frame's game time `0x7a7fbd8` is its real time `0x7a7fbf0` times it | `0x7a7fb90` | In the game: 1.0, then 0.30 slowed; the frame ratio went from 1.043 to 0.311 (a frame's game time has a floor near 1/240 s) |
| In the pause menu a frame's game time `0x7a7fbd8` is exactly 0.0, while the `TimeScaleSystem` goes on updating at scale 1.0 (61 updates a second) and `PlayerModelHudFollower` does not run | | In the game, October 11 (`reports/cutscene-3d.json`): the gate for scenes in stereo reads the frame's game time |
| A render instance's bounding sphere: centre in the model's own axes (3 floats), then its radius | instance `+0x40` | The HUD panel's: (-0.0098, 0, -0.4053), 1.5208; placed by that centre the HUD lies on the stereo screen's plane to a pixel, by the instance's position 22% behind it |
| Havok's step: float step, float base (1/30 s), owner; set by `0x1822670` (step = scale x base) | `0x609a560` | In the game: 0.0333 s, 0.0100 s at 30% |
| Component timestep (the player's mover's step) | `0x16769f0` | In the game: 0.301 game seconds per real second at 30% |

For Spider-Sense (October 18; `include/spidy/game_sense.hpp`,
`include/spidy/spider_sense.hpp`), found from RTTI, the game's reflection
tables and disassembly. **Read offline only: not yet traced in the running
game** (`tools/probe_sense.py` reads it there; the session report's `sense`
does in a headset):

| Fact | RVA | Verification |
|---|---:|---|
| `Hero::HeroDodgeManager` (0xce8 bytes), a component of the hero's actor; its type (name at +0x60 = "HeroDodgeManager" `0x38b2420`, set by the static initializer `0xb7451`) | vtable `0x38b23d0`; type `0x6dd6450` | RTTI; the hero's code finds it by that type in many places (`0x307860` ...) |
| Its attacks: 16 slots of 0xa8 bytes from +0x78 (cleared by its init `0x925af0`), the count at +0xaf8 | | Every function that reads +0xaf8 (`0x925ef0` ... `0x92d661`) walks them so |
| In an attack: the attacker's actor handle +0x00 (generation << 20 \| index, `0x15a0560`'s rule); when the hero is warned +0x30 (double); the flags +0x5c; the state +0x60 (0x20 waiting, 0x10 warned); when the blow lands +0x80 (double, -1 none) | | `RegisterAttack` `0x926600` writes them; `0x928780` lists the attackers whose state has 0x10; `0x928220` clamps +0x80 to now as an attack lands |
| Register an attack (manager, attacker record, data): the warning at once, or deferred to the blow's time less DodgeTime and 1/30 s (`0x38b2434`, 0.0333) | `0x926600` (from `0x92a700`, which finds the manager on the hero) | Disassembly |
| Warn (manager, attack): the hero's Spider-Sense sound (`0x16d5e80`, hash 0x4b5f5740), then state 0x10 for 0x20 | `0x926580` | Disassembly |
| Take an attack off (attacker, hero) | `0x928020` | Called by the action's stop event |
| `HeroDodgeRegisterAttackAction` (a ScriptAction): +0xb4 the attacker's handle, +0xb8 the hero's, its data at +0x60 (`0x926390` its defaults); start event 0xa5f8fd64 registers, stop event 0xfce23dc7 takes off | vtable `0x38b6840`; event `0x9aeaf0` | Disassembly |
| `BotAttackDodgeData` Properties: 0x1 kIsDodgable, 0x2 kIsCounterable ("Hero dodging can interrupt the attack"), 0x4 kNeverBeatToThePunch, 0x8 kUsesDodgeButton, 0x10 kUsesJumpButton, 0x20-0x100 kDodgeLeft/Right/Away/Toward, 0x200 kDontAutoRotateCamera, 0x400 kReversable, 0x800 kAllowCameraTrackOnJump, 0x1000 kDontDrawRangedShooterFX; times DodgeTime, OffScreenDodgeTime, RealDodgeTime, RealOffScreenDodgeTime, MaxPerfectDodgeDistance, MaxDamageImmunityTime | values `0x5df3d80`, names `0x5df3dc0`, fields `0x391f848` | The game's reflection tables; the registered flags' default `0x103c487` has bits above 0x1000 too, meaning unknown |
| The game's clock: seconds of game time, the manager's times on it | double `0x7a7fbd0` | Disassembly (RegisterAttack adds a time to it); its scale `0x7a7fb90` (game_time.hpp) |
| `Hero::HeroSpiderSenseManager` answers `HeroCounterableDodgeWindowOpenEvent` / `CloseEvent` (the flat game's mask lines, a material value 0x9402b89e set to 1, then 0) and `BotConfirmTimerIncreasing` (the stealth Spider-Sense) | vtable `0x38b4028`; handlers `0x966dd0`, `0x966d80`, `0x966c70` | Disassembly of its registrations at `0x966590` |

For the dodge a web carrying the player starts (October 24;
`include/spidy/game_dodge.hpp`, `spider_sense.hpp`'s `WebDodge`), read from
the exe with capstone, then started and ended in the running game with
`tools/probe_web_dodge.py`:

| Fact | RVA | Verification |
|---|---:|---|
| The manager's update (manager, game seconds): its timers and immunities, each attack (`0x92b6d0`), and while its dodge's window (+0xca0) lasts, the dodge again for warnings that went off since (`0x929d20`) | `0x92d640`, called once a frame for every manager by the component type's update `0x926040` | Hooked by `game_dodge.cpp`; its first 16 bytes checked |
| Dodge start (manager, kind, an attacker's handle or 0, the dodge's way or none, whether a perfect one slows time): its kind at +0xcd8, its answer at +0xcdc (0 none answered, 1 plain, 2 accurate, 3 perfect), when it started at +0xcc0; sets the hero's `HeroWAMUser` flag 0x10 (+0x180); sounds `0x5f9ae3a4` (accurate), `0x3cd06219` (perfect), `0xeedb2021` (none); combat action bits `0x8000` / `0x18000` (`0xb87540`) | `0x92a040` | In the game: kind 5 started on the hero's manager three times, answer 0 with no warning standing |
| Dodge end (manager, whether `HeroWAMUser` hears it at once): kind and answer 0, the window 0, the immunities left clamped to the config's +0x74 (`MaxDodgeEndDamageImmunityTime`), each dodged attack's 0x1 for 0x400000 | `0x929ef0` (28 callers: the states' ends) | In the game: kind 0 again after each |
| The dodge for each attack (manager, config, event, slot): warned (0x10), and not 0x100 unless its flags have 0x8 (kUsesDodgeButton); its stage under 2; the kind's flag in its flags (kinds 1, 2, 3, 5, 7, 12: 0x400; 9: 0x800; the rest either); then 0x1, with flags 0x21 an immunity from its damager (`0x927030`, `0x1ecabe0` on the hero's damage component), timed by the config (+0x58 by difficulty for an accurate one, +0x6c a perfect one, when +0x78 / +0x79 say so; by the reflection's field order `AccurateDodgeDamageImmunityTimeByDifficulty`, `PerfectDodgeDamageImmunityTime`, `...UsesTimedDamageImmunity`) or by the attack's own; and the answer: plain without kIsDodgable, perfect in its perfect window (0x8000, within the attack's perfect distance) on a side it allows (`0x929800`: flags 0x4000-0x20000 against the attacker's axes) | `0x929990`, for every attack from `0x929d20` | Disassembly |
| The kinds the hero's states start: 1 `HeroStatePounceOff` and `0x9f5390`, 4 `HeroStateSplineDodge`, `HeroStateWallRunLaneJump` and `HeroStateJumpDodge`, 5 `HeroStateTraversalDodge`, 6 `HeroStateJumpDodgeOver`, 7, 8 `HeroStateWebRush`, 10 `HeroStateJumpOffTarget`, 11, 12, 13 `HeroStateJumpGroundPound`, 14 `HeroStateZipToTarget`; the air states (jump, fall, swing jump, zip jump...) by their own (vtable slot 37), only with no dodge on | | Each caller's argument; owners by RTTI |
| The immunities: up to 16 of {time left, attacker, damager, the attack's warning time} from +0xb00, their count +0xc80; at 0 the damager leaves the damage component's set (`0x1ecad00`) | `0x92c700`, `0x92aa50` | Disassembly |

For the rays that stand in for the player's body (October 24;
`native_rays::meets`, `WorldQueries::bodyRaycast`, `Command::bodyRays`), read
from the exe with capstone, measured in free roam with a scratchpad MinHook
build on the test, then read through Spidy's own module with
`tools/probe_body_rays.py`:

| Fact | RVA | Verification |
|---|---:|---|
| The world's collision filter: `InsomniacCollisionFilter`, at the world's +0x498 (the pointer a query's +8 holds) | vtable `0x3d095b0` | RTTI; read in the game |
| Its test (filter, the query's word, the body's word): with the query's 0x800, a category in common (the low 11 bits; either none passes), the body's type (bits 12-15) among the query's types (bits 12-19), then the groups (bits 21-31, a bit matrix at +0x40, rows +0x48 bytes; bit 20 includes, else excludes; world bodies have group 0); without 0x800, a type table of 16 bytes at +0x30 | `0x18060a0`; callers `0x1807690` (query, body: the body's word at hknpBody +0x6c), `0x18076d0` | Disassembly; in the game every query word of the mover's profile (`0x..e7804`) met 0x43f and not 0x10 |
| Collision profiles: 32 bytes each from `[0x7813120]` (19 live, each starting -1): +6, +8 the types (low byte), +0xc the categories; a query's word is the categories, 0x800, the types << 12 and a group the query system leases | `0x17ff820` builds a query's filter from one | Read in the game: profile 3 `{-1, 0x00070001, 0xfee7, 0x4}` |
| The mover's profile: its MoverManager's +0xd9c (2 at its init `0x1fb995f`, set at `0x1fbbe96`); the player's is 3, categories 0x004, types 0xe7 | `MoverManager::SubmitSafeBodySizeRequest` (`0x1826600` from `0x1fbc37b`) | Its query filter logged in the game: `{0x2ed, 0x70001, 0xfee7, 0x4}`; Spidy's `SpidyRayBody` read 0x004, 0xe7 |
| What Spidy's rays (categories 0x410) met in the user's sessions of October 24: 0x43f, 0x63f, 0x6bf (the city), 0x10 (2 bodies, all vertical: the burning office's broken glass), 0x400, 0x43b, 0x6bb, 0x47b, 0x6aa, 0x79, 0x3b, 0x9b (no category of the player's mover: his body passes them) | | The session reports' ray samples (`filter` of each hit) |

For the button icons in the game's prompts (October 24;
`include/spidy/prompt_buttons.hpp`, `include/spidy/game_prompts.hpp`),
found from disassembly and measured in the running game with scratchpad
MinHook builds:

| Fact | RVA | Verification |
|---|---:|---|
| The HUD's context prompts: a manager of 50 slots that looks a prompt's text up by hash as it makes the prompt (`[ACTION_WEB_YANK] (Hold) Throw`, hash `0x9e608aeb`, from `0xe363cb`; `[BTN_Y] Interact`, `0xc77e6208`, from `0x6b2223` every frame) | `0xe36300` | In the game: the text written in the lookup before the prompt is made drew "(X) (Hold) Throw"; once made, a prompt keeps its text |
| The prompt widget: show (widget, kind 0-20, text, float, four more) / hide (widget, kind, text, flag) | `0x7793d0` / `0x77a1e0` | Hooked by `game_menu.cpp` (10 prologue bytes each checked) |
| `IconMarkupSystem` (static): +0x19 loaded, +0x40 dirty (sends Flash `setIconMarkups`, `0x1d299b0`), +0xa8 all cached, +0x20 `UIIconMarkupConfig` (+0x28 icons, 72 bytes x 183; +0x38 bindings, 64 bytes x 115: +8 name, +0x14 hash); its cache map at +0x78 | `0x5d98750` (vtable `0x3899a20`) | In the game; a binding's hash is the game's name hash (CRC table seeded `0xedb88320`, no final xor) of the token with its brackets. Patching the cache and the dirty flag changed neither prompt nor marker: the HUD keeps what it was sent |
| Resolve an image markup (subtitles and tokens; only direct caller `0x800350`) | `0x7a2330` | Disassembly |
| The virtual pad's icons are the Generic family. Its action words: `WEB_YANK` LB + RB, `WEB_YANK_BOT` Y, `ACTIVE_FINISHER` B + Y, `SUIT_POWER` L3 + R3, `PING` R3, `ZIP_TO_POINT` LT + RT, `WEB_ZIP_OBJ1` LT, `WEB_ZIP_OBJ2` RT, `TRICKMODE` X, `WEAPON_WHEEL` LB, `WEAPON_WHEEL_ALT` RB, `LS_*` the left stick | | Read from the cache in the game |
| The world markers: `AddPOI(id, category, subtype)`, its name the literal at `0x38a02a0` (the `lea rdx` at `0x1f07f98` before the call at `0x1f07fa6`); `POIInteractAvailable(id, on, text)` (`0x38982c8`) and `POISetButton(id, button)` (`0x4f64770`) after it; `ClearPOI(id)` (`0x4f646e0`) ends it | | Logged through the UI's call wrapper |
| `HUD.managers::POIManager` in POIs.gfx: categories `POI_INTERACT` 9, `POI_OBJECTIVE` 3, ...; interact subtypes `SUBTYPE_TALK` 1 (a pedestrian: `POISetButton(id, 0x85920BAD)`, the name hash of the hero's `kInteract`, which `POI.SetButton` ignores, and the text `[BTN_Y] Interact`), `SUBTYPE_THROWABLE` 7, `SUBTYPE_GENERIC_TRIANGLE` 12, `SUBTYPE_GENERIC_SQUARE` 13, `SUBTYPE_GENERIC_CIRCLE` 14, `SUBTYPE_GENERIC_X` 15 | | The movie's bytecode (ABC) read from the game's memory and disassembled, October 24 |
| `POIDefault` (7, 8 and 12-15, 22, 32): the throwable's `RefreshThrowable` asks the game `getDataFromMarkup('[ACTION_WEB_YANK]')` and draws a keyboard's markup, a Steam Input pad's two parts (LB . RB beside the diamond: the virtual pad), or `icon_web_throw`; 12 to 15 draw `icon_face_button_top`, `_left`, `_right`, `_bottom` | | Disassembled; subtype 7 made 13 drew X over a mailbox (a research build; Spidy's `game_prompts::rewriteCall` makes the same call) |
| A bumped pedestrian: `ShowText(1, 'Spider-Man: Sorry about that.')` | | Logged |

For the game's texts and its tutorials (November 22; `include/spidy/vr_texts.hpp`, `tools/game_texts.py`), read
offline from the game's archives and the exe with capstone, then checked in the running game with
`tools/probe_vr_texts.py`:

| Fact | RVA / where | Verification |
|---|---:|---|
| `asset_archive/toc`: u32 0x77AF12AF, u32 size, zlib of a DAT1 (u32 'DAT1', u32 type, u32 size, u32 count, then {tag, offset, size} per section). Sections: 0x506D7B8A asset ids (u64), 0x65BCF461 {1, size, index}, 0xDCD720B5 {archive, offset}, 0x398ABFF0 archive names (72 bytes each, the name from +8), 0xEDE8ADA9 spans {first, count} (one a language or variant) | | `game_texts.py` reads it; 771,677 assets |
| An asset's id: CRC-64/ECMA reflected (poly 0xC96C5795D7870F42) seeded with the polynomial, over the path lowercased with `/`, then `>> 2 \| 0x8000000000000000`. Ids from 0xE000... are not path hashes | | `localization/localization_all.localization` 0xBE55D94F171BF8DE, `configs/system/system_tutorialprompt.config` 0x904043FBE0214EF3, `configs/UIConfig/UIIconMarkup.config` 0x9E439288DE960BF2 |
| `gNNsNNN` archives are DSAR: 32-byte header (u32 'DSAR', version, block count, header size, u64 unpacked size), then 32 bytes a block {u64 unpacked offset, u64 file offset, u32 unpacked size, u32 stored size, u8 kind}; kind 3 is a raw LZ4 block of 256 KB. An asset is a 36-byte header and a DAT1 | | Every block read back to the sizes the toc gives |
| The texts: `localization_all.localization`, 23 copies (one a language; the English is the first, in g00s019). 57,403 entries; sections 0xD540A903 the count, 0x06A58050 each entry's hash, 0x4D73CEBD the tags with 0xA4EA55B2 their offsets, 0x70A382B8 the texts with 0xF80DEEB4 theirs, 0xC43731B5 the hashes sorted with 0x0CD2CFE9 (u16) the entry of each, 0xB0653243 flags | | `CTRL_ACTION_THROW` 0x9E608AEB and `PROMPT_INTERACT` 0xC77E6208, the hashes logged in the game on October 24 |
| The table in memory: the object at `[0x6057f80]`: +0x30 count, +0x40 sorted hashes, +0x48 the u16 entries, +0x50 tag offsets, +0x58 text offsets, +0x60 flags, +0x68 tag blob, +0x70 text blob | global `0x6057f80` | Disassembly of the readers below |
| A text by hash (table, hash): the binary search (`0x1bc9bb0`), then blob + offset; null for none. Its siblings: `0x1748cd0` the tag, `0x1748d40` flag 2, `0x1748de0` a text by index | `0x1748d90` | Hooked (`game_menu`, `tableTextHook`); first bytes `40 53 48 83 ec 20 44 8b 41 30` |
| The lookups over it: by hash (hash, fallback) `0x1749970`, by tag (tag; the tag's hash `0x1bb88d0`, CRC table seeded 0xEDB88320) `0x1749a20`; both call `0x1748d90`. Help lines (`ShowText(7, ...)`) take the one by tag, which is why the hook on `0x1749970` never saw them (October 13) | `0x1749970` (241 callers), `0x1749a20` (149) | Disassembly; `SpidyVrTextsCheck` runs every entry through both |
| The tutorial prompt config: contextual prompts by name (`StandardDodgePrompt`, `ZipToVentPrompt`, ...), each a tag of the table (`HUD_MSG_DODGE`, `CTRL_ACTION_ZIPTOVENT`) | `configs/system/system_TutorialPrompt.config` | Read from the asset |
| The hint cards: `UITutorialMessageAction` (vtable string `0x38a1d10`), `PlayerHudMessage::TutorialMessage` (pauses the clock: "TutorialMessage setting clock mode to kPause"); the tutorials' ids `kTestTutorial` ... `kSuitSwapTutorial` | strings `0x38957c8`, `0x39379c8` | Strings only |
| Icon markup (offline, not yet run in the game): `UIIconMarkup.config` holds 183 icons {IconId, Texture path, VectorIconName, width, height, vspace} and 115 bindings {MarkupText, hash, mapping per pad family}. A token resolves (`0x7a2330`, per token `0x7a0070`) to `<img src='img://%s' width='%d' height='%d' vspace='%d' />` with the UIIconId name (`img://XBButtonX`); `GUI::IGImageCreator` (vtable `0x4f45d30`, slot 1 `0x1d23b80`) serves `img://` from a registry filled by `0x1d23400` (name, texture path). `GUI::GameUIData` (`0x6d93be0`) slot 9 `0x800650` adds a token of its own (token, texture path, width, height, vspace; 63 places). No texture of the game shows a hand, a grip or a gesture | `0x7a2330`, `0x800650` | Disassembly and the config asset |

For the throw of a prop (October 24; `include/spidy/throw_aim.hpp`,
`src/game_throw.cpp`), from disassembly and measured in the game with
scratchpad MinHook builds:

| Fact | RVA | Verification |
|---|---:|---|
| `Hero::HeroAimManagerLocal`: its results in the object its +0xB9CC handle names; per slot an override index at +0xC408 for slots 0-3 and 6 only (mask 0x4f) | vtable `0x38b1658`, type `0x6dd5a40` | Disassembly of its slot getters `0x8ff1a0` (actor), `0x8ff3a0`, `0x8ff340` (part), `0x8ff160` (way); the type's name from its registration `0xb64bd` |
| The results' getters: actor handle `+0x9FB8 + 0x18 slot` (results, out, slot); aim point `+0x9FC4 + 0x18 slot` (results, slot); level way `+0x9B28` x, `+0x9B30` z, stride `0x98` (results, out, slot), y zeroed and normalised | `0x957620`, `0x957640`, `0x9575d0` | Hooked (Spidy's `game_throw`): entry bytes `41 8b c0 4c 8d 04 40`, `8b c2 48 8d 14 40`, `40 53 48 83 ec 30` |
| HeroStateWebYankEnviro's throw target (slot 4, the aim manager found by its type on the hero): an actor and part give the way to his aim point (`0x211eb50`: his position, or his target point of that byte index), else the slot's way, else the state's +0x32c; stored at +0x358 (way), +0x364 (actor handle), +0x368 (part) | `0xa52270` | Disassembly. In the game: slot 4 read about 117 times in one 1.5 s hold, never outside one; its way the camera's heading (-39.5 against -39.8, -90.2 against -90.1) |
| `0xa51340` sends the state's target as `HeroStateWebYankAndThrowTargetUpdate` (co-op sync, behind a flag and a frame count); `0xa55950` stores a level way at +0x320 | | Disassembly |
| Actors: records of 0xc0 bytes in a table of their own; a handle's index (20 bits) names the record, its generation (11 bits) the word at record +0x10; a record holds its index at +0xC | table `0x7a44380`, count `0x7a4439c`; resolver `0x15a0560` | Disassembly; the throw's handle made as `0xa52429` makes it |

The camera-related entry was temporarily hooked by the independently written
Spidy observer. Local disassembly showed RCX as the object, XMM1 as the float
delta, and the low bytes of R8/R9 as flags. A live 30-second test produced 3,362
valid transform observations. The observed object sits at HeroCameraManager +
`0x6c0` and has the AimContextSwing vtable; treating `this` as HeroCameraManager
failed validation. Its flag meanings and behavior outside the tested free-roam
scene remain unknown. See `reports/camera-update-owner.json` and
`docs/VALIDATION.md` for measured evidence and limits.

The observer disabled its hook and verified entry-byte restoration, including a
second enable/capture/disable cycle with 543 valid observations. No physics write
was part of those early observer tests. Later stereo and collision evidence is
recorded in `docs/VALIDATION.md`. No diagnostic DLL was installed in the game
directory; it remains in the process only until game exit.

## Native body identity and airborne velocity

Offline reflection in this exact executable describes `hknpBody` as 0xc0 bytes.
The member table at `0x52849b0` identifies `motionId` at `0x40`, `flags` at
`0x44`, `lodFlags` at `0x68`, `id` at `0x70`, and `broadPhaseId` at `0x78`.
The earlier collision capture's 0x68 word contains LOD and material fields;
it must not be interpreted as the motion flags. Ray telemetry version 2 fixes
that label and includes motion and broad-phase IDs.

Native lookup `0x2e410d0` bounds the body index using world + `0x30`, checks
flags & 3, compares the full ID, and rejects broad-phase ID -1. The motion
property setter at `0x2e49630` skips bodies with flags & 1. Captured city
buildings use that flag and motion ID 0. The fixed-anchor predicate requires
all of these checks; a matching slot index alone cannot identify a live body.

RTTI identifies event handler `0xa7b3a0` in jump and fall states, including
vtables `0x38c1340` and `0x38c9090`. It reads displacement through
`0x1f9db60` (return `0xa7b489`), derives velocity at state + `0x3f8`, and
updates vertical gravity history at `0x3cc`. The optional movement synchronization
hook replaces that one decoded displacement only while the local actor has a
valid velocity lease. The original handler performs its own state updates.
It does not directly write the actor transform or airborne-state fields.
The live experiment updated these fields but did not prevent the post-lease
vertical drop. This experiment is disabled by default. The swing controller
instead keeps control of flight after web release and returns it on landing.

Through Spidy's flight the handler runs about twice a mover step (the
movement bridge's `airEvents`): in the user's sessions of October 15 not one
driven airborne step went without it. Steps without it are the hero in
another of the game's states - its wall grasp (mover flag `0x80a0`), or one
of its own moves, none of which derive from its jump states:
`HeroStateZipToPointLaunch` `0x38d5dd0`, `HeroStateZipToGround` `0x38d5ea0`,
`HeroStateWebRush` (the web strike) `0x38c4dc0`, `HeroStateVehicleLanding`
`0x38bf1f8`, `HeroStateVehicleTakedown` `0x38bf598` (RTTI; a car chase's zip
onto the car is the web strike at the vehicle, `VehicleWebRushedEvent`).
`game_swing::AirYield` lets the game have the hero for such a move the
player asked for, once the handler goes quiet.

**The game's aim mode** (October 18, its strings and a session report; the
code not traced). LT alone is it: "Lock out the standard aim mode
functionality (L2) while the photo gadget is equipped". Its aim snaps to
the points the hero can zip to ("If true, we'll snap to zip-to-perch
targets", "We'll only snap if a valid snap target is found this many
frames into aim mode"), and it slows the game's own time once it has lasted
a moment ("How long to delay at the start of aim-mode before triggering
time-scale", "How long in real time to allow player driven time-scale to
persist", "How long between player driven time-scale requests", "Should
time slow when in aim mode as this character?"). In the user's session of
October 18, LT + RT held from the right stick brought the game's own scale
to 0.2 eight times, 0.56-0.64 s after they went down, for up to 3 s (once a
single frame: the stick let go then), in the air and once standing; in the
air the game's air handler went on running through it and the swing kept the
hero. Its states by RTTI:
`HeroStateAimModeIdle` `0x38d1cf8` (local `0x38d1de8`),
`HeroStateAimModePreFire` (local `0x38d2060`), and `HeroStateAirFire`
`0x38c14a8` (local `0x38c1828`), whose table has the air handler
`0xa7b3a0` as the jump and fall states' do. `game_pad::ZipPress` takes the
slowed time as the sign a held zip found no point: LT alone from then on,
and RT again beside it as the stick is let go. That RT in the aim mode zips
to the point its aim holds is the game's LT + RT as its controls have it,
not measured.

## Rope points and gadgets (October 15, read from the code only)

Static reading of this executable, never run: what to check in the game
before relying on any of it.

**A rope's points.** A rope slot (manager `+0x50 + slot * 0x7c8`) keeps its
points at `+0x1c` (Vec3, stride 12, at most 128), their count at `+0x6ac`.
Inside RopeManager::Update `676dd0`, `679dc0` rebuilds every attached rope's
points from scratch each update: Start, Grip when it is not zero, then the
hero's hand points (`95f9c0`, the hero rope manager's vtable `+0x78`, entry
`48 8b c4 f3 0f 11 50 18 55 53 56 41 56 48 8d a8`, `void(mgr, rope, dt,
int* count)`), then the line to the anchor - from Start (or a non-zero Grip),
NOT from the last hand point (checked October 15, `0067a5d2` and the fill loop
at `0067ac0c`): points after the hand points lie on Start-to-anchor, at most to
min(128, most) in all. A caller laying points of its own makes the last of them
Start for the update (native_webs does), or the line comes back to Start; `677f40`,
still inside the update, copies the points into the tube and ribbon geometry
with their bounds. Points written after the update are never drawn; written
by the hand points, they are (native_webs' `handPoints`). The most points a
rope type takes: int16 at `[rope + 8] + 0x28`. Creating a rope posts
HeroRopeFiredEvent (`0xef6e3c2a`) at its first update (`+0x70c` bit 0x40):
the shot's sound.

**The player's gadgets.** The hero's HeroWeaponManagerLocal (TYPE static
`0x6dd84e0`, vtable `0x38b59b8`) keeps a weapon loaded for every gadget the
player owns: records at `+0x1a8 + i * 0x18` (weapon id at `+0x10`), their
count at `+0x628`; eight slots of `0x28` bytes, slot `s`'s weapon id at
`+0x6c + s * 0x28` - slot 0 the equipped gadget (what RB fires), slot 2 the
built-in web shooter. A weapon by its id: `21633c0(manager, id)`. Gadget
classes by vtable: WebShooter `391c950` (the built-in one), WebBlast
`391bb10` (the Impact Web: the exe's own text at `3abd870` has "the Web
Blast gadget (aka Impact Web)"), WebBomb
`391bfc0`, ElectricWeb `3919110`, ProximityMine (Trip Mine) `391ace0`,
ShockerBlast (Concussion Blast) `391b1a0`, SpiderDrone `391b658`,
AirLauncher (Suspension Matrix, a guess) `39186a0`, and ImpactWeb
`3919a80`, which for all its name is the gadget the game calls the Web
Shooter, its charged web ball (its HUD reads "Web Shooter" with it
equipped; until October 18 Spidy took it for the Impact Web). All
share the fire event (`+0x108` = `e3bd80`) and the charges: `+0x188` now
(`e3b710`), `+0x190` most (`e3b790`), both `float(weapon, trigger)`; each has
its own event fill at `+0x100`. Equipping: GameEquipInventory (TYPE
`0x6e09180`) keeps the selected weapon id at `+0x11a`; the game's wheel
calls `bbc2d0(inventory, guid, &request)` with the item's GUID and a request
`{-1, 1, 0, 0, 0x6dacab4c}`, applied the next frame. Given a GUID the player
does not own, that call creates the item: pass only GUIDs read from owned
items. Firing: the game checks the manager (`+0x108` of its vtable), the
weapon's readiness (`+0x2a0`) and charges (`+0x180`), and its fire handler
consumes the charges itself for the usual fire modes. Spidy's game_shooter
reads the gadget list (Data v3); equipping and firing from the hand wait for
a run in the game.

The game's Web Shooter from the hand (October 22, `tools/probe_gadgets.py
--only-web-shooter`, free roam on your save): with it equipped (slot 0 the
weapon no wheel item names, 40076 there), 12 pulls fired 10 of its own shots
and, its 10 charges spent, 2 of the built-in web shooter's balls; the game's
HUD showed its Web Shooter. The game's camera, with the virtual pad in free
roam: D-pad up equips a weapon of its own in slot 0 (40079 there, owned, on
no wheel item) and the UI calls `ShowCameraOverlay()` (name literal
`0x3894818`) and `CameraRefresh(-1, 'No Subject In View', ...)`
(`0x38948a8`); LT held raises the first-person viewfinder (`SetReticle`,
`ShowReticle`); RB takes the photo (the frame flashes white), RT takes none;
D-pad up again gives slot 0 back to the gadget before and calls
`HideCameraOverlay()` (`0x3894830`). A load's `ShowLoadingIcon` is at
`0x4f44a68`, `ForceHUDShutdown` at `0x38946c8`.

The game's Focus (October 22, read from the exe with capstone, then
measured with `tools/probe_focus.py`):

| Fact | RVA | Verification |
|---|---:|---|
| `Hero::HeroSpiderAssaultManager`, the Focus meter: segments of {current, most} floats from +0x110, their count at +0x128, the segment filling at +0x12c | vtable `0x38b3f10` (slot 0 its event setup `0x960a60`), component type `0x6dd79c0` | 47 of the type's 52 code references are component lookups; on your save 3 segments, most 300 |
| Its events: DamageDealtEvent `0x962a00`, DamageEvent, HealthChangedEvent, HeroSpiderAssaultAddEnergyEvent (an amount at +0x138), HeroCombatActionEvent `0x963aa0` (dodges by action bits), HeroWebYankToHeroEvent, HeroTimedImpactEvent, TargetWebbedEvent, TargetElectrifiedEvent, Bot{Ground,Wall}StuckEvent | registered in `0x960a60` through `0x1676070` | Event classes by their factories' vtables (RTTI) |
| AddEnergy (manager, amount, flags); energy for a kind (manager, kind, flags); both (`961e90`) | `0x961890`, `0x962210`, `0x961e90` | `0x962210` switches on the kind (jump table `0x962570`) to the config's float from +0x90 |
| Energy kinds (enum names at `0x5ee1400`): 0 kNone, 1 kMeleeDefault, 2 kMeleeAir, 3 kStealthKill, 4 kDodgeAccurate, 5 kDodgePerfect, 6 kOtherDamage, 7 kElectricWeb, 8 kImpactWeb, 9 kWebBombShot, 10 kProximityMine, 11 kWebBlast, 12 kAirLauncher, 13 kShockerBlast, 14 kSpiderDroneShot, 15 kYankBot, 16 kYankBotIntoAirBonus, 17 kThrowBot, 18 kThrowObject, 19 kThrowObjectIntoBot, 20 kYankEnviro, 21 kYankEnviroOnBot, 22 kBotFlungIntoBot, 23 kElectrified, 24 kWebStunned, 25 kWebbedToGround, 26 kWebbedToWall, 27 kJustFrameHit, 28 kWingedStriker, 29 kAirTrick | | kOtherDamage has no case: 0. Measured on your save: kMeleeDefault 1.25, kMeleeAir 4, kYankBot 4 |
| DamageDealtEvent's fields: the request's own at their offsets (DamageHash +0x19c, amount +0x144, victim handle +0x1a8), and a list of named custom data an attack attaches (count +0x188; an entry's name hash +0x28, data +0x30, size +0x38), read by name hash | `0x1ed2620` | Spidy's requests carry none, and fall to kOtherDamage |
| `Hero::HeroComboCounter`: its events in `0x918db0`; dealt damage `0x9197b0`, damage taken `0x9197f0` (resets), combat actions `0x919850`; add a hit (counter, victim): count +0x58, live +0x60, the game's benchmarks at 25 and 35; its tier (+0x68) scales Focus in AddEnergy (`0x919700`) | vtable `0x38b22b8`, `0x919450` | Read, not run |

The game's melee impact sound (October 22, read from the exe with capstone,
then played from Spidy's call in the game with `tools/probe_sound.py`):

| Fact | RVA | Verification |
|---|---:|---|
| The sound call (system, hash, the emitter's actor handle, a pointer to it, then int 1, byte, qword, dword, byte, float, float, qword, qword): looks the hash up in the system's event map (+0x291e0, count +0x291d8; the game's map lookup `0x1bcf3e0`), posts it through `0x16d2f90` and gives back its sound object, 0 when not posted | `0x16d5e80`, system `0x7475690` | 2282 call sites; the Spider-Sense warning's (`0x926580`, hash 0x4b5f5740) and the melee impact's below. From Spidy's frame hook every call gave a sound back |
| An actor's handle for it: generation `word [actor+0x10] & 0x7ff` << 20 \| index `dword [actor+0xc] & 0xfffff` | `0x1676900` (from a component: `[component+8]` the actor) | Disassembly |
| `AnimImpactEventManager` (0x718 bytes): an attack's impact entries, 0x150 bytes each from +0x50 (count +0x5a4, their animation times +0x590, fired flags +0x5a0); the attack config's reference +0x5ac; the one struck's handle +0x5a8 while an impact plays | vtable `0x3882bd0` (slot 0 its event setup `0x69b320`), component type `0x6d163a0` | Found on the hero only while the flat game's own attack runs (none before, none in VR play) |
| Its DamageDealtEvent handler: an impact plays only for a request from its own attack config (+0x168 against +0x5ac) carrying its impact id (named data 0x9b0d0501 against +0x5d0) and not muted (named data 0x12865ca2) | `0x69e5d0` | Why Spidy's blows (plain DamageSystem requests) never sounded |
| Its per-frame update fires each entry at its animation time; one impact (manager, entry) plays 0x6845dc9d on the one struck with a variant at the sound's +0x138 (the entry data's +0x4c where +0x48 says so, else the config's `0x8c4910`: 0 or 1 seen; 0 plays none), then the melee impact below | `0x69e870`, `0x69c730` | Read; 0x6845dc9d played from Spidy's call gave a sound object but no sound heard in a recording |
| The melee impact: on each sound source of the one struck's `SoundSourceComponent` (count +0x154, object ids at +0x48 + 12 i) it sets five switches of the "Melee Impact Event" through SetSwitch (object, group id, value id, group name, value name, context; `0x196b920`): Enemy_Impact_Location from the entry data's +0x40 (kImpact_Head, kImpact_Torso, kImpact_Leg; table `0x4f90aa0`), Enemy_Impact_Intensity from +0x44 (kImpact_None, Light, Medium, Heavy, Extreme; `0x4f90af0`), Hero_Combo_Depth (Combo_0 to Combo_5; `0x393a430`), Hero_Elevation (Ground, Air; `0x393a390`), Hero_Attack_Type (BasicMelee, WebStrike; `0x393a3e0`); then plays the hit sound the entry data's +0x3c picks: 0 0x7b71909e, 1 0x9387aab9, 2 0xe21849db (the third also with the attacker's component); one struck the game does not spare (`0x1f16a40` on his handle) has the sound's +0x138 set to 1, flag 0x100 at +0x8 and +0x18 | `0x69ceb0`; `SoundSourceComponent` type `0x7a51f80` | The tables' value names read from the exe. From Spidy's call on the player (3 sound sources): every switch took (15) and every hit sound gave a sound object back |
| The sound engine's ids: FNV-1 (32 bits, from 0x811c9dc5, multiply by 0x1000193 then xor) of the lowercased name | `0x1bb8a10` | Disassembly; `game_sound::audioId`, core test |

## OpenXR

Dependency: [Khronos OpenXR SDK 1.1.49](https://github.com/KhronosGroup/OpenXR-SDK/tree/977f6675bc0057d5a54ed290cb5c71c699b1c0ab),
pinned to `977f6675bc0057d5a54ed290cb5c71c699b1c0ab`.
The SDK supplies headers and the static loader. Its licenses remain in the
dependency checkout. See the [Khronos D3D12 tutorial](https://openxr-tutorial.com/windows/d3d12/3-graphics.html)
for swapchain, projection, and submission concepts.

VirtualDesktopXR's installed runtime loaded successfully and advertised D3D12.
The launcher selects its JSON manifest only in the launched process environment;
the system's SteamVR OpenXR registration is unchanged.
