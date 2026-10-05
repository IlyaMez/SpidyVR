param([switch]$Probe,[switch]$SystemRuntime)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $root 'build\windows-ninja\spidy_xr_lab.exe'
if(-not(Test-Path -LiteralPath $exe)){throw 'Build first with tools\build.ps1'}
$previousRuntime=$env:XR_RUNTIME_JSON
try {
    if(-not $SystemRuntime) {
        $runtime=Join-Path $env:ProgramFiles 'Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json'
        if(-not(Test-Path -LiteralPath $runtime)){throw 'Virtual Desktop OpenXR runtime not found; use -SystemRuntime for the registered runtime.'}
        $env:XR_RUNTIME_JSON=$runtime
    }
    if($Probe){& $exe --probe}else{& $exe}
    $result=$LASTEXITCODE
} finally {$env:XR_RUNTIME_JSON=$previousRuntime}
exit $result
