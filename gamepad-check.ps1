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
  The gamepad's checks on Windows (G1b): the same scripted games as off Windows, pad against hand.

.DESCRIPTION
  One runner for the three checks in GeneralsMD\Code\Tools: gamepad-crc-check.sh (a skirmish played by a
  virtual pad and by hand ends on the same CRC; then the radial menu and the base chord), gamepad-combo-check.sh
  (a combo box picked with the pad is the mouse's pick) and gamepad-menu-check.sh (the pad walks the menus).
  The checks themselves are not written again here: each runs as it is, in Git for Windows' bash, with its
  game started through gamepad-game.sh's Windows branch - generals.exe in the farm this makes, in a window.
  The scripts, the runs, the logs they read and their verdicts are the ones every other platform runs.

  What this adds is Windows': the desktop session (a Direct3D device needs one, as for windows-ci.ps1's GPU
  tests: started in session 0, it runs its desktop part through a one-off scheduled task in the classic
  console, and waits for it), the farm (RULE 9: hard links to -DataDir\zerohour with this worktree's
  GeneralsMD\Run over them, the exe and DLLs copied), and a time-out on each check.  Each check lists the
  install before and after its runs itself (install-guard.sh).  Nothing here has a sound device to use:
  the games are started with -noaudio.

.EXAMPLE
  .\gamepad-check.ps1 -DataDir D:\ZeroHourData
  .\gamepad-check.ps1 -DataDir D:\ZeroHourData -Checks combo -DllPath D:\x64-dlls

  Exit status: 0 when every check passed, 1 otherwise.
#>
param(
	# a folder holding zerohour\ (a Zero Hour install) and generals\ (the base game)
	[string] $DataDir = "",
	# which checks: crc, combo, menu; trainer (gamepad-trainer-check.sh) when asked for
	[string[]] $Checks = @("crc", "combo", "menu"),
	[string] $WorkDir = (Join-Path $env:TEMP "zh-gamepad-check"),
	# a folder put in front of PATH for the games: the x64 lane's d3dx9_43.dll on an ARM64 machine
	[string] $DllPath = "",
	[int] $TimeoutMinutes = 240,		# for each check, whose games have their own backstop below
	# each game's backstop in the checks (their GAMEPAD_*_TIMEOUT), in seconds: a Windows VM drawing through
	# WARP ran a 600-frame skirmish in over 600 s, the POSIX checks' default, and the check killed it
	[int] $GameTimeoutSeconds = 2400,
	[string] $Bash = "$env:ProgramFiles\Git\bin\bash.exe",
	# internal: "desktop" when the script is running its desktop part, and where that part writes its result
	[string] $Phase = "all",
	[string] $ResultFile = ""
)

$ErrorActionPreference = "Continue"
$env:ZH_UNATTENDED = "1"	# every game is unattended: no box may wait on a person (EarlyCommandLine.h)
$Checks = @($Checks | ForEach-Object { $_.Split(',') } | Where-Object { $_ -ne "" })
$Root = $PSScriptRoot
$RunDir = Join-Path $Root "GeneralsMD\Run"
$Tools = Join-Path $Root "GeneralsMD\Code\Tools"
if ($DllPath -ne "") { $env:PATH = "$DllPath;$env:PATH" }

# windows-ci.ps1's farm: a symbolic link where the token may make one, else a hard link
function New-FarmLink([string] $path, [string] $target) {
	try { New-Item -ItemType SymbolicLink -Path $path -Target $target -ErrorAction Stop | Out-Null }
	catch { New-Item -ItemType HardLink -Path $path -Target $target -ErrorAction Stop | Out-Null }
}

