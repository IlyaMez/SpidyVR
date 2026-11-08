# Gameplay presets

With VR stopped, open the **Profile** dropdown on **Play** to choose Arcade,
Balanced, Pro, or one of your saved profiles. **Manage** opens Options > Profiles.
Changing a preset is optional: installing this build keeps your current settings.

| | Arcade | Balanced | Pro |
|---|---|---|---|
| Feel | Light, forgiving, assisted | Real anchors with gentler catches | Heavy momentum and physical control |
| Speed limit / weight | 32 m/s / 80% | 40 m/s / 125% | 56 m/s / 200% |
| Webs | Arcade, open-air fallback | Realistic, near-miss anchor assistance | Realistic, deliberate aiming |
| Catch / release | Medium elastic / soft | Low elastic / soft | Rigid / immediate |
| Swing assist | On | On | Off |
| Flips | Off | A in the air toggles manual left-stick flips | Arms and swing momentum rotate you; A rights you |
| Movement | Native sprint, softened landings | Momentum carries into runs; web gestures | Hand climbing, airborne slingshots, run from standing |

All 52 gameplay settings have explicit values, including inactive fine tuning.
There is one active flip mode at most; open-air webs and smart anchor are never
enabled together. All three keep realistic flips out of combat, enable responsive
combat movement, and retain the air-rotation brake. Pro intentionally includes
view rotation and banking; it is not simply every slider at maximum.

Personal setup stays as you set it: seated mode, height and body calibration,
turning, web-button mapping and aiming alignment (including eye tracking),
haptics, sound and media, HUD and screen presentation, cutscenes, graphics,
recording and diagnostic logging. A preset remains selected when only these
preferences change. A gameplay edit adds `*`; use **Save as a profile** to keep
your variation. Built-ins cannot be overwritten or deleted.

Your existing custom and imported profiles still apply the keys they contain,
including personal settings if those keys are present. Missing keys stay unchanged.
Copying or exporting an unmodified built-in includes gameplay only. A custom
save retains the existing full-profile behavior. Before replacing settings not
already held by a complete saved profile, Spidy saves **Previous settings** so
you can return to them. Preset changes take effect for the next VR session.

## Community review, November 8, 2026

These are proposed starting points derived from community examples and current
code behavior, not a community vote or a headset-tested balance claim. Reviewed
the ten profile discussions and sharing guide, six inline profile configurations,
the local Flippin smooth profile, 300 recent messages each from spidy-chat and
spidy-dev, and relevant movement/flip suggestions and bug reports. Discord CDN
rejected the attached .spidy download with HTTP 403, so attached-only configurations
were not used as numerical evidence.

