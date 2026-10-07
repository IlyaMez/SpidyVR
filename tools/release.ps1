# Publishes a Spidy release under the GitHub repository's Releases. It raises the version in
# CMakeLists.txt (the launcher, the zip and the tag all take it from there), commits that line
# alone, tags the commit v<version> and pushes both. The tag starts .github/workflows/release.yml,
# which builds the commit on a clean Windows machine, runs the tests and publishes
# Spidy-<version>-win64.zip. Nothing is built here, and uncommitted changes stay out of a release.
#   .\tools\release.ps1                  0.1.0 -> 0.1.1
#   .\tools\release.ps1 -Bump minor      0.1.1 -> 0.2.0 (-Bump major: 1.0.0)
#   .\tools\release.ps1 -Version 0.3.0   a chosen version; the current one is tagged without a commit
#   .\tools\release.ps1 -DryRun          check everything and show the plan, change nothing
#   .\tools\release.ps1 -Wait            then follow the build and print the release's address
param([ValidateSet('patch', 'minor', 'major')][string]$Bump = 'patch', [string]$Version, [switch]$DryRun, [switch]$Wait)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# git and gh print progress on stderr; only the exit code tells a failure.
function Invoke-Git {
    $ErrorActionPreference = 'Continue'
    $output = & git -C $root @args
    if ($LASTEXITCODE) { throw "git $args failed (exit code $LASTEXITCODE)." }
    if ($null -ne $output) { $output }
}
function Invoke-Gh {
    $ErrorActionPreference = 'Continue'
    $output = & gh @args
    if (-not $LASTEXITCODE -and $null -ne $output) { $output }
}

if ($Version -and $PSBoundParameters.ContainsKey('Bump')) { throw 'Give -Bump or -Version, not both.' }
$branch = Invoke-Git rev-parse --abbrev-ref HEAD
if ($branch -ne 'main') { throw "Releases are made from main; this checkout is on $branch." }
Invoke-Git fetch --quiet --tags origin main
$behind = [int](Invoke-Git rev-list --count HEAD..origin/main)
if ($behind) { throw "origin/main has $behind commit(s) that this checkout lacks. Pull them first." }

$cmake = Join-Path $root 'CMakeLists.txt'
$text = [IO.File]::ReadAllText($cmake)
$found = [regex]::Match($text, 'project\(Spidy VERSION (\d+\.\d+\.\d+)')
if (-not $found.Success) { throw 'CMakeLists.txt has no "project(Spidy VERSION x.y.z" line.' }
$current = [version]$found.Groups[1].Value
if ($Version) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Give the version as major.minor.patch, such as 0.2.0 (not $Version)." }
    $next = [version]$Version
} elseif ($Bump -eq 'major') {
    $next = New-Object Version ($current.Major + 1), 0, 0
} elseif ($Bump -eq 'minor') {
    $next = New-Object Version $current.Major, ($current.Minor + 1), 0
} else {
    $next = New-Object Version $current.Major, $current.Minor, ($current.Build + 1)
}
$tag = "v$next"
$tagged = @(Invoke-Git tag --list 'v*' | Where-Object { $_ -match '^v\d+\.\d+\.\d+$' } | ForEach-Object { [version]$_.Substring(1) })
$newest = $tagged | Sort-Object | Select-Object -Last 1
if ($tagged -contains $next) { throw "$tag already exists. Choose another version." }
if ($next -lt $current) { throw "$next is below the current version, $current." }
if ($newest -and $next -lt $newest) { throw "$next is below the newest tag, v$newest." }
# The current version is released as it stands (no commit) only while it has no tag.
$raise = $next -ne $current
if ($raise) {
    & git -C $root diff --quiet HEAD -- CMakeLists.txt
    if ($LASTEXITCODE) { throw 'CMakeLists.txt has uncommitted changes. Commit or stash them first: the release commit holds the version line only.' }
}

if ($raise) { Write-Output "Spidy $current -> ${next}. Commit 'Release Spidy $next' (CMakeLists.txt), tag $tag, push to origin." }
else { Write-Output "Spidy ${next}. Tag $tag on the current commit, push it to origin." }
$outgoing = @(Invoke-Git log --format='  %h %s' origin/main..HEAD)
if ($outgoing.Count) { Write-Output "The push also publishes $($outgoing.Count) commit(s) not yet on GitHub:"; $outgoing }
$uncommitted = @(Invoke-Git status --porcelain).Count
if ($uncommitted) { Write-Output "$uncommitted changed file(s) are not committed and stay out of the release." }
if ($DryRun) { Write-Output 'Dry run: nothing changed.'; return }

if ($raise) {
    $updated = $text.Remove($found.Groups[1].Index, $found.Groups[1].Length).Insert($found.Groups[1].Index, "$next")
    [IO.File]::WriteAllText($cmake, $updated, (New-Object Text.UTF8Encoding $false))
    Invoke-Git commit --quiet -m "Release Spidy $next" -- CMakeLists.txt
}
Invoke-Git tag -a $tag -m "Spidy $next"
$commit = Invoke-Git rev-parse HEAD
try {
    Invoke-Git push --atomic --quiet origin main "refs/tags/$tag"
} catch {
    # Undo the tag and the version commit, so the release can simply run again.
    Invoke-Git tag -d $tag | Out-Null
    if ($raise -and (Invoke-Git rev-parse HEAD) -ne $commit) {
        throw "Nothing was released: the push failed. Undo the commit 'Release Spidy $next' by hand (newer commits follow it)."
    }
    if ($raise) {
        Invoke-Git reset --quiet --soft HEAD~1
        Invoke-Git checkout --quiet HEAD -- CMakeLists.txt
    }
    throw "Nothing was released: the push failed. The version is back to $current."
}
Write-Output "Pushed $tag. GitHub Actions builds it now and publishes Spidy $next under Releases."

$remote = Invoke-Git remote get-url origin
if ($remote -notmatch 'github\.com[:/]([^/]+/[^/]+?)(\.git)?/?$') { return }
$repo = $Matches[1]
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Write-Output "Follow it at https://github.com/$repo/actions"; return }
$run = $null
for ($try = 0; $try -lt 10 -and -not $run; $try++) {
    Start-Sleep -Seconds 3
    $run = Invoke-Gh run list -R $repo --workflow release.yml --branch $tag --limit 1 --json databaseId --jq '.[].databaseId'
}
if (-not $run) { Write-Output "Follow it at https://github.com/$repo/actions"; return }
Write-Output "Build: https://github.com/$repo/actions/runs/$run"
if (-not $Wait) { return }
& gh run watch $run -R $repo --exit-status
if ($LASTEXITCODE) {
    throw "The build failed, so nothing was published. A passing fault (a download): gh run rerun $run -R $repo. Otherwise fix it and release the next version."
}
Write-Output ('Released: ' + (Invoke-Gh release view $tag -R $repo --json url --jq .url))
