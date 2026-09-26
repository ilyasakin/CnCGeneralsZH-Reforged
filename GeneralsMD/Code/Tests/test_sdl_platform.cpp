/*
 * C2's SDL side of two engine seams, without a screen: SDL3's offscreen video driver stands in for the
 * machine's displays.
 *   - Monitors.h over SdlDisplays: with no table the one 800x600 monitor; with it, SDL's displays in
 *     pixels, named "\\.\DISPLAY<n>", found by name with the primary for an unknown one, and each
 *     one's sizes sorted, once each, none under the game's floor, the desktop's own among them.
 *   - SdlMessageBox's layout for MessageBoxWrapper's flags: the buttons Windows shows, in its order,
 *     answering its IDs, the default button and the icon.  No box is shown: that needs a person.
 * What it cannot see: a real display's modes (the offscreen driver has one), and a box on screen.
 */
#include "test_harness.h"

#include "PreRTS.h"
#include "Common/MessageBoxFlags.h"
#include "Common/Monitors.h"
#include "SdlDevice/Common/SdlDisplays.h"
#include "SdlDevice/Common/SdlMessageBox.h"

#include <SDL3/SDL.h>

#include <math.h>
#include <string.h>

namespace {

bool start_offscreen_video()
{
	static bool started = false;
	if (!started) {
		SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
		started = SDL_Init(SDL_INIT_VIDEO);
		if (!started) printf("  SDL_Init: %s\n", SDL_GetError());
	}
	return started;
}

} // namespace

TEST(monitors_without_a_platform_table_are_the_one_floor_sized_monitor)
{
	ThePlatformDisplays = NULL;
	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	CHECK_EQ(listMonitors(monitors, MAX_MONITOR_ENTRIES), 1);
	CHECK_EQ((int)monitors[0].rect.right, 800);
	CHECK_EQ((int)monitors[0].rect.bottom, 600);
	CHECK(monitors[0].primary);
	DisplayModeEntry modes[MAX_DISPLAY_MODE_ENTRIES];
	CHECK_EQ(listDisplayModes("", modes, MAX_DISPLAY_MODE_ENTRIES), 1);
	CHECK_EQ(modes[0].width, 800);
}

TEST(monitors_come_from_sdl_in_pixels_and_by_name)
{
	CHECK(start_offscreen_video());
	ThePlatformDisplays = &TheSdlDisplays;

	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	CHECK(displays != NULL && count >= 1);

	MonitorEntry monitors[MAX_MONITOR_ENTRIES];
	const int listed = listMonitors(monitors, MAX_MONITOR_ENTRIES);
	CHECK_EQ(listed, count);
	int primaries = 0;
	for (int i = 0; i < listed && displays != NULL; ++i) {
		char device[32];
		snprintf(device, sizeof(device), "\\\\.\\DISPLAY%d", i + 1);
		CHECK_STR(monitors[i].device, device);
		CHECK_EQ(monitors[i].number, i + 1);
		SDL_Rect bounds;
		CHECK(SDL_GetDisplayBounds(displays[i], &bounds));
		const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(displays[i]);
		const float density = (desktop && desktop->pixel_density > 0) ? desktop->pixel_density : 1.0f;
		CHECK_EQ((long)(monitors[i].rect.right - monitors[i].rect.left), lround(bounds.w * (double)density));
		CHECK_EQ((long)(monitors[i].rect.bottom - monitors[i].rect.top), lround(bounds.h * (double)density));
		if (monitors[i].primary) {
			++primaries;
			CHECK_EQ(displays[i], SDL_GetPrimaryDisplay());
		}
		printf("  %s \"%s\" %ldx%ld%s\n", monitors[i].device, monitors[i].name,
			(long)(monitors[i].rect.right - monitors[i].rect.left), (long)(monitors[i].rect.bottom - monitors[i].rect.top),
			monitors[i].primary ? " primary" : "");
	}
	CHECK_EQ(primaries, 1);

	// by name, and the primary for a name no monitor has (Options.ini naming an unplugged one)
	CHECK_STR(findMonitor("\\\\.\\display1").device, "\\\\.\\DISPLAY1");
	CHECK(findMonitor("\\\\.\\DISPLAY99").primary);
	CHECK(findMonitor(NULL).primary);
	SDL_free(displays);
	ThePlatformDisplays = NULL;
}

