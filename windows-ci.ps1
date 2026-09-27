#	Copyright 2026 İlyas Akın
#	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
#
#	This program is free software: you can redistribute it and/or modify
#	it under the terms of the GNU General Public License as published by
#	the Free Software Foundation, either version 3 of the License, or
#	(at your option) any later version.
#
#	This program is distributed in the hope that it will be useful,
#	but WITHOUT ANY WARRANTY; without even the implied warranty of
#	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#	GNU General Public License for more details.
#
#	You should have received a copy of the GNU General Public License
#	along with this program.  If not, see <http://www.gnu.org/licenses/>.
<#
.SYNOPSIS
  The Windows check for a merge, in one command: build, the ctest suite, the GPU tests, and E1's replay CRCs.

.DESCRIPTION
  What the M3 Pro Mac's post-merge run is for the Mac, for Windows (W2). In order:
    1. build.bat -Config (skip with -SkipBuild), then ZH_GAME_DATA set on the build tree when -DataDir is given;
    2. ctest, minus the audio and video tests (no sound on a worker) and, in session 0, minus the GPU tests;
    3. the desktop part: the GPU tests, then replay-check.ps1 for each E1 run on a farm of the data. The runs
       are -Seeds at -MaxFrames, or -Runs "seed@frames" pairs when the seeds need different lengths.

  Session 0 (an ssh or service session) has no display, so no Direct3D device, and Windows -headless still
  makes one. Started there, the script runs its desktop part again in the logged-on user's interactive
  session through a one-off scheduled task (schtasks /it), waits for it, reads its result and deletes the
  task. Started on the desktop, it runs that part itself. Either way it needs a user logged on to a desktop.

  RULE 9: the game never runs in the data folder. It runs in -WorkDir\farm: a symbolic link to every file
  of -DataDir\zerohour, with the build's GeneralsMD\Run over them (the exe and DLLs copied), as Windows'
  one-folder layout has the fork's files over the install. Every file of the data folder is hashed before
  the farm is made and again after the runs, and a difference fails the check. Symbolic links need an
  elevated session or Developer Mode.

  -ExpectCrc "0@1200:0x0177BEF6","1@12000:0x830467DB" fails the check when a run's recorded CRC is another
  one: that is the cross-platform comparison ("seed:crc" means that seed at -MaxFrames). Without it the
  check only asks that each replay plays back to the same world. Pin -ExpectCrc to the commit the numbers
  were made on: upstream gameplay data moves them.

  -Bundle <file> -Ref <branch> first fetches that branch from a git bundle into this worktree and checks it
  out (detached), so a merge can be checked from another machine: make the bundle there, copy it here, run
  this. -Ref alone checks out a ref the worktree already has. PowerShell parsed this script before it ran,
  so the process that checked out keeps running the OLD version: it hands over at once to the checked-out
  one, with the same arguments, and exits with its status.

.EXAMPLE
  .\windows-ci.ps1 -DataDir C:\work\data -Runs "0@1200","1@12000" -ExpectCrc "0@1200:0x0177BEF6","1@12000:0x830467DB"
    (feature/mac-port from 9f17c203, upstream's merge; seed 0 is at 1200 until defect #34's fix, which
    crashes it at 12000 on every platform)
  .\windows-ci.ps1 -DataDir C:\work\data -SkipBuild -Seeds 0 -MaxFrames 1200
  .\windows-ci.ps1 -Bundle C:\work\bundles\fmp.bundle -Ref feature/mac-port -DataDir C:\work\data

  Exit status: 0 when everything passed, 1 otherwise. The summary says which part failed.
#>
param(
	# a folder holding zerohour\ (a Zero Hour install) and generals\ (the base game); without it the data
	# tests skip and E1 does not run
	[string] $DataDir = "",
	[string] $Config = "Release",
	[switch] $SkipBuild,
	[string[]] $Seeds = @("0", "1"),
	[int] $MaxFrames = 12000,
	# "seed@frames" pairs, in place of -Seeds at -MaxFrames
	[string[]] $Runs = @(),
	# "seed:0xCRC" pairs the recorded CRCs must equal
	[string[]] $ExpectCrc = @(),
	# check out this ref first: from -Bundle when one is given, otherwise one this worktree already has
	[string] $Bundle = "",
	[string] $Ref = "",
	[string] $WorkDir = (Join-Path $env:TEMP "zh-windows-ci"),
	# internal: "desktop" when the script is running its desktop part, and where that part writes its result
	[string] $Phase = "all",
	[string] $CheckedOut = "",	# internal: what the -Bundle/-Ref hand-over checked out, for the summary
	[string] $ResultFile = ""
)

