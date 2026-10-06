# Builds dist\Spidy-<version>-win64.zip: the launcher, the game modules, the
# Python tools a VR session runs, an embeddable Python, and the notices.
# Players extract it anywhere and start "Spidy Launcher.exe"; nothing else to install.
param([switch]$KeepFolder)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$version = (Select-String -LiteralPath (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(Spidy VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$bin = Join-Path $root 'build\windows-ninja'
$modules = @('spidy_headset_probe.exe', 'spidy_bridge.dll', 'spidy_render_probe.dll', 'spidy_ray_bridge.dll',
             'spidy_movement_bridge.dll', 'spidy_stereo_probe.dll', 'spidy_render_memory.dll')
foreach ($name in $modules + 'spidy_launcher.exe') {
    if (-not (Test-Path -LiteralPath (Join-Path $bin $name))) {
        throw "Missing build\windows-ninja\$name. Run tools\bootstrap.ps1 -Observer, then tools\build.ps1 -Observer."
    }
}
# What tools\run_game_vr.py imports, directly or not (standard library otherwise).
$tools = @('run_game_vr', 'vr_launcher', 'vr_display', 'xr_runtime', 'inspect_game', 'bridge_game', 'capture_game_state',
           'capture_movement', 'capture_stereo', 'observe_game', 'probe_game_grab', 'probe_game_screen',
           'probe_game_swing', 'probe_menu_pad', 'probe_native_motion', 'probe_native_rays', 'probe_render',
           'probe_stereo', 'probe_stereo_gpu')

# Python 3.12.10 is the newest 3.12 with Windows binaries; its MD5 matches python.org's release page.
$pythonVersion = '3.12.10'
$pythonHash = '4ACBED6DD1C744B0376E3B1CF57CE906F9DC9E95E68824584C8099A63025A3C3'
$cache = Join-Path $root 'build\package-cache'
$pythonZip = Join-Path $cache "python-$pythonVersion-embed-amd64.zip"
if (-not (Test-Path -LiteralPath $pythonZip)) {
    New-Item -ItemType Directory -Force $cache | Out-Null
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest -UseBasicParsing "https://www.python.org/ftp/python/$pythonVersion/python-$pythonVersion-embed-amd64.zip" -OutFile $pythonZip
}
if ((Get-FileHash -LiteralPath $pythonZip -Algorithm SHA256).Hash -ne $pythonHash) {
    throw "The Python download does not match the pinned hash. Delete $pythonZip and try again."
}

$name = "Spidy-$version"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
foreach ($folder in @('build\windows-ninja', 'tools', 'python', 'docs\licenses')) {
    New-Item -ItemType Directory -Force (Join-Path $stage $folder) | Out-Null
}
Copy-Item -LiteralPath (Join-Path $bin 'spidy_launcher.exe') -Destination (Join-Path $stage 'Spidy Launcher.exe')
foreach ($module in $modules) { Copy-Item -LiteralPath (Join-Path $bin $module) -Destination (Join-Path $stage 'build\windows-ninja') }
foreach ($tool in $tools) { Copy-Item -LiteralPath (Join-Path $root "tools\$tool.py") -Destination (Join-Path $stage 'tools') }
Expand-Archive -LiteralPath $pythonZip -DestinationPath (Join-Path $stage 'python')
# The embeddable Python ignores the script's folder; the tools import each other.
Add-Content -LiteralPath (Join-Path $stage 'python\python312._pth') -Value '..\tools' -Encoding ascii
Copy-Item -LiteralPath (Join-Path $root 'docs\PLAYERS.md') -Destination (Join-Path $stage 'README.txt')
Copy-Item -LiteralPath (Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $stage
Copy-Item -Path (Join-Path $root 'docs\licenses\*') -Destination (Join-Path $stage 'docs\licenses')
Copy-Item -LiteralPath (Join-Path $root 'third_party\imgui\LICENSE.txt') -Destination (Join-Path $stage 'docs\licenses\DearImGui-LICENSE.txt')

# The packaged Python must find every module a session imports.
$check = & (Join-Path $stage 'python\python.exe') -B -X utf8 -c 'import run_game_vr, xr_runtime, probe_game_grab; print(7*6)'
if ($LASTEXITCODE -or "$check" -ne '42') { throw 'The packaged Python cannot load the tools (run the line above to see why).' }

$zip = Join-Path $dist "$name-win64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
if (-not $KeepFolder) { Remove-Item -LiteralPath $stage -Recurse -Force }
$size = (Get-Item -LiteralPath $zip).Length / 1MB
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
Write-Output ("Spidy {0}: {1} ({2:N1} MB)" -f $version, $zip, $size)
Write-Output "SHA-256: $hash"
