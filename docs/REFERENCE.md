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
- R.E.A.L. VR was present during initial research. On October 4 its local files
  were removed from this game's directory at the user's request and backed up
  under `backups/realvr-removed-20261004`. The executable hash is unchanged.

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
| Havok's step: float step, float base (1/30 s), owner; set by `0x1822670` (step = scale x base) | `0x609a560` | In the game: 0.0333 s, 0.0100 s at 30% |
| Component timestep (the player's mover's step) | `0x16769f0` | In the game: 0.301 game seconds per real second at 30% |

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

## OpenXR

Dependency: [Khronos OpenXR SDK 1.1.49](https://github.com/KhronosGroup/OpenXR-SDK/tree/977f6675bc0057d5a54ed290cb5c71c699b1c0ab),
pinned to `977f6675bc0057d5a54ed290cb5c71c699b1c0ab`.
The SDK supplies headers and the static loader. Its licenses remain in the
dependency checkout. See the [Khronos D3D12 tutorial](https://openxr-tutorial.com/windows/d3d12/3-graphics.html)
for swapchain, projection, and submission concepts.

VirtualDesktopXR's installed runtime loaded successfully and advertised D3D12.
The launcher selects its JSON manifest only in the launched process environment;
the system's SteamVR OpenXR registration is unchanged.
