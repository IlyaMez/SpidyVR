param([switch]$CoreOnly,[switch]$Observer,[ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
# vcvars64.bat runs vswhere by name, and its folder is not on every PATH.
$env:PATH = (Split-Path -Parent $vswhere) + ';' + $env:PATH
$vs = & $vswhere -version '[17.0,18.0)' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
$build = if($CoreOnly){'core-ninja'}else{'windows-ninja'}
$xr = if($CoreOnly){'OFF'}else{'ON'}
$observerFlag = if($Observer){'ON'}else{'OFF'}
& (Join-Path $PSScriptRoot 'build-native.cmd') $vs $build $Configuration $xr $observerFlag
if ($LASTEXITCODE) { throw 'Build or tests failed' }

# Optional Authenticode signing of the shipped launcher and modules. A no-op unless a certificate is
# configured, so unsigned dev builds keep working; signing is what most reduces antivirus false
# positives (an unsigned binary that attaches to a game starts with no reputation). Configure one of:
#   $env:SPIDY_SIGN_THUMBPRINT  SHA-1 thumbprint of a code-signing cert in the Windows store
#                               (a hardware token, or an imported PFX)
#   $env:SPIDY_SIGN_PFX         path to a PFX file (+ $env:SPIDY_SIGN_PASSWORD if it has one)
#   $env:SPIDY_SIGN_ARGS        raw signtool 'sign' arguments, for schemes the two above do not cover
#                               (e.g. Azure Trusted Signing's '/v /debug /dlib <dll> /dmdf <json>')
# Override the RFC3161 timestamp server with $env:SPIDY_SIGN_TIMESTAMP_URL (timestamping keeps the
# signature valid after the certificate expires).
if (-not $CoreOnly -and ($env:SPIDY_SIGN_THUMBPRINT -or $env:SPIDY_SIGN_PFX -or $env:SPIDY_SIGN_ARGS)) {
    $bin = Join-Path (Split-Path -Parent $PSScriptRoot) "build\$build"
    $names = @('spidy_launcher.exe', 'spidy_headset_probe.exe', 'spidy_bridge.dll', 'spidy_render_probe.dll',
               'spidy_ray_bridge.dll', 'spidy_movement_bridge.dll', 'spidy_stereo_probe.dll', 'spidy_render_memory.dll')
    $files = foreach ($n in $names) { $p = Join-Path $bin $n; if (Test-Path -LiteralPath $p) { $p } }
    if (-not $files) { throw "Signing was requested but no built binaries were found in $bin." }

    $signtool = (Get-Command signtool.exe -ErrorAction SilentlyContinue).Source
    if (-not $signtool) {
        $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
        $signtool = Get-ChildItem -LiteralPath $kits -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName |
            Select-Object -Last 1 -ExpandProperty FullName
    }
    if (-not $signtool) { throw 'signtool.exe not found. Install the Windows SDK (it ships signtool.exe).' }

    if ($env:SPIDY_SIGN_ARGS) {
        $signArgs = @('sign') + ($env:SPIDY_SIGN_ARGS -split '\s+' | Where-Object { $_ })
    } else {
        $ts = if ($env:SPIDY_SIGN_TIMESTAMP_URL) { $env:SPIDY_SIGN_TIMESTAMP_URL } else { 'http://timestamp.digicert.com' }
        $signArgs = @('sign', '/fd', 'sha256', '/tr', $ts, '/td', 'sha256')
        if ($env:SPIDY_SIGN_THUMBPRINT) {
            $signArgs += @('/sha1', $env:SPIDY_SIGN_THUMBPRINT)
        } else {
            $signArgs += @('/f', $env:SPIDY_SIGN_PFX)
            if ($env:SPIDY_SIGN_PASSWORD) { $signArgs += @('/p', $env:SPIDY_SIGN_PASSWORD) }
        }
    }
    & $signtool @signArgs @files
    if ($LASTEXITCODE) { throw 'signtool failed to sign the binaries.' }
    & $signtool verify /pa @files | Out-Null
    if ($LASTEXITCODE) { throw 'The signed binaries did not verify.' }
    Write-Output "Signed and verified $($files.Count) binaries."
}
