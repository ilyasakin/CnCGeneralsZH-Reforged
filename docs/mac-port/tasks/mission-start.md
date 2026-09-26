# -mission: a single player start from the command line, 2026-09-26 (-18)

## Why

The crash sweeps start every map headless. The multiplayer maps start as a skirmish (`-autoskirmish`), but
86 of the 171 stock maps are not multiplayer maps, and nothing started them: the campaign missions, the
generals' challenges, and the cinematic and test maps. A crash on those maps could go unseen.

## `-file <map>` is not the answer in a Release build

`-file` is registered only inside `#if defined(_DEBUG) || defined(_INTERNAL)` in `CommandLine.cpp`'s
switch table. A Release build ignores it, and with `-noshellmap` the run sits in GAME_NONE (lldb:
`TheGameLogic->m_gameMode` 6, `m_frame` in the millions) until it is killed.

-18 first read this as "`-file` is dead on every platform", blaming `init()`'s `resetAll()`. **That was
wrong, and the measurement shows it.** A probe build registered a Release-visible switch with
`parseFile`'s body:
- with HEAD's immediate start in `init()`, Tournament Desert reached its frame limit;
- with a deferred start, it also reached its frame limit.

So `-file` works where it is compiled in. Its code is left exactly as it was: the `.map` branch in
`GameEngine::init` is byte-identical to HEAD. Nothing goes in the latent list for it.

## What `-mission <map> [easy|normal|hard]` does

It is a Release switch, as `-replay` and `-autoskirmish` are. It is recorded in `init()` and started
after `resetAll()`, where the save and the replay start (`startPendingMap` in `GameEngine.cpp`):

1. A map that is not there is logged (`-mission: '...' is not there`). A headless run quits; a windowed
   run goes to the shell.
2. `CampaignManager::setCampaignAndMissionForMap` looks for the campaign mission that plays the map. It
   prefers a real campaign over a `_demo` one, which repeats the same maps.
3. **A campaign mission** starts as `MainMenu.cpp`'s `setupGameStart` and `doGameStart` start it, at
   the difficulty asked for (default normal).
4. **A challenge** also gets what `ChallengeMenu.cpp` sets up before its Play button:
   - `TheChallengeGameInfo`, created and readied as `ChallengeMenuInit` does (`:335`);
   - the general's player template in slot 0, as `setGeneralCampaign` does.
   This is `prepareChallengeMission`, whose comment names both, so a change there has to be made here
   as well.
5. **A map no campaign plays** loads plain, as `-file` loads it.

`<map>` is a path, or a bare name: `-mission GC_ChemGeneral` is
`Maps\GC_ChemGeneral\GC_ChemGeneral.map`.

The run's log names the result, for example:
`-mission: 'Maps\GC_ChemGeneral\GC_ChemGeneral.map' is campaign 'challenge_0' mission 'mission01', a challenge, normal`.

Options.ini is never written. The menu stores the difficulty there, and a dev aid has no business
changing the player's preferences.

## A crash it found: a challenge whose load movie does not open

`ChallengeLoadScreen::init` read the movie stream's size through NULL when the movie did not open.
`SinglePlayerLoadScreen::init` already returns in that case, and the challenge screen now does the same.
The screen is left as `ChallengeLoadScreen::init`'s own return leaves it when the video buffer cannot be
allocated: no stream, no portraits.

- **Headless:** there is no video, so every challenge crashed (SIGSEGV).
- **Windows:** the same crash follows for a challenge whose movie is missing, for example a damaged
  install or a mod.
- **Why it has no number:** shipped data has every challenge movie, so this is latent, not numbered
  (README).

## Tested

`mission_check` (ctest, `Tests/run_mission_check.sh`, with the game's data on a rule-9 farm) covers:
- a campaign mission by path, at hard;
- a challenge by bare name;
- a plain map;
- a missing map.

Each of the first three must reach frame 150; the missing map must quit without starting a game. Every
run has a time limit, so a start that never happens fails the test instead of hanging it.

**Armed controls**, run through the same script:
- **The `startPendingMap()` call removed:** all eight checks fail, and each run is killed by the time
  limit.
- **The load screen's NULL-stream guard removed:** the challenge fails with SIGSEGV (exit 139), and the
  other three pass.

## Not checked

- Windows: no Windows build or machine was used.
- A windowed `-mission` run. The shell path taken for a missing map was not exercised either.
- Whether a Windows `-headless` run has a video player that opens the challenge movies.
