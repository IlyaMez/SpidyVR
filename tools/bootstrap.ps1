param([switch]$Observer)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$destination = Join-Path $root 'third_party\OpenXR-SDK'
$commit = '977f6675bc0057d5a54ed290cb5c71c699b1c0ab'
if (-not (Test-Path -LiteralPath $destination)) {
    git clone --depth 1 --branch release-1.1.49 https://github.com/KhronosGroup/OpenXR-SDK.git $destination
    if ($LASTEXITCODE) { throw 'OpenXR SDK download failed' }
}
$actual = git -c "safe.directory=$($destination.Replace('\','/'))" -C $destination rev-parse HEAD
if ($LASTEXITCODE -or $actual -ne $commit) { throw 'OpenXR SDK revision differs from the pinned revision. Existing files were preserved.' }
Write-Output "OpenXR SDK 1.1.49 verified: $actual"
# The launcher window (apps/launcher) is drawn with Dear ImGui.
$imguiDestination = Join-Path $root 'third_party\imgui'
$imguiCommit = 'f1cc2ae15e53a861a874c3034aae6798fde194ab'
if (-not (Test-Path -LiteralPath $imguiDestination)) {
    git clone --depth 1 --branch v1.92.9b https://github.com/ocornut/imgui.git $imguiDestination
    if ($LASTEXITCODE) { throw 'Dear ImGui download failed' }
}
$imguiActual = git -c "safe.directory=$($imguiDestination.Replace('\','/'))" -C $imguiDestination rev-parse HEAD
if ($LASTEXITCODE -or $imguiActual -ne $imguiCommit) { throw 'Dear ImGui revision differs from the pinned revision. Existing files were preserved.' }
Write-Output "Dear ImGui 1.92.9b verified: $imguiActual"
if ($Observer) {
    $hookDestination = Join-Path $root 'third_party\minhook'
    $hookCommit = 'c3fcafdc10146beb5919319d0683e44e3c30d537'
    if (-not (Test-Path -LiteralPath $hookDestination)) {
        git clone --depth 1 --branch v1.3.4 https://github.com/TsudaKageyu/minhook.git $hookDestination
        if ($LASTEXITCODE) { throw 'MinHook download failed' }
    }
    $hookActual = git -c "safe.directory=$($hookDestination.Replace('\','/'))" -C $hookDestination rev-parse HEAD
    if ($LASTEXITCODE -or $hookActual -ne $hookCommit) { throw 'MinHook revision differs from the pinned revision. Existing files were preserved.' }
    Write-Output "MinHook 1.3.4 verified: $hookActual"
}
