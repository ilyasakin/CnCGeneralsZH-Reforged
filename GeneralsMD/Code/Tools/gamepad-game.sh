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
# gamepad-game.sh: where a gamepad check's game runs, sourced by gamepad-crc-check.sh, gamepad-combo-check.sh
# and gamepad-menu-check.sh after they set GENERALS, INSTALL, WORK and CODE.  gamepad_game_setup sets ROOT,
# the folder the game runs in, and GAME, the command before the game's own switches:
#   - off Windows, the POSIX executable with no window (-offscreen), on a farm of links to the install in the
#     work folder (-root) and the overlay staged beside it (-overlay);
#   - on Windows (G1b), under Git Bash, when gamepad-check.ps1 has made the farm and names it in ZH_GAME_FARM:
#     generals.exe in that farm, through gamepad-windows-game.sh.
# Everything else - the scripts, the runs, the logs read and the verdicts - is the check's own, on both.

gamepad_game_setup() {
	if [ -n "${ZH_GAME_FARM:-}" ]; then
		ROOT="$ZH_GAME_FARM"
		GAME=(bash "$(dirname "${BASH_SOURCE[0]}")/gamepad-windows-game.sh" "$GENERALS")
		return 0
	fi
	ROOT="$WORK/root"
	mkdir -p "$ROOT"
	( cd "$INSTALL" && find . -type d ! -name '._*' ) | while IFS= read -r d; do mkdir -p "$ROOT/$d"; done
	( cd "$INSTALL" && find . -type f ! -name '._*' ) | while IFS= read -r f; do ln -s "$INSTALL/${f#./}" "$ROOT/$f"; done
	local overlay="$WORK/overlay"
	"$(dirname "${BASH_SOURCE[0]}")/stage-overlay.sh" "$CODE/Data" "$CODE/../Run" "$overlay"
	GAME=("$GENERALS" -offscreen -root "$ROOT" -overlay "$overlay")
}