function New-Farm([string] $data, [string] $farm) {
	if (Test-Path $farm) { cmd /c "rmdir /s /q `"$farm`"" | Out-Null }		# links go, never their targets
	New-Item -ItemType Directory $farm | Out-Null
	Get-ChildItem -LiteralPath $data -Recurse -File | Where-Object { $_.Name -notlike '._*' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($data.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		New-FarmLink $dst $_.FullName
	}
	Get-ChildItem -LiteralPath $RunDir -Recurse -File | Where-Object { $_.Extension -ne '.pdb' } | ForEach-Object {
		$dst = Join-Path $farm $_.FullName.Substring($RunDir.Length + 1)
		New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
		if (Test-Path -LiteralPath $dst) { Remove-Item -LiteralPath $dst -Force }	# the link, never its target
		if ($_.Extension -in '.exe', '.dll') { Copy-Item -LiteralPath $_.FullName $dst }
		else { New-FarmLink $dst $_.FullName }
	}
}

# ---- the desktop part: every check, returning a result object ------------------------------------------
function Invoke-DesktopPart {
	$r = [ordered]@{ Checks = [ordered]@{}; Log = "" }
	New-Item -ItemType Directory -Force $WorkDir | Out-Null
	$farm = Join-Path $WorkDir "farm"
	try { New-Farm (Join-Path $DataDir "zerohour") $farm }
	catch { $r.Checks["farm"] = @{ Exit = -1; Lines = @("could not make the farm: $($_.Exception.Message)") }; return $r }
	foreach ($check in $Checks) {
		$out = Join-Path $WorkDir "$check.out"
		$script = "gamepad-$check-check.sh"
		# a script of its own, not bash -c: nothing to quote twice.  The paths in Git Bash's spelling; the check
		# starts each game in the farm (gamepad-game.sh).  LF line ends: bash takes a CR for part of a word.
		$starter = Join-Path $WorkDir "$check.sh"
		$lines = @(
			"export ZH_GAME_FARM=`"`$(cygpath -u '$farm')`"",
			"cd `"`$(cygpath -u '$Tools')`" || exit 2",
			"exec ./$script --generals `"`$ZH_GAME_FARM/generals.exe`" --data `"`$(cygpath -u '$DataDir')`"")
		[IO.File]::WriteAllText($starter, ($lines -join "`n") + "`n")
		foreach ($name in "GAMEPAD_CRC_TIMEOUT", "GAMEPAD_COMBO_TIMEOUT", "GAMEPAD_MENU_TIMEOUT", "GAMEPAD_TRAINER_TIMEOUT") {
			Set-Item "env:$name" "$GameTimeoutSeconds"
		}
		$clock = [Diagnostics.Stopwatch]::StartNew()
		$proc = Start-Process -FilePath $Bash -ArgumentList "`"$starter`"" -NoNewWindow -PassThru `
			-RedirectStandardOutput $out -RedirectStandardError "$out.err"
		$null = $proc.Handle		# kept, so ExitCode is still there after the exit
		$code = $null
		if (-not $proc.WaitForExit($TimeoutMinutes * 60 * 1000)) {
			# the check and whatever game it has running: the games are the farm's copy of generals.exe
			Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
			Get-Process generals -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$farm\*" } | Stop-Process -Force
			$code = 124
		} else { $code = $proc.ExitCode }
		# plain strings: Get-Content's carry properties that ConvertTo-Json would write out beside each line
		$lines = @(Get-Content $out, "$out.err" -ErrorAction SilentlyContinue | ForEach-Object { "$_" })
		$r.Checks[$check] = @{ Exit = $code; Seconds = [int]$clock.Elapsed.TotalSeconds; Lines = $lines }
	}
	return $r
}

if ($Phase -eq "desktop") {
	$result = Invoke-DesktopPart
	$result | ConvertTo-Json -Depth 5 | Out-File -Encoding utf8 "$ResultFile.partial"
	Move-Item -Force "$ResultFile.partial" $ResultFile
	exit 0
}

