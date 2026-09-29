#!/bin/bash
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
#
# gamepad-windows-game.sh <generals.exe> <switches>: one game of a gamepad check on Windows, under Git Bash
# (gamepad-game.sh).  The paths the check hands the game in its environment are Git Bash's, so they are
# given to the Windows program in its own spelling; the game runs in the farm it sits in, in a window (the
# checks' -offscreen draws, and Windows' -headless would not); and its log's CRLF line ends are made LF, so
# the check reads it as it reads a POSIX game's.
set -u
exe="$1"; shift
for name in ZH_INPUT_SCRIPT ZH_USER_DATA_DIR; do
	value="${!name:-}"
	[ -n "$value" ] && export "$name=$(cygpath -w "$value")"
done
prefix=""
args=("$@")
for (( i = 0; i + 1 < ${#args[@]}; ++i )); do
	[ "${args[$i]}" = "-logPrefix" ] && prefix="${args[$((i + 1))]}"
done
cd "$(dirname "$exe")" || exit 2
"$exe" "$@"
status=$?
for log in "${prefix}"DebugLogFile*.txt; do
	[ -f "$log" ] && sed -i 's/\r$//' "$log"
done
exit $status
