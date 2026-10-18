UI Entry Point and Usage

All runtime interfaces are drawn by /Game/UI/HUD/BP_PlanetHUD (AHUD).
Its native parent class is CosmoGameHUD; individual tutorials do not have separate HUDs or Widgets.
DA_PlanetUI stores 24 layout groups, including game screens, 9 tutorials, and asset sheets.
External/UI/Layout.json is the reimportable layout source. External/UI/Source retains the user-supplied original assets.
Textures and Fonts contain the officially imported Unreal assets.

Project Settings / Maps & Modes:
Game Default Map and Editor Startup Map are both /Game/UI/Maps/Lvl_MainMenu.
The start screen's dedicated GameMode does not spawn a gameplay character.
START MISSION -> SKIP TUTORIAL? dialog (centered, covering approximately 59.3% of the viewport area).
CONFIRM skips the tutorial and enters /Game/AlienPlanet/Lvl_AlienPlanet directly; CANCEL connects to Grok for the opening conversation and 9 tutorials before entering the same level.
The same BP_PlanetHUD draws the dialog using the existing dark panel, cyan border, and fonts. Default focus is CANCEL; Esc also continues the tutorial.
Tutorials share one HUD, with 0.55-second fade-in and fade-out. Pages advance only after the audio response completes and the local audio buffer finishes playing.
The real Grok service must run at ws://127.0.0.1:8766, using the project's existing Scripts/StartCosmo.ps1.
If unavailable, the tutorial waits for connection. Interrupted narration stays on the current page; Enter/gamepad confirm retries.
Fixed durations and local TTS do not skip tutorials. Esc cancels the tutorial and returns to the start screen.

Click with the mouse; select with arrow keys/Tab/gamepad directional controls; confirm with Enter/Space/gamepad confirm.
During gameplay, Esc/gamepad back pauses; press again to resume.
The pause menu includes resume, restart confirmation, voice settings, and return to main menu.
Victory/defeat screens connect to character events. Results display actual kills, progress, core status, and remaining health.
The combat HUD connects to health, shield, ability cooldowns, boss health, objectives, subtitles, and webcam preview.

Verification: Scripts/VerifyUI.ps1 -Build -RegenerateAssets -Preview
Tests use only local simulated audio, without calling Grok or accessing the webcam/microphone.
Checks cover retrying the same page after voice failure, actual buffer completion on 10 pages, level loading, pause/resume, and no page advance without audio.
Offscreen screenshots: Saved/Screenshots/UI_*.png.
Real Grok credentials, service, speakers, and webcam still require verification in the actual play environment.

Conversation after the tutorial:
Entering the level enables FreeConversation by default, disabling location announcements, ability instruction, and automatic next-step questions.
The HUD no longer shows tutorial action guidance at the top. The opening and 9 preceding tutorials are unaffected.
Players can chat freely or actively ask Grok game-related questions.
After approximately 75-150 seconds of silence, Grok may initiate one conversation. Player speech or Grok replies reset the timer.
No proactive speech occurs during combat, pause, focus loss, muting, death/victory, or an existing conversation.
Grok generates proactive topics without fabricated player messages or fixed tutorial lines.

Interactive tutorial (following the supplied nine-task table):
Left fist -> extend left hand -> retract left hand -> hold right hand open and still -> pinch -> release -> push a fist forward -> swipe horizontally -> turn the head left and right.
Each page's English instruction and success feedback are stored in the tutorials array of External/UI/Layout.json and imported into DA_PlanetUI.
After the instruction finishes playing, the tutorial waits for the action. Success displays feedback narrated by Grok; only after feedback finishes does the page fade out and advance.
Steady gestures require approximately 0.45 seconds. Head turns require both directions. Pinch release first requires a confirmed pinch, with the hand still tracked during release.
Fire/Wave accept only new recognition events after the current page's instruction finishes. Empty frames, tracking loss, and repeated old events do not complete the tutorial.
Tutorial startup launches camera recognition and shows the preview. Canceling or entering the level releases the tutorial camera process and receiver port.
Configure camera index, hand swapping, and reversed head turning under Tutorial|Camera in BP_PlanetHUD.
Verification includes 9 simulated gestures, 19 complete audio segments (opening + 9 instructions + 9 feedback lines), and instruction/feedback failure retries.

Layout JSON and original assets were moved to External/UI to prevent Unreal from automatically importing JSON/CSV as DataTables. Production assets remain in Content/UI. Do not drag layout JSON into the Content Browser; the game uses the generated DA_PlanetUI.

Start Button and Connection:
Opening the main menu does not connect voice; clicking START MISSION starts the built-in COSMO service.
The screen stays black during connection, upstream session setup, and initial audio buffering. When the first audio is ready, the opening UI appears and playback starts in the same frame.
GameInstance manages the built-in service, keeping it alive across map transitions and ending it when the game exits.
Connection timeouts, missing keys, and other errors display their cause. Enter retries; Esc returns.
No browser or manual Python startup is needed. Set the existing Grok key in the Windows user environment variable XAI_API_KEY.
GROK_API_KEY is also supported as an alias. Grok Voice alone does not require XAI_MODEL.
On every launch, the launcher reads persistent user/system environment variables, supporting editors opened before the variables were set.
Temporary variables set with CMD's set are unavailable to unrelated game processes; save them through Windows environment variable settings.
Additional verification option: Scripts/VerifyUI.ps1 -Bootstrap. Uses explicit mock configuration to verify actual built-in service startup without external Grok calls.