TEST(display_modes_come_from_sdl_sorted_once_each_and_above_the_floor)
{
	CHECK(start_offscreen_video());
	ThePlatformDisplays = &TheSdlDisplays;

	DisplayModeEntry modes[MAX_DISPLAY_MODE_ENTRIES];
	const int count = listDisplayModes("\\\\.\\DISPLAY1", modes, MAX_DISPLAY_MODE_ENTRIES);
	CHECK(count >= 1);
	for (int i = 0; i < count; ++i) {
		CHECK(modes[i].width >= MIN_DISPLAY_MODE_WIDTH && modes[i].height >= MIN_DISPLAY_MODE_HEIGHT);
		if (i > 0)
			CHECK(modes[i - 1].width < modes[i].width
				|| (modes[i - 1].width == modes[i].width && modes[i - 1].height < modes[i].height));
	}
	const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
	bool hasDesktop = false;
	for (int i = 0; i < count && desktop; ++i)
		hasDesktop |= modes[i].width == (int)lround(desktop->w * (double)desktop->pixel_density)
			&& modes[i].height == (int)lround(desktop->h * (double)desktop->pixel_density);
	CHECK(hasDesktop);
	ThePlatformDisplays = NULL;
}

TEST(message_box_layout_is_windows_buttons_ids_default_and_icon)
{
	// The assertion box: Abort, Retry, Ignore, Ignore the default, an error.
	SdlMessageBoxLayout layout = sdlMessageBoxLayout(MSGBOX_ABORTRETRYIGNORE | MSGBOX_ICONERROR | MSGBOX_DEFBUTTON3 | MSGBOX_TASKMODAL);
	CHECK_EQ(layout.numButtons, 3);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_ABORT);
	CHECK_EQ(layout.buttons[1].buttonID, (int)MSGBOX_ID_RETRY);
	CHECK_EQ(layout.buttons[2].buttonID, (int)MSGBOX_ID_IGNORE);
	CHECK((layout.buttons[2].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) == 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_ERROR) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT) != 0);

	// Yes/No with a warning: MSGBOX_ICONWARNING (0x30) shares a bit with MSGBOX_ICONERROR (0x10).
	layout = sdlMessageBoxLayout(MSGBOX_YESNO | MSGBOX_ICONWARNING);
	CHECK_EQ(layout.numButtons, 2);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_YES);
	CHECK_EQ(layout.buttons[1].buttonID, (int)MSGBOX_ID_NO);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_WARNING) != 0);
	CHECK((layout.flags & SDL_MESSAGEBOX_ERROR) == 0);

	// OK alone: one button, information.
	layout = sdlMessageBoxLayout(MSGBOX_OK);
	CHECK_EQ(layout.numButtons, 1);
	CHECK_EQ(layout.buttons[0].buttonID, (int)MSGBOX_ID_OK);
	CHECK((layout.flags & SDL_MESSAGEBOX_INFORMATION) != 0);

	// DEFBUTTON3 on a two-button box has no third button: the first stays the default.
	layout = sdlMessageBoxLayout(MSGBOX_YESNO | MSGBOX_DEFBUTTON3);
	CHECK((layout.buttons[0].flags & SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT) != 0);
}

TEST(message_box_hook_is_set_only_while_there_is_an_owner)
{
	CHECK(TheMessageBoxHook == NULL);
	CHECK(start_offscreen_video());
	SDL_Window *window = SDL_CreateWindow("test_sdl_platform", 320, 240, SDL_WINDOW_HIDDEN);
	CHECK(window != NULL);
	setSdlMessageBoxOwner(window);
	CHECK(TheMessageBoxHook != NULL);
	setSdlMessageBoxOwner(NULL);
	CHECK(TheMessageBoxHook == NULL);
	SDL_DestroyWindow(window);
}
