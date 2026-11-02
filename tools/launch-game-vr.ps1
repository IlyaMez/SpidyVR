param(
    [ValidateRange(0,25)][int]$Seconds=0,
    [ValidateRange(0,8192)][int]$Size=0,
    [ValidateRange(50,200)][int]$RenderScale=100,
    [ValidateRange(50,100)][int]$Upscale=67,
    [ValidateRange(0,240)][int]$MonitorRate=30,
    [ValidateRange(1,65)][float]$SwingSpeed=32,
    [ValidateRange(40,300)][int]$Weight=80,
    [ValidateRange(90,480)][int]$FlipSpeed=180,
    [ValidateRange(0,90)][int]$SnapTurn=30,
    [ValidateRange(0,360)][int]$SmoothTurn=0,
    [ValidateRange(0,100)][int]$Haptics=100,
    [ValidateSet('Small','Medium','Large')][string]$ScreenSize='Medium',
    [ValidateSet('Off','Small','Medium','Large')][string]$Hud='Medium',
    [ValidateSet('Off','Low','Medium','High')][string]$ScreenDepth='Medium',
    [ValidateRange(0,2500)][int]$EyeHeight=0,
    [ValidateRange(0,1200)][int]$ArmLength=0,
    [string]$Python,
    [switch]$CaptureImages,
    [switch]$OverlayWebs,
    [switch]$NoWebGrab,
    [switch]$NoAirWebs,
    [switch]$NoBody,
    [switch]$NoPunch,
    [switch]$NoWebShooter,
    [switch]$NoAimMarkers,
    [switch]$NoPromptHelp,
    [switch]$NoCalibrationPrompt,
    [switch]$Flips,
    [switch]$TriggerWebs,
    [switch]$ArcadeWebs,
    [switch]$NoStandOnWalls,
    [switch]$NoWallRun,
    [switch]$NoEyeOcclusion,
    [switch]$NoEyeAntialiasing,
    [switch]$GameMonitorView,
    [switch]$OwnShadows,
    [switch]$EyeLensDistances,
    [switch]$NoEyePacing,
    [switch]$NoEyeReflections,
    [switch]$StockMonitorView,
    [switch]$FullDesktopView,
    [switch]$AttachOnly,
    [string]$XrRuntime
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not $Python) {
    # A release package carries its own Python; a development checkout uses an installed one.
    $packagedPython=Join-Path $projectRoot 'python\python.exe'
    $bundledPython=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if(Test-Path -LiteralPath $packagedPython) { $Python=$packagedPython }
    elseif(Test-Path -LiteralPath $bundledPython) { $Python=$bundledPython }
    else { $Python=(Get-Command python.exe -ErrorAction Stop).Source }
}
foreach($name in @('spidy_headset_probe.exe','spidy_bridge.dll','spidy_render_probe.dll',
                   'spidy_ray_bridge.dll','spidy_movement_bridge.dll','spidy_stereo_probe.dll',
                   'spidy_render_memory.dll')) {
    $requiredFile=Join-Path $projectRoot "build\windows-ninja\$name"
    if(-not(Test-Path -LiteralPath $requiredFile)) { throw 'Build first with tools\build.ps1 -Observer.' }
}
if($Seconds -eq 1 -or ($Size -gt 0 -and $Size -lt 64)) { throw 'Use 0 or 2..25 seconds and 0 or 64..8192 pixels.' }
if($Size -and $RenderScale -ne 100) { throw 'Use -Size or -RenderScale, not both.' }
if(($EyeHeight -eq 0) -ne ($ArmLength -eq 0) -or ($EyeHeight -and ($EyeHeight -lt 1000 -or $ArmLength -lt 250))) {
    throw 'Give both -EyeHeight (1000..2500 mm) and -ArmLength (250..1200 mm) from a T-pose calibration, or neither.'
}
# The button that shoots and holds webs, and the one that reels them in and shoots web balls.
$web='grip'; $reel='trigger'
if($TriggerWebs) { $web='trigger'; $reel='grip' }
Write-Host 'Spidy VR - connect your headset before starting (Quest 3: Virtual Desktop or Steam Link; SteamVR headsets: start SteamVR).'
if($XrRuntime) { Write-Host "VR runtime: $XrRuntime." }
else { Write-Host 'VR runtime: the one your headset is connected to (add -XrRuntime <manifest> to choose).' }
if($Size) { Write-Host "Eye resolution override: $Size x $Size." }
elseif($RenderScale -ne 100) { Write-Host "Eye resolution: $RenderScale% of the VR runtime's recommendation, per side." }
else { Write-Host "Eye resolution: the VR runtime's recommendation (add -RenderScale 125 for sharper, slower images)." }
if($NoEyeAntialiasing) { Write-Host "Eyes: no anti-aliasing and no upscaling, as before October 10's eighth build (jagged and slower)." }
elseif($Upscale -eq 100) { Write-Host 'Eyes: anti-aliased, the scene rendered at their full size (-Upscale 67 is faster).' }
else { Write-Host "Eyes: the scene rendered at $Upscale% of their size per side, anti-aliased and upscaled (-Upscale 100: full size, slower)." }
if($Seconds) { Write-Host "Timed test: $Seconds seconds." }
else { Write-Host 'VR stays active until you close the game or press Ctrl+C here.' }
Write-Host 'VR starts with the game: its intro, menus, loading and cutscenes show on a screen in the headset.'
Write-Host 'There the controllers are an Xbox controller: thumbstick moves, A selects, B goes back, grips switch tabs.'
Write-Host 'In VR the menu button pauses and X opens the game menu (map, suits, skills).'
Write-Host "In VR B and Y are the game's own: B dodges the way the left stick points, Y interacts (backpacks,"
Write-Host 'doors, prompts; web strike in a fight). Look at the thing and press Y within about a metre of it;'
Write-Host 'hold Y and you step up to what you look at until the game takes it.'
Write-Host "Where the game shows LB + RB (a door or a grate to pull off), click the right thumbstick."
Write-Host 'Keep the game window in front on the desktop: the game pauses while another window is.'
Write-Host 'Settings > SPIDY VR > VIEW in the pause menu switches between immersive VR and a flat game screen.'
Write-Host "VR settings are in the game's own Settings: pause, Settings, then SPIDY VR (Up from GAME reaches it)."
Write-Host 'There change the web button, aim markers, webs in open air, swing speed, weight, your body calibration,'
Write-Host 'snap turn, smooth turn, vibration, screen size, the HUD, standing on walls, and the experimental flips and'
Write-Host 'flip speed during play.'
Write-Host "Press a $web to shoot that hand's web. Keep it held to swing; release it to let go."
Write-Host "Swing speed cap: $SwingSpeed m/s. Press the $reel while a web is attached to reel in."
if($TriggerWebs) { Write-Host 'Web button: the trigger (the grip reels in and shoots web balls).' }
else { Write-Host 'Web button: the grip (add -TriggerWebs to shoot webs with the trigger and reel with the grip).' }
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$report=Join-Path $projectRoot "reports\game-vr-$stamp.json"
$captureArgs=@()
if($CaptureImages) { $captureArgs=@('--capture-images') }
if($TriggerWebs) { $captureArgs+=@('--trigger-webs') }
if($ArcadeWebs) { $captureArgs+=@('--arcade-webs'); Write-Host 'Web style: arcade (webs go through lamp posts, signs and cars; swings stay above the street).' }
if($NoStandOnWalls) { $captureArgs+=@('--no-stand-on-walls'); Write-Host 'Stand on walls off: on a wall your view stays upright.' }
if($NoWallRun) { $captureArgs+=@('--no-wall-run'); Write-Host "Wall running off: the game's own wall crawl takes you at a wall." }
if($OverlayWebs) { $captureArgs+=@('--overlay-webs'); Write-Host 'Webs: Spidy overlay strands.' }
else { Write-Host "Webs: the game's own web lines (add -OverlayWebs for Spidy's strands)." }
if($NoWebGrab) { $captureArgs+=@('--no-web-grab'); Write-Host 'Web grab off: webs only swing.' }
else {
    Write-Host "A web aimed at a throwable prop or a thug catches it: the $reel reels it in, a sharp pull yanks"
    Write-Host "it to your hand, release the $web to throw (add -NoWebGrab to only swing)."
}
if($NoAirWebs) {
    $captureArgs+=@('--no-air-webs')
    Write-Host 'Webs in open air off: a web that meets nothing within 100 m misses.'
}
else { Write-Host 'A web that meets nothing within 100 m holds in open air there (add -NoAirWebs to make it miss).' }
if($NoBody) {
    $captureArgs+=@('--no-body')
    Write-Host 'Body off: the hero stays hidden in VR and gloves are drawn over the image.'
}
else { Write-Host "Your body: Spider-Man's, following your head and hands (add -NoBody for gloves)." }
if($NoPunch) { $captureArgs+=@('--no-punch'); Write-Host 'Punching off: fists pass through thugs.' }
else { Write-Host 'Punch a thug: a fist that hits him fast enough knocks him back (add -NoPunch to turn it off).' }
if($NoWebShooter) { $captureArgs+=@('--no-web-shooter'); Write-Host "Web shooter off: the $reel of a free hand shoots nothing." }
else {
    Write-Host "Press the $reel of a hand without a web to shoot the game's web balls where it points; a thug"
    Write-Host 'near that line takes it (add -NoWebShooter to turn it off).'
}
if($NoPromptHelp) {
    $captureArgs+=@('--no-prompt-help')
    Write-Host "Prompt help off: Spidy leaves the game's button prompts alone and the interact button is its Y."
}
else {
    Write-Host 'Where the game asks for a button, the interact button (Y) sends whatever it asks for, and the'
    Write-Host 'prompt names your VR control (add -NoPromptHelp to leave the prompts alone). Clicking both'
    Write-Host "thumbsticks latches the whole pad: grips as the game's LB and RB, triggers as its LT and RT."
}
if($NoAimMarkers) {
    $captureArgs+=@('--no-aim-markers')
    Write-Host "Aim markers hidden at the start: AIM MARKERS in Settings > SPIDY VR shows where each hand's web would land."
}
else { Write-Host "Aim markers show where each hand's web would land (add -NoAimMarkers to start hidden)." }
if($Flips) {
    $captureArgs+=@('--flips')
    Write-Host 'Flips (experimental) on: in the air the left stick turns you over; tap A for one flip, hold A to stay turned.'
}
if($EyeHeight) {
    $captureArgs+=@('--eye-height',$EyeHeight,'--arm-length',$ArmLength)
    Write-Host "Your body: eye height $EyeHeight mm, arm $ArmLength mm from a T-pose calibration."
}
elseif($NoCalibrationPrompt) {
    $captureArgs+=@('--no-calibration-prompt')
    Write-Host "Your size comes from the headset's height (Settings > SPIDY VR > CALIBRATE BODY asks for a T-pose)."
}
else {
    Write-Host 'At the first gameplay stand in a T-pose and hold both triggers: Spider-Man takes your height and'
    Write-Host 'arm length (B skips it; Settings > SPIDY VR > CALIBRATE BODY does it again).'
}
if($NoEyeOcclusion) {
    $captureArgs+=@('--no-eye-occlusion')
    Write-Host 'Eye occlusion off: each eye draws everything in view, hidden or not (about half the frame rate).'
}
if($NoEyeAntialiasing) { $captureArgs+=@('--no-eye-antialiasing') }
if($StockMonitorView) {
    $captureArgs+=@('--stock-monitor-view')
    Write-Host 'Monitor: stock game camera. The game culls and shades for it, not your head.'
}
elseif($GameMonitorView) {
    $captureArgs+=@('--game-monitor-view')
    Write-Host "Monitor: the game's own view of your head in immersive VR (a third scene view: about a fifth fewer frames)."
}
elseif($MonitorRate) { Write-Host "Monitor: your left eye in immersive VR, $MonitorRate pictures a second (-MonitorRate 0: every frame, slower in the headset; -StockMonitorView: the stock camera)." }
else { Write-Host 'Monitor: your left eye in immersive VR, every frame (-MonitorRate 30 leaves the headset more).' }
if($NoEyeReflections) { $captureArgs+=@('--no-eye-reflections') }
if($OwnShadows) {
    $captureArgs+=@('--own-shadows')
    Write-Host "Shadows: each eye makes its own sun shadow maps, as before October 11 (more work for the processor)."
}
if($EyeLensDistances) {
    $captureArgs+=@('--eye-lens-distances')
    Write-Host "Distances: the eyes' own wide lenses, as before October 19 (detail and textures load at about half the flat game's distance)."
}
if($NoEyePacing) {
    $captureArgs+=@('--no-eye-pacing')
    Write-Host "Pacing: the game draws eye pairs as fast as it can, as before November 1 (pairs the headset never shows, uneven motion)."
}
if($FullDesktopView) {
    $captureArgs+=@('--full-desktop-view')
    Write-Host "Desktop window: the game's own display settings."
}
elseif(-not $AttachOnly) {
    Write-Host 'Desktop window: small while VR runs, to save GPU time (add -FullDesktopView to keep your settings).'
}
if(-not $AttachOnly) { $captureArgs+=@('--auto-launch') }
if($XrRuntime) { $captureArgs+=@('--xr-runtime',$XrRuntime) }
if($SnapTurn -ne 30) { Write-Host "Snap turn: $SnapTurn degrees (0: off)." }
if($SmoothTurn -ne 0) { Write-Host "Smooth turn: $SmoothTurn degrees a second (replaces snap turning)." }
if($Weight -ne 80) { Write-Host "Weight: $Weight% of real gravity while webs fly you." }
if($Haptics -ne 100) { Write-Host "Controller vibration: $Haptics%." }
if($Hud -ne 'Medium') { Write-Host "HUD: $Hud." }
if($ScreenDepth -ne 'Medium') { Write-Host "Game screen 3D (cutscenes): $ScreenDepth." }
if($FlipSpeed -ne 180) { Write-Host "Flip speed: $FlipSpeed degrees a second at full tilt of the left stick." }
$screenIndex=@{Small=0;Medium=1;Large=2}[$ScreenSize]
$hudIndex=@{Off=0;Small=1;Medium=2;Large=3}[$Hud]
$depthIndex=@{Off=0;Low=1;Medium=2;High=3}[$ScreenDepth]
$captureArgs+=@('--snap-turn',$SnapTurn,'--smooth-turn',$SmoothTurn,'--haptics',$Haptics,'--screen-size',$screenIndex,
               '--render-scale',$RenderScale,'--upscale',$Upscale,'--weight',$Weight,'--hud',$hudIndex,
               '--flip-speed',$FlipSpeed,'--monitor-rate',$MonitorRate,'--screen-depth',$depthIndex)
& $Python (Join-Path $PSScriptRoot 'run_game_vr.py') --seconds $Seconds --size $Size --swing-speed $SwingSpeed --output $report @captureArgs
exit $LASTEXITCODE