# Continue, not Stop: Windows PowerShell 5.1 turns a native command's stderr, under 2>&1, into a terminating
# error, and ctest writes "Errors while running CTest" there. Failures are counted by exit status instead.
$ErrorActionPreference = "Continue"
# through powershell -File an array arrives as one comma-joined string: -Seeds, -Runs and -ExpectCrc alike
$Seeds = @($Seeds | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" } | ForEach-Object { [int]$_ })
# the E1 runs as "seed@frames"
$Runs = @($Runs | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" })
if ($Runs.Count -eq 0) { $Runs = @($Seeds | ForEach-Object { "$_@$MaxFrames" }) }
$ExpectCrc = @($ExpectCrc | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" } |
	ForEach-Object { $k, $v = $_.Split(':', 2); if ($k -notmatch '@') { $k = "$k@$MaxFrames" }; "${k}:$v" })
$Root = $PSScriptRoot
$Build = Join-Path $Root "build64"
$RunDir = Join-Path $Root "GeneralsMD\Run"
$NoSound = "test_milesaudiomanager|miles_smoke|test_miles_miniaudio|test_binkvideo|bink_smoke"
$GpuTests = "^(dx9_smoke|dx9_smoke_msaa|test_dx11device)$"

function Get-CtestExe {
	$cache = Join-Path $Build "CMakeCache.txt"
	$line = Select-String -Path $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$' | Select-Object -First 1
	if (-not $line) { throw "no CMAKE_COMMAND in ${cache}: build first" }
	$cmake = $line.Matches[0].Groups[1].Value
	return @{ CMake = $cmake; CTest = (Join-Path (Split-Path $cmake) "ctest.exe") }
}

# every file of a folder: its relative path, size and SHA-256, sorted - equal listings mean an equal folder
function Get-TreeListing([string] $dir) {
	Get-ChildItem -LiteralPath $dir -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
		"{0} {1} {2}" -f $_.FullName.Substring($dir.Length + 1), $_.Length, (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
	}
}

function New-Farm([string] $data, [string] $farm) {
	if (Test-Path $farm) { cmd /c "rmdir /s /q `"$farm`"" | Out-Null }		# links go, never their targets
	New-Item -ItemType Directory $farm | Out-Null
	Get-ChildItem -LiteralPath $data -Recurse -File | Where-Object { $_.Name -notlike '._*' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($data.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		New-Item -ItemType SymbolicLink -Path $dst -Target $_.FullName | Out-Null
	}
	Get-ChildItem -LiteralPath $RunDir -Recurse -File | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($RunDir.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }	# the link, never its target
		if ($_.Extension -in '.exe', '.dll') { Copy-Item -LiteralPath $_.FullName $dst }
		else { New-Item -ItemType SymbolicLink -Path $dst -Target $_.FullName | Out-Null }
	}
}

# ---- the desktop part: GPU tests and E1, returning a result object -------------------------------------
function Invoke-DesktopPart {
	$r = [ordered]@{ Gpu = "not run"; E1 = "not run"; Crcs = @{}; DataUnchanged = $null; Log = @() }
	$tools = Get-CtestExe
	Push-Location $Build
	$gpu = & $tools.CTest -C $Config -R $GpuTests --output-on-failure 2>&1
	Pop-Location
	$r.Log += $gpu
	$r.Gpu = if ($LASTEXITCODE -eq 0) { "passed" } else { "FAILED" }

	if ($DataDir -ne "") {
		$zh = Join-Path $DataDir "zerohour"
		New-Item -ItemType Directory -Force $WorkDir | Out-Null
		$before = @(Get-TreeListing $zh)
		$farm = Join-Path $WorkDir "farm"
		New-Farm $zh $farm
		$failures = 0
		foreach ($run in $Runs) {
			$seed, $frames = $run.Split('@')
			$out = & (Join-Path $Root "replay-check.ps1") -RunDir $farm -Seeds ([int]$seed) -MaxFrames ([int]$frames) *>&1 | ForEach-Object { "$_" }
			$failures += $LASTEXITCODE
			$r.Log += $out
			foreach ($line in $out) {
				if ($line -match 'frame (\d+), CRC (0x[0-9A-Fa-f]+)') { $r.Crcs[$run] = $Matches[2]; break }
			}
		}
		$r.Log | Out-File -Encoding utf8 (Join-Path $WorkDir "e1.log")
		$r.E1 = if ($failures -eq 0 -and $r.Crcs.Count -eq $Runs.Count) { "played back the same" } else { "FAILED (the output: $(Join-Path $WorkDir 'e1.log'))" }
		$after = @(Get-TreeListing $zh)
		$r.DataUnchanged = ($before.Count -gt 0) -and (($before -join "`n") -eq ($after -join "`n"))
	}
	return $r
}

if ($Phase -eq "desktop") {
	# written whole under another name, then renamed: the main part waits for this name, and Out-File would
	# create it empty at the start of the pipeline
	$json = Invoke-DesktopPart | ConvertTo-Json -Depth 4
	$json | Out-File -Encoding utf8 "$ResultFile.partial"
	Move-Item -Force "$ResultFile.partial" $ResultFile
	exit 0
}

# ---- the main part --------------------------------------------------------------------------------------
$summary = @()
$failed = $false

if ($Bundle -ne "" -or $Ref -ne "") {
	if ($Ref -eq "") { Write-Host "-Bundle needs -Ref, the branch to take from it"; exit 1 }
	if ($Bundle -ne "") {
		git -C $Root fetch -q -f $Bundle "${Ref}:refs/remotes/ci/${Ref}"
		if ($LASTEXITCODE -ne 0) { Write-Host "could not fetch $Ref from $Bundle"; exit 1 }
		$target = "refs/remotes/ci/${Ref}"
	} else { $target = $Ref }
	git -C $Root checkout -q --detach $target
	if ($LASTEXITCODE -ne 0) { Write-Host "could not check out $target (uncommitted changes in $Root?)"; exit 1 }
	# hand over to the version just checked out (this process runs the one it was started as)
	$again = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath)
	foreach ($k in $PSBoundParameters.Keys) {
		if ($k -in 'Bundle', 'Ref') { continue }
		$v = $PSBoundParameters[$k]
		if ($v -is [System.Management.Automation.SwitchParameter]) { if ($v.IsPresent) { $again += "-$k" } }
		else { $again += "-$k"; $again += (@($v) -join ',') }
	}
	$again += '-CheckedOut'; $again += ("`"" + (git -C $Root log --oneline -1) + "`"")
	& powershell.exe @again
	exit $LASTEXITCODE
}
if ($CheckedOut -ne "") { $summary += "checked out: $CheckedOut" }

if (-not $SkipBuild) {
	# The last build's exe goes first: a build that fails must leave nothing the desktop part could run.
	Remove-Item (Join-Path $RunDir "generals.exe") -ErrorAction SilentlyContinue
	Push-Location $Root
	# not $phase: PowerShell names ignore case, and that would be this script's -Phase, a [string].  A
	# stopwatch, not Get-Date: the VM's wall clock was once stepped back 7 h in the middle of a gate.
	$phaseClock = [Diagnostics.Stopwatch]::StartNew()
	cmd /c "build.bat $Config < NUL" | Tee-Object -Variable buildOut | Out-Host
	$built = $LASTEXITCODE
	Pop-Location
	# how much it rebuilt: MSBuild names each source it compiles on a line of its own
	$compiled = @($buildOut | Where-Object { "$_" -match '^\s+[\w\.\-]+\.(cpp|c|cc|cxx)$' }).Count
	$took = "$([int]$phaseClock.Elapsed.TotalSeconds) s, $compiled source(s) compiled"
	$summary += "build: " + $(if ($built -eq 0) { "ok ($took)" } else { $failed = $true; "FAILED (exit $built; $took)" })
	if ($built -ne 0) {
		$summary += "ctest, GPU tests: not run (build failed)"
		if ($DataDir -ne "") { $summary += "E1: not run (build failed)" }
		Write-Host ""
		$summary | ForEach-Object { Write-Host $_ }
		Write-Host "WINDOWS CHECK FAILED"
		exit 1
	}
}
$tools = Get-CtestExe
if ($DataDir -ne "") { & $tools.CMake -S (Join-Path $Root "GeneralsMD\Code") -B $Build "-DZH_GAME_DATA=$DataDir" | Out-Null }

$session0 = (Get-Process -Id $PID).SessionId -eq 0
Push-Location $Build
$exclude = if ($session0) { "$NoSound|dx9_smoke|dx9_smoke_msaa|test_dx11device" } else { $NoSound }
$phaseClock = [Diagnostics.Stopwatch]::StartNew()
$ctestOut = & $tools.CTest -C $Config -j4 --timeout 900 --output-on-failure -E $exclude 2>&1
$ctestTook = "$([int]$phaseClock.Elapsed.TotalSeconds) s"
$ctestExit = $LASTEXITCODE
Pop-Location
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$ctestOut | ForEach-Object { "$_" } | Out-File -Encoding utf8 (Join-Path $WorkDir "ctest.log")		# the failing tests' own output
$summary += "ctest: " + $(if ($ctestExit -eq 0) { "passed ($ctestTook)" } else { $failed = $true; "FAILED ($ctestTook; the output: $(Join-Path $WorkDir 'ctest.log'))" })
$ctestOut | Select-String -Pattern 'tests passed|\*\*\*' | ForEach-Object { $summary += "  " + $_.Line.Trim() }

# the desktop part: here, or in the interactive session through a one-off task
$phaseClock = [Diagnostics.Stopwatch]::StartNew()
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$resultFile = Join-Path $WorkDir "desktop-result.json"
Remove-Item $resultFile, "$resultFile.partial" -ErrorAction SilentlyContinue
$argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Phase desktop -ResultFile `"$resultFile`" -Config $Config -MaxFrames $MaxFrames -WorkDir `"$WorkDir`" -Runs $($Runs -join ',')"
if ($DataDir -ne "") { $argList += " -DataDir `"$DataDir`"" }
if ($session0) {
	$user = (Get-CimInstance Win32_ComputerSystem).UserName
	if (-not $user) { Write-Host "no user is logged on to a desktop: the GPU tests and E1 cannot run"; exit 1 }
	$task = "zh-windows-ci-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
	# schtasks /tr takes at most 261 characters, so the task runs a one-line wrapper
	$wrapper = Join-Path $WorkDir "desktop-part.cmd"
	"@powershell.exe $argList" | Out-File -Encoding ascii $wrapper
	schtasks /create /tn $task /tr "`"$wrapper`"" /sc once /st 23:59 /it /ru $user /f | Out-Null
	schtasks /run /tn $task | Out-Null
	$waitClock = [Diagnostics.Stopwatch]::StartNew()		# not the wall clock: a VM's can step by hours
	while (-not (Test-Path $resultFile) -and $waitClock.Elapsed.TotalMinutes -lt 90) { Start-Sleep -Seconds 10 }
	Start-Sleep -Seconds 2
	schtasks /delete /tn $task /f | Out-Null
} else {
	& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Phase desktop -ResultFile $resultFile -Config $Config -MaxFrames $MaxFrames -WorkDir $WorkDir -Runs ($Runs -join ',') -DataDir $DataDir
}
$desktopTook = "$([int]$phaseClock.Elapsed.TotalSeconds) s"
if (-not (Test-Path $resultFile)) {
	$summary += "desktop part: NO RESULT (it did not finish, $desktopTook)"; $failed = $true
} else {
	$summary += "desktop part (GPU tests and E1): $desktopTook"
	$d = Get-Content -Raw $resultFile | ConvertFrom-Json
	$summary += "GPU tests (desktop session): $($d.Gpu)"; if ($d.Gpu -ne "passed") { $failed = $true }
	if ($DataDir -ne "") {
		$summary += "E1: $($d.E1)"; if ($d.E1 -ne "played back the same") { $failed = $true }
		foreach ($run in $Runs) { $s, $f = $run.Split('@'); $summary += "  seed $s at frame ${f}: $($d.Crcs.$run)" }
		foreach ($pair in $ExpectCrc) {
			$run, $want = $pair.Split(':', 2)
			$got = $d.Crcs."$run"
			if ($got -ne $want) { $summary += "  ${run}: EXPECTED $want, got $got"; $failed = $true }
			else { $summary += "  ${run}: as expected" }
		}
		$summary += "data folder: " + $(if ($d.DataUnchanged) { "unchanged" } else { $failed = $true; "CHANGED OR NOT CHECKED" })
	}
}
Write-Host ""
$summary | ForEach-Object { Write-Host $_ }
Write-Host $(if ($failed) { "WINDOWS CHECK FAILED" } else { "WINDOWS CHECK PASSED" })
exit $(if ($failed) { 1 } else { 0 })
