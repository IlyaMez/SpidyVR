param(
    [ValidateRange(0,25)][int]$Seconds=0,
    [ValidateRange(0,4096)][int]$Size=0,
    [ValidateRange(1,65)][float]$SwingSpeed=32,
    [string]$Python,
    [switch]$CaptureImages,
    [switch]$OverlayWebs,
    [switch]$StockMonitorView,
    [switch]$FullDesktopView,
    [switch]$AttachOnly
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not $Python) {
    $bundledPython=Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
    if(Test-Path -LiteralPath $bundledPython) { $Python=$bundledPython }
    else { $Python=(Get-Command python.exe -ErrorAction Stop).Source }
}
foreach($name in @('spidy_headset_probe.exe','spidy_bridge.dll','spidy_render_probe.dll',
                   'spidy_ray_bridge.dll','spidy_movement_bridge.dll','spidy_stereo_probe.dll')) {
    $requiredFile=Join-Path $projectRoot "build\windows-ninja\$name"
    if(-not(Test-Path -LiteralPath $requiredFile)) { throw 'Build first with tools\build.ps1 -Observer.' }
}
if($Seconds -eq 1 -or ($Size -gt 0 -and $Size -lt 64)) { throw 'Use 0 or 2..25 seconds and 0 or 64..4096 pixels.' }
Write-Host 'Spidy VR - connect Quest 3 in Virtual Desktop before starting.'
if($Size) { Write-Host "Eye resolution override: $Size x $Size." }
else { Write-Host 'Eye resolution: Virtual Desktop / Quest runtime recommendation.' }
if($Seconds) { Write-Host "Timed test: $Seconds seconds." }
else { Write-Host 'VR stays active until you close the game or press Ctrl+C here.' }
Write-Host 'Click both thumbsticks to switch between immersive VR and a flat game screen in the headset.'
Write-Host 'Keep grip held to swing. Trigger + grip attaches; release grip to let go.'
Write-Host "Swing speed cap: $SwingSpeed m/s. Release and press trigger again while gripping to reel."
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
$report=Join-Path $projectRoot "reports\game-vr-$stamp.json"
$captureArgs=@()
if($CaptureImages) { $captureArgs=@('--capture-images') }
if($OverlayWebs) { $captureArgs+=@('--overlay-webs'); Write-Host 'Webs: Spidy overlay strands.' }
else { Write-Host "Webs: the game's own web lines (add -OverlayWebs for Spidy's strands)." }
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
& $Python (Join-Path $PSScriptRoot 'run_game_vr.py') --seconds $Seconds --size $Size --swing-speed $SwingSpeed --output $report @captureArgs
exit $LASTEXITCODE