- [Webslinger](https://discord.com/channels/1557032200606842880/1562531532877332620/1562531532877332620)
  uses arcade ropes, 32 m/s, 80% weight and no flips: the starting point for Arcade.
  Open-air fallback, softer catches and wall assistance are our additional
  accessibility choices, informed by requests for
  [swing assistance](https://discord.com/channels/1557032200606842880/1563190482513694730/1563190482513694730).
- [SlickSwing](https://discord.com/channels/1557032200606842880/1562975865841909890/1562975865841909890)
  favors a slower response and higher gesture threshold. The
  [realistic/air-web hybrid](https://discord.com/channels/1557032200606842880/1563090120704393266/1563090120704393266)
  shows that rope style and assistance can be chosen separately. Balanced uses
  real surfaces with smart anchoring rather than air webs, and deliberate manual
  flips rather than automatic rotation. Its exact values are our synthesis.
- [Webhead](https://discord.com/channels/1557032200606842880/1562881824160743444/1562881824160743444)
  combines realistic ropes and more weight. The
  [200% weight / 150% yank pairing](https://discord.com/channels/1557032200606842880/1563495157578334338/1566631247868985356)
  informs Pro. The
  [responsive-flips profile](https://discord.com/channels/1557032200606842880/1563535316206223440/1563535958811615233)
  supplies the low-strength/high-speed direction; Pro uses a less abrupt 200 ms
  response and a higher hand threshold than that example.
- Reports of [unwanted combat flips](https://discord.com/channels/1557032200606842880/1563665753759023266/1563665753759023266)
  inform disabling combat rotation across all three. Requests to
  [separate arms from swing rotation](https://discord.com/channels/1557032200606842880/1563741490193105057/1563741490193105057)
  and [avoid constant rotation](https://discord.com/channels/1557032200606842880/1557335777153650788/1568441283221917840)
  support keeping automatic rotation exclusive to the opt-in Pro preset.

## Complete gameplay values

These are the shared-profile keys: switches use 0/1. Speed is m/s, weight and
strength are percentages, flip speed is degrees/s, response/drag are milliseconds.
Web speed 0 is instant and 3 is 350 m/s; elastic/release values 0/1/2 mean
off/low/medium; early jump 1 is quick tap; dodge tumble 1 is lean.

| Setting | Arcade | Balanced | Pro |
|---|---:|---:|---:|
| `arcade_webs` | 1 | 0 | 0 |
| `aim_markers` | 1 | 1 | 0 |
| `air_webs` | 1 | 0 | 0 |
| `smart_anchor` | 0 | 1 | 0 |
| `web_range_m` | 150 | 100 | 100 |
| `web_speed` | 0 | 3 | 3 |
| `swing_speed` | 32 | 40 | 56 |
| `weight` | 80 | 125 | 200 |
| `elastic_web` | 2 | 1 | 0 |
| `soft_release` | 2 | 1 | 0 |
| `swing_assist` | 1 | 1 | 0 |
| `web_slingshot` | 0 | 1 | 1 |
| `slingshot_speed` | 40 | 55 | 80 |
| `better_climbing` | 1 | 1 | 1 |
| `single_hand_yank_percent` | 150 | 125 | 150 |
| `two_hand_zip` | 0 | 1 | 1 |
| `rope_steering` | 0 | 1 | 1 |
| `flips` | 0 | 0 | 0 |
| `controlled_flips` | 0 | 1 | 0 |
| `flip_speed` | 180 | 240 | 300 |
| `hand_climb` | 0 | 0 | 1 |
| `joystick_wall_run_camera` | 1 | 1 | 0 |
| `wall_run_camera_tilt` | 0 | 1 | 1 |
| `half_punch_damage` | 0 | 0 | 0 |
| `web_tie` | 0 | 1 | 1 |
| `native_run` | 1 | 1 | 1 |
| `air_slingshot` | 0 | 0 | 1 |
| `dodge_tumble` | 0 | 0 | 1 |
| `run_curve` | 1 | 0 | 0 |
| `native_run_at_percent` | 70 | 70 | 80 |
| `running_leap` | 0 | 1 | 1 |
| `early_jump` | 1 | 1 | 0 |
| `soft_landing` | 1 | 1 | 0 |
| `spidy_run` | 0 | 1 | 1 |
| `spidy_run_from_standing` | 0 | 0 | 1 |
| `web_landing_smooth` | 1 | 1 | 1 |
| `web_landing_impact_speed` | 24 | 24 | 24 |
| `steady_states` | 1 | 1 | 1 |
| `combat_responsive` | 1 | 1 | 1 |
| `spidy_run_jump` | 1 | 1 | 1 |
| `web_lines` | 0 | 1 | 1 |
| `realistic_flips` | 0 | 0 | 1 |
| `rotation_strength` | 25 | 50 | 25 |
| `rotation_speed` | 240 | 240 | 360 |
| `flight_speed_min` | 8 | 8 | 8 |
| `swing_momentum` | 0 | 0 | 1 |
| `rotation_response_ms` | 750 | 500 | 200 |
| `hand_momentum` | 100 | 100 | 100 |
| `angular_drag_ms` | 500 | 1500 | 1500 |
| `hand_speed_min_cm` | 200 | 200 | 100 |
| `air_rotation_brake` | 1 | 1 | 1 |
| `flips_in_combat` | 0 | 0 | 0 |
