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
