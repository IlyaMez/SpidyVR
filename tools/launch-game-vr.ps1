param(
    [ValidateRange(0,25)][int]$Seconds=0,
    [ValidateRange(0,4096)][int]$Size=0,
    [ValidateRange(1,65)][float]$SwingSpeed=32,
    [string]$Python,
    [switch]$CaptureImages,
    [switch]$OverlayWebs,
    [switch]$NoWebGrab,
    [switch]$NoBody,
    [switch]$NoPunch,
    [switch]$NoEyeOcclusion,
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
if($Seconds -eq 1 -or ($Size -gt 0 -and $Size -lt 64)) { throw 'Use 0 or 2..25 seconds and 0 or 64..4096 pixels.' }
Write-Host 'Spidy VR - connect your headset (Quest 3: in Virtual Desktop) before starting.'
if($Size) { Write-Host "Eye resolution override: $Size x $Size." }
else { Write-Host 'Eye resolution: Virtual Desktop / Quest runtime recommendation.' }
if($Seconds) { Write-Host "Timed test: $Seconds seconds." }
else { Write-Host 'VR stays active until you close the game or press Ctrl+C here.' }
Write-Host 'VR starts with the game: its intro, menus, loading and cutscenes show on a screen in the headset.'
Write-Host 'There the controllers are an Xbox controller: thumbstick moves, A selects, B goes back, grips switch tabs.'
Write-Host 'In VR the menu button pauses and Y opens the game menu (map, suits, skills).'
Write-Host 'Keep the game window in front on the desktop: the game pauses while another window is.'
Write-Host 'Click both thumbsticks to switch between immersive VR and a flat game screen in the headset.'
Write-Host "Squeeze a grip to shoot that hand's web. Keep it held to swing; release it to let go."
Write-Host "Swing speed cap: $SwingSpeed m/s. Pull the trigger while a web is attached to reel in."
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$report=Join-Path $projectRoot "reports\game-vr-$stamp.json"
$captureArgs=@()
if($CaptureImages) { $captureArgs=@('--capture-images') }
if($OverlayWebs) { $captureArgs+=@('--overlay-webs'); Write-Host 'Webs: Spidy overlay strands.' }
else { Write-Host "Webs: the game's own web lines (add -OverlayWebs for Spidy's strands)." }
if($NoWebGrab) { $captureArgs+=@('--no-web-grab'); Write-Host 'Web grab off: webs only swing.' }
else {
    Write-Host 'A web aimed at a throwable prop or a thug catches it: trigger reels it in, a sharp pull yanks'
    Write-Host 'it to your hand, release the grip to throw (add -NoWebGrab to only swing).'
}
if($NoBody) {
    $captureArgs+=@('--no-body')
    Write-Host 'Body off: the hero stays hidden in VR and gloves are drawn over the image.'
}
else { Write-Host "Your body: Spider-Man's, following your head and hands (add -NoBody for gloves)." }
if($NoPunch) { $captureArgs+=@('--no-punch'); Write-Host 'Punching off: fists pass through thugs.' }
else { Write-Host 'Punch a thug: a fist that hits him fast enough knocks him back (add -NoPunch to turn it off).' }
if($NoEyeOcclusion) {
    $captureArgs+=@('--no-eye-occlusion')
    Write-Host 'Eye occlusion off: each eye draws everything in view, hidden or not (about half the frame rate).'
}
if($StockMonitorView) {
    $captureArgs+=@('--stock-monitor-view')
    Write-Host 'Monitor: stock game camera. The game culls and shades for it, not your head.'
}
else { Write-Host 'Monitor: your head view in immersive VR (add -StockMonitorView for the stock camera).' }
if($FullDesktopView) {
    $captureArgs+=@('--full-desktop-view')
    Write-Host "Desktop window: the game's own display settings."
}
elseif(-not $AttachOnly) {
    Write-Host 'Desktop window: small while VR runs, to save GPU time (add -FullDesktopView to keep your settings).'
}
if(-not $AttachOnly) { $captureArgs+=@('--auto-launch') }
if($XrRuntime) { $captureArgs+=@('--xr-runtime',$XrRuntime) }
& $Python (Join-Path $PSScriptRoot 'run_game_vr.py') --seconds $Seconds --size $Size --swing-speed $SwingSpeed --output $report @captureArgs
exit $LASTEXITCODE
