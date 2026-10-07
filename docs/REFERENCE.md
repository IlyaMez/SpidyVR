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