# ---- the main part --------------------------------------------------------------------------------------
if ($DataDir -eq "" -or -not (Test-Path (Join-Path $DataDir "zerohour"))) { Write-Host "-DataDir must name a folder holding zerohour\"; exit 1 }
if (-not (Test-Path $Bash)) { Write-Host "no Git Bash at $Bash (-Bash): the checks are bash scripts"; exit 1 }
if (-not (Test-Path (Join-Path $RunDir "generals.exe"))) { Write-Host "no generals.exe in ${RunDir}: build first"; exit 1 }
New-Item -ItemType Directory -Force $WorkDir | Out-Null
$resultFile = Join-Path $WorkDir "gamepad-result.json"
Remove-Item $resultFile, "$resultFile.partial" -ErrorAction SilentlyContinue
$clock = [Diagnostics.Stopwatch]::StartNew()
if ((Get-Process -Id $PID).SessionId -eq 0) {
	# windows-ci.ps1's way into the logged-on user's desktop: a one-off task, its wrapper in the classic console
	$user = (Get-CimInstance Win32_ComputerSystem).UserName
	if (-not $user) { Write-Host "no user is logged on to a desktop: the games cannot run"; exit 1 }
	$argList = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Phase desktop -ResultFile `"$resultFile`" " +
		"-DataDir `"$DataDir`" -Checks $($Checks -join ',') -WorkDir `"$WorkDir`" -TimeoutMinutes $TimeoutMinutes " +
		"-GameTimeoutSeconds $GameTimeoutSeconds"
	if ($DllPath -ne "") { $argList += " -DllPath `"$DllPath`"" }
	$wrapper = Join-Path $WorkDir "desktop-part.cmd"
	"@powershell.exe $argList" | Out-File -Encoding ascii $wrapper
	if ("$env:SystemRoot$wrapper" -match ' ') { Write-Host "the task cannot run $wrapper (a space in the path): a -WorkDir without one"; exit 1 }
	$task = "zh-gamepad-check-" + [guid]::NewGuid().ToString("N").Substring(0, 8)
	$taskRun = "$env:SystemRoot\System32\conhost.exe $env:SystemRoot\System32\cmd.exe /c $wrapper"
	if ($taskRun.Length -gt 261) { Write-Host "the task line is $($taskRun.Length) characters, over schtasks' 261: a shorter -WorkDir"; exit 1 }
	schtasks /create /tn $task /tr $taskRun /sc once /st 23:59 /it /ru $user /f | Out-Null
	schtasks /run /tn $task | Out-Null
	$limit = $TimeoutMinutes * $Checks.Count + 10
	while (-not (Test-Path $resultFile) -and $clock.Elapsed.TotalMinutes -lt $limit) { Start-Sleep -Seconds 10 }
	Start-Sleep -Seconds 2
	schtasks /delete /tn $task /f | Out-Null
} else {
	$dllArgs = @(if ($DllPath -ne "") { "-DllPath", $DllPath })
	& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Phase desktop -ResultFile $resultFile `
		-DataDir $DataDir -Checks ($Checks -join ',') -WorkDir $WorkDir -TimeoutMinutes $TimeoutMinutes -GameTimeoutSeconds $GameTimeoutSeconds @dllArgs
}

$failed = $false
if (-not (Test-Path $resultFile)) {
	Write-Host "NO RESULT: the desktop part did not finish ($([int]$clock.Elapsed.TotalSeconds) s)"; $failed = $true
} else {
	$d = Get-Content -Raw $resultFile | ConvertFrom-Json
	foreach ($p in $d.Checks.PSObject.Properties) {
		Write-Host "--- $($p.Name): exit $($p.Value.Exit), $($p.Value.Seconds) s"
		$p.Value.Lines | ForEach-Object { Write-Host $_ }
		if ($p.Value.Exit -ne 0) { $failed = $true }
	}
}
Write-Host $(if ($failed) { "GAMEPAD CHECKS FAILED" } else { "GAMEPAD CHECKS PASSED" })
exit $(if ($failed) { 1 } else { 0 })
