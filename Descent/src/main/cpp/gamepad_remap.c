//
// Remap Gamepad -- lets the player rebind the 7 gameplay-action gamepad buttons
// (Fire Primary/Secondary/Flare, Rear View, Bank Left/Right, Toggle Cockpit), plus a
// "Stick Layout" option (Standard/Modern) that changes which physical inputs drive
// turning, pitching, strafing and thrust (see Gamepad_stick_layout below).
//
// D-pad, Start ("Menu") and Select ("Map") are intentionally never routed through
// this system -- DescentView.handleGamepadKey() still dispatches them directly via
// its original, fixed keyHandler() calls, so navigation and the ability to reach
// this very screen always keep working regardless of what's been remapped.
//
// Architecture note: today, by the time a gamepad button press reaches native code
// it has already been collapsed to a fixed Descent scancode by a hardcoded Java
// switch, so native code can't tell "physical button A" apart from "keyboard Ctrl."
// gamepadButtonRaw() below is the fix -- Java forwards the *raw* Android keyCode for
// the 7 remappable buttons instead of pre-translating it, so this file can own the
// real keyCode<->action table (both for live dispatch and for the remap UI/save data).
//

#include <jni.h>
#include <string.h>

#include "types.h"
#include "error.h"
#include "gr.h"
#include "key.h"
#include "palette.h"
#include "game.h"
#include "gamefont.h"
#include "newmenu.h"
#include "multi.h"
#include "endlevel.h"
#include "mouse.h"

// Mirrors android.view.KeyEvent.KEYCODE_BUTTON_* -- stable public Android API ints.
#define GP_BUTTON_A       96
#define GP_BUTTON_B       97
#define GP_BUTTON_X       99
#define GP_BUTTON_Y       100
#define GP_BUTTON_L1      102
#define GP_BUTTON_R1      103
#define GP_BUTTON_THUMBL  106

#define GP_NUM_ACTIONS 7
#define GP_UNBOUND (-1)

typedef struct GamepadRemapAction {
	const char *label;
	unsigned char scancode;   // Descent KEY_* this action always triggers -- never remapped.
	int defaultKeyCode;       // Compiled-in default physical button (Android keyCode).
} GamepadRemapAction;

static const GamepadRemapAction Gamepad_remap_actions[GP_NUM_ACTIONS] = {
	{ "Fire Primary",   KEY_LCTRL,    GP_BUTTON_A },
	{ "Fire Secondary", KEY_SPACEBAR, GP_BUTTON_B },
	{ "Fire Flare",     KEY_F,        GP_BUTTON_X },
	{ "Rear View",      KEY_R,        GP_BUTTON_Y },
	{ "Bank Left",      KEY_Q,        GP_BUTTON_L1 },
	{ "Bank Right",     KEY_E,        GP_BUTTON_R1 },
	{ "Toggle Cockpit", KEY_F3,       GP_BUTTON_THUMBL },
};

// Live, persisted bindings -- consulted every time a gamepad button event arrives.
// Statically seeded to the defaults so a freshly-launched process (before any
// read_player_file() call) is already correct. playsave.c reads/writes this array
// directly (see playsave.c's write_player_file()/read_player_file()).
int Gamepad_bound_keycodes[GP_NUM_ACTIONS] = {
	GP_BUTTON_A, GP_BUTTON_B, GP_BUTTON_X, GP_BUTTON_Y, GP_BUTTON_L1, GP_BUTTON_R1, GP_BUTTON_THUMBL
};

// Which physical inputs drive which analog functions:
//   0 (Standard) -- left stick turns/pitches, right stick slides, triggers thrust.
//   1 (Modern)   -- left stick is fully movement (slide left/right on X, forward/
//                   reverse thrust on Y), right stick is fully looking (turn/pitch),
//                   and the triggers fire instead of driving thrust (RT = Fire
//                   Primary, LT = Fire Secondary). Vertical strafe has no stick or
//                   trigger in this layout -- both stick Y axes and both triggers are
//                   already spoken for.
// All the actual axis routing lives in DescentView.java's onGenericMotionEvent() --
// this is just the persisted live setting it reads via the JNI getter below, same role
// Gamepad_bound_keycodes plays for the 7 button actions.
int Gamepad_stick_layout = 0;

// Cross-thread state for the remap screen's capture step. JNI calls land on the UI
// thread; the menu's poll loop below runs on the native game thread -- same informal
// single-writer/single-reader volatile pattern this codebase already relies on for
// keyd_pressed[] (lib/key.h).
static volatile int Gamepad_remap_capturing = 0;
static volatile int Gamepad_remap_captured_keycode = GP_UNBOUND;
static volatile int Gamepad_remap_confirm_pressed = 0;

// True for the entire time do_remap_gamepad_menu() is on screen (set/cleared at the top
// and bottom of that function). DescentView.handleGamepadKey() checks this via the JNI
// export below to let physical A act as this screen's confirm button even when
// isInGame() is false -- which it always is here, since this screen only opens from the
// Options menu, never from actual gameplay.
static volatile int Gamepad_remap_screen_active = 0;

extern void key_handler(unsigned char scancode, bool down);

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_gamepadButtonRaw(JNIEnv *env, jclass type, jint keyCode, jboolean down) {
	int i;

	if (Gamepad_remap_capturing) {
		if (down) {
			Gamepad_remap_captured_keycode = keyCode;
		}
		return;
	}

	if (keyCode == GP_BUTTON_A && down) {
		// Physical A doubles as the universal "confirm" gesture inside the Remap
		// Gamepad screen (see do_remap_gamepad_menu() below). It's only latched
		// here, outside capture mode, so a capture-completing A press never leaves
		// a stale confirm flag for the menu loop to misread afterward.
		Gamepad_remap_confirm_pressed = 1;
	}

	for (i = 0; i < GP_NUM_ACTIONS; i++) {
		if (Gamepad_bound_keycodes[i] == keyCode) {
			key_handler(Gamepad_remap_actions[i].scancode, down);
		}
	}
}

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_isInRemapGamepadScreen(JNIEnv *env, jclass type) {
	return (jboolean) Gamepad_remap_screen_active;
}

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_isModernStickLayout(JNIEnv *env, jclass type) {
	return (jboolean) Gamepad_stick_layout;
}

// main/menu.c's Options-menu "Invert Y" checkbox -- defined there (not here) since it's
// a plain Options entry, not part of this screen, but exposed here alongside the other
// stick-related JNI getters DescentView.java already reads every motion event.
extern int Config_invert_y;

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_isInvertYEnabled(JNIEnv *env, jclass type) {
	return (jboolean) Config_invert_y;
}

// Resets the live table to its compiled-in defaults. Called from playsave.c's
// read_player_file(), before the trailing fread of the saved bindings, so that a
// pilot with no saved remap data (a fresh pilot, or one whose .plr predates this
// feature) always starts from a known-clean state rather than possibly inheriting
// whatever a previously-loaded pilot in this same process left behind.
void gamepad_remap_reset_to_defaults(void) {
	int i;
	for (i = 0; i < GP_NUM_ACTIONS; i++) {
		Gamepad_bound_keycodes[i] = Gamepad_remap_actions[i].defaultKeyCode;
	}
	Gamepad_stick_layout = 0;
}

// --- Helpers for the menu screen below -------------------------------------------

static const char *gamepad_remap_bound_name(int keyCode) {
	switch (keyCode) {
		case GP_BUTTON_A:      return "A";
		case GP_BUTTON_B:      return "B";
		case GP_BUTTON_X:      return "X";
		case GP_BUTTON_Y:      return "Y";
		case GP_BUTTON_L1:     return "L1";
		case GP_BUTTON_R1:     return "R1";
		case GP_BUTTON_THUMBL: return "L3";
		default:               return "---";
	}
}

static void gamepad_remap_start_capture(void) {
	Gamepad_remap_captured_keycode = GP_UNBOUND;
	Gamepad_remap_capturing = 1;
}

static void gamepad_remap_cancel_capture(void) {
	Gamepad_remap_capturing = 0;
	Gamepad_remap_captured_keycode = GP_UNBOUND;
}

static int gamepad_remap_poll_confirm(void) {
	int pressed = Gamepad_remap_confirm_pressed;
	Gamepad_remap_confirm_pressed = 0;
	return pressed;
}

// --- The "Remap Gamepad" screen itself --------------------------------------------
//
// newmenu_item/newmenu_do1 (main/newmenu.c) has no scrolling and no way to block and
// wait for a raw physical button press, so -- like main/kconfig.c's kconfig_sub()/
// kc_change_key(), the classic DOS "press a key to rebind" screen this is modeled on
// -- this is a standalone custom draw+input loop, not a newmenu_item screen.

#define GP_NUM_ROWS (GP_NUM_ACTIONS + 4)    // 7 actions + Stick Layout + Reset/Cancel/Apply
#define GP_ROW_STICK_LAYOUT (GP_NUM_ACTIONS)
#define GP_ROW_RESET  (GP_NUM_ACTIONS + 1)
#define GP_ROW_CANCEL (GP_NUM_ACTIONS + 2)
#define GP_ROW_APPLY  (GP_NUM_ACTIONS + 3)

#define GP_TITLE_Y 8
#define GP_INFO_Y 20
#define GP_ROWS_START_Y 36

// Same font set + roles main/newmenu.c's newmenu_do3()/draw_item() use for every other
// menu in the game (TITLE_FONT/SUBTITLE_FONT/CURRENT_FONT/NORMAL_FONT there) -- reused
// here under local names since those macros are private to newmenu.c. Matching them is
// what keeps this screen looking like the rest of the Options menu instead of using the
// small HUD gauge font (GAME_FONT, 5px tall) the first draft mistakenly drew with.
#define GP_TITLE_FONT    (Gamefonts[GFONT_BIG_1])
#define GP_SUBTITLE_FONT (Gamefonts[GFONT_MEDIUM_3])
#define GP_CURRENT_FONT  (Gamefonts[GFONT_MEDIUM_2])   // highlighted row
#define GP_NORMAL_FONT   (Gamefonts[GFONT_MEDIUM_1])   // all other rows

static int Gamepad_remap_row_y[GP_NUM_ROWS];
static int Gamepad_remap_row_h = 10;    // UNscaled row-to-row spacing (see the comment
                                         // where this is computed, in do_remap_gamepad_menu()).
static int Gamepad_remap_erase_h = 14;  // SCALED erase-box height -- see gamepad_remap_draw_row().

static const char *gamepad_remap_row_label(int row) {
	if (row == GP_ROW_STICK_LAYOUT) return "Stick Layout";
	if (row == GP_ROW_RESET) return "Reset to Defaults";
	if (row == GP_ROW_CANCEL) return "Cancel";
	if (row == GP_ROW_APPLY) return "Apply";
	return Gamepad_remap_actions[row].label;
}

// staging_layout is only meaningful for GP_ROW_STICK_LAYOUT -- pass whatever's current
// (it's ignored for every other row).
static void gamepad_remap_draw_row(int row, int staging[GP_NUM_ACTIONS], int staging_layout, int is_current) {
	int w, h, aw;
	char rtext[24];
	int y = Gamepad_remap_row_y[row];

	// Erase the row by repainting its slice of the actual saved backdrop (see
	// nm_restore_background() and how do_remap_gamepad_menu() saves that backdrop into
	// VR_offscreen_buffer on entry) -- NOT a flat black fill. A flat fill is what this
	// used to do, and it's wrong here: this screen's background is the real, darkened
	// game/menu backdrop showing through (same as every other menu in the game), not
	// solid black, so a black erase box left a visible black bar behind every row
	// instead of blending into that backdrop like the rest of the screen does.
	//
	// Uses Gamepad_remap_erase_h, NOT Gamepad_remap_row_h, for the box height: y here is
	// already scaled (it comes out of Gamepad_remap_row_y[], which applies Scale_y once),
	// but row_h is deliberately kept unscaled so the row-to-row *spacing* doesn't get
	// double-scaled (see do_remap_gamepad_menu()). Reusing that unscaled value as an
	// erase-box height against an already-scaled y made the box too short on anything
	// but 1x scale, so old text (e.g. the previous bound button, or a stale "---") kept
	// peeking out from under whatever was drawn on top of it.
	{
		int x0 = (int) (20 * f2fl(Scale_x));
		int x1 = (int) (grd_curcanv->cv_bitmap.bm_w - 20 * f2fl(Scale_x));
		nm_restore_background(x0, y - 1, x1 - x0, Gamepad_remap_erase_h);
	}

	// Selection is shown by switching to the bigger CURRENT_FONT, exactly like every
	// other menu's draw_item() -- not by a custom highlight color, so this screen reads
	// as the same UI as the rest of Options rather than a one-off.
	grd_curcanv->cv_font = is_current ? GP_CURRENT_FONT : GP_NORMAL_FONT;

	gr_scale_string(30 * f2fl(Scale_x), y, Scale_factor, Scale_factor, gamepad_remap_row_label(row));

	if (row < GP_NUM_ACTIONS) {
		strncpy(rtext, gamepad_remap_bound_name(staging[row]), sizeof(rtext) - 1);
		rtext[sizeof(rtext) - 1] = '\0';
	} else if (row == GP_ROW_STICK_LAYOUT) {
		strncpy(rtext, staging_layout ? "Modern" : "Standard", sizeof(rtext) - 1);
		rtext[sizeof(rtext) - 1] = '\0';
	} else {
		return;   // Reset/Cancel/Apply have no right-hand value to draw.
	}

	gr_get_string_size(rtext, &w, &h, &aw);
	w = (int) (w * f2fl(Scale_factor));
	gr_scale_string(grd_curcanv->cv_bitmap.bm_w - 60 * f2fl(Scale_x) - w, y, Scale_factor, Scale_factor, rtext);
}

static void gamepad_remap_draw_prompt(const char *text) {
	int ph = (int) (GP_SUBTITLE_FONT->ft_h * f2fl(Scale_y)) + 4;

	// See gamepad_remap_draw_row() -- same reasoning, restore the real backdrop rather
	// than fill flat black.
	nm_restore_background(0, (int) (GP_INFO_Y * f2fl(Scale_y)) - 1, grd_curcanv->cv_bitmap.bm_w, ph + 1);

	if (text) {
		grd_curcanv->cv_font = GP_SUBTITLE_FONT;
		gr_set_fontcolor(GR_GETCOLOR(21, 21, 21), -1);
		gr_scale_string(0x8000, GP_INFO_Y * f2fl(Scale_y), Scale_factor, Scale_factor, text);
	}
}

// Which row (if any) a screen point falls in, using the exact same bounding box each
// row is erased/highlighted with in gamepad_remap_draw_row() -- so the tap target
// always matches what's actually drawn on screen. mouse_x/mouse_y (from
// mouse_button_down_count()/mouse_button_up_count() below) are already in this same
// scaled screen-pixel space, the same space newmenu.c's get_item_at_menu_pos() expects
// its arguments in -- see mouseHandler()/mouse_handler() and how newmenu_do3() uses
// mouse_x/mouse_y directly against item[i].x/item[i].y without any extra conversion.
static int gamepad_remap_row_at(int x, int y) {
	int row;
	int x0 = (int) (20 * f2fl(Scale_x));
	int x1 = (int) (grd_curcanv->cv_bitmap.bm_w - 20 * f2fl(Scale_x));

	if (x < x0 || x > x1) {
		return -1;
	}
	for (row = 0; row < GP_NUM_ROWS; row++) {
		int y0 = Gamepad_remap_row_y[row] - 1;
		int y1 = Gamepad_remap_row_y[row] + Gamepad_remap_erase_h;
		if (y >= y0 && y <= y1) {
			return row;
		}
	}
	return -1;
}

extern void delay(unsigned long time);          // main/kconfig.c -- ~100Hz usleep throttle.
extern void game_flush_inputs(void);
extern void stop_time(void);
extern void start_time(void);
extern void write_player_file(void);            // main/playsave.c
extern void showRenderBuffer(void);             // Descent/src/main/cpp/render.c -- actually
                                                 // presents the frame (eglSwapBuffers); every
                                                 // other menu loop in this codebase calls this
                                                 // once per iteration (see main/newmenu.c's
                                                 // newmenu_do3) or nothing new ever becomes
                                                 // visible on screen.

void do_remap_gamepad_menu(void) {
	grs_canvas *save_canvas;
	grs_font *save_font;
	int staging[GP_NUM_ACTIONS];
	int staging_layout;
	int i, w, h, aw, k, ek, citem, ocitem, time_stopped = 0;
	int captured;
	int mouse_x, mouse_y, mouse_up, tapped_row;

	memcpy(staging, Gamepad_bound_keycodes, sizeof(staging));
	staging_layout = Gamepad_stick_layout;

	if (!((Game_mode & GM_MULTI) && (Function_mode == FMODE_GAME) && (!Endlevel_sequence))) {
		time_stopped = 1;
		stop_time();
	}

	save_canvas = grd_curcanv;
	gr_set_current_canvas(NULL);
	save_font = grd_curcanv->cv_font;
	game_flush_inputs();

	// Save the same darkened backdrop into VR_offscreen_buffer that we're about to draw
	// onto the visible canvas, exactly like main/newmenu.c's newmenu_do3() does before
	// any menu box is drawn. This is what lets nm_restore_background() (used by
	// gamepad_remap_draw_row()/gamepad_remap_draw_prompt(), and again below when this
	// screen closes) correctly repaint a region from the real backdrop instead of flat
	// black. Deliberately no gr_clear_canvas() here -- clearing first would blank out
	// the live game/menu scene nm_draw_background() is supposed to be darkening, which
	// is exactly what made the background solid black everywhere after this screen closed.
	gr_set_current_canvas(VR_offscreen_buffer);
	nm_draw_background(0, 0, grd_curcanv->cv_bitmap.bm_w - 1, grd_curcanv->cv_bitmap.bm_h - 1);
	gr_set_current_canvas(NULL);
	nm_draw_background(0, 0, grd_curcanv->cv_bitmap.bm_w - 1, grd_curcanv->cv_bitmap.bm_h - 1);

	// Title uses the same font+color as every other menu's title (TITLE_FONT /
	// GR_GETCOLOR(31,31,31) in newmenu.c's newmenu_do3()) instead of the smaller
	// subtitle-sized font this screen originally (and mistakenly) used.
	grd_curcanv->cv_font = GP_TITLE_FONT;
	gr_set_fontcolor(GR_GETCOLOR(31, 31, 31), -1);
	gr_scale_string(0x8000, GP_TITLE_Y * f2fl(Scale_y), Scale_factor, Scale_factor, "Remap Gamepad");

	// Row spacing is sized off the *unselected* row font (GP_NORMAL_FONT), same as
	// newmenu_do3() sizes every item's slot off NORMAL_FONT even though the selected
	// item later draws bigger in CURRENT_FONT -- this is the existing, already-shipping
	// behavior of every other menu in the game, not something new to work around here.
	// NOTE: keep this height UNscaled -- Gamepad_remap_row_y[] below multiplies
	// (GP_ROWS_START_Y + i * Gamepad_remap_row_h) by Scale_y as a single final step, so
	// pre-scaling h here would double-apply the scale factor and blow the row spacing
	// out (this is exactly what happened: rows ended up so far apart only the first two
	// fit on screen, with the rest still reachable -- just off-canvas -- via the D-pad).
	grd_curcanv->cv_font = GP_NORMAL_FONT;
	gr_get_string_size("Ay", &w, &h, &aw);
	Gamepad_remap_row_h = h + 4;

	// Erase-box height: scaled (unlike row_h above), and sized off CURRENT_FONT -- the
	// taller of the two row fonts -- so it's tall enough to fully cover either font's
	// glyphs no matter which one a row was last drawn with.
	Gamepad_remap_erase_h = (int) (GP_CURRENT_FONT->ft_h * f2fl(Scale_y)) + 4;

	for (i = 0; i < GP_NUM_ROWS; i++) {
		Gamepad_remap_row_y[i] = (GP_ROWS_START_Y + i * Gamepad_remap_row_h) * f2fl(Scale_y);
	}

	for (i = 0; i < GP_NUM_ROWS; i++) {
		gamepad_remap_draw_row(i, staging, staging_layout, 0);
	}
	citem = 0;
	gamepad_remap_draw_row(citem, staging, staging_layout, 1);
#ifdef OGLES
	showRenderBuffer();
#endif

	Gamepad_remap_screen_active = 1;

	for (;;) {
		k = key_inkey();

		if (k == KEY_ESC) {
			// Start/ESC at the row-list level == Cancel: discard staging, leave.
			break;
		}

		ocitem = citem;
		switch (k) {
			case KEY_UP:
				citem = (citem == 0) ? GP_NUM_ROWS - 1 : citem - 1;
				break;
			case KEY_DOWN:
				citem = (citem == GP_NUM_ROWS - 1) ? 0 : citem + 1;
				break;
		}

		// Touch: a tap-up over a row selects AND confirms it in one gesture, the same
		// as tapping an NM_TYPE_MENU row (e.g. "Video Options") does in the parent
		// Options menu. mouse_button_up_count() only reports a fresh release, so this
		// fires once per tap rather than every frame the finger happens to be up.
		tapped_row = -1;
		mouse_up = mouse_button_up_count(0, &mouse_x, &mouse_y);
		if (mouse_up) {
			tapped_row = gamepad_remap_row_at(mouse_x, mouse_y);
			if (tapped_row != -1) {
				citem = tapped_row;
			}
		}

		if (ocitem != citem) {
			gamepad_remap_draw_row(ocitem, staging, staging_layout, 0);
			gamepad_remap_draw_row(citem, staging, staging_layout, 1);
		}

		// Present every iteration, unconditionally, before any continue/break below --
		// otherwise a cursor move (or the tail end of a Reset/capture) would draw into
		// the backbuffer but never actually reach the screen.
#ifdef OGLES
		showRenderBuffer();
#endif

		// A tap on a row confirms it just like the physical-A latch does -- citem is
		// already pointed at that row above, so everything below (Cancel/Apply/Reset/
		// Stick Layout/capture) treats a confirmed tap exactly like a confirmed A press.
		if (!gamepad_remap_poll_confirm() && tapped_row == -1) {
			continue;
		}

		if (citem == GP_ROW_CANCEL) {
			break;
		}

		if (citem == GP_ROW_APPLY) {
			memcpy(Gamepad_bound_keycodes, staging, sizeof(staging));
			Gamepad_stick_layout = staging_layout;
			write_player_file();
			break;
		}

		if (citem == GP_ROW_RESET) {
			for (i = 0; i < GP_NUM_ACTIONS; i++)
				staging[i] = Gamepad_remap_actions[i].defaultKeyCode;
			staging_layout = 0;
			for (i = 0; i < GP_NUM_ACTIONS; i++)
				gamepad_remap_draw_row(i, staging, staging_layout, i == citem);
			gamepad_remap_draw_row(GP_ROW_STICK_LAYOUT, staging, staging_layout, GP_ROW_STICK_LAYOUT == citem);
			continue;
		}

		if (citem == GP_ROW_STICK_LAYOUT) {
			// Not a "press a button" row -- confirming it just cycles between the two
			// layouts in place, same as flipping a checkbox elsewhere in the game.
			staging_layout = !staging_layout;
			gamepad_remap_draw_row(citem, staging, staging_layout, 1);
			continue;
		}

		// citem is an action row -- capture a new binding for it.
		gamepad_remap_draw_prompt("Press a button for this action...  (Start to cancel)");
		gamepad_remap_start_capture();
#ifdef OGLES
		showRenderBuffer();
#endif
		for (;;) {
			ek = key_inkey();
			delay(10);
			captured = Gamepad_remap_captured_keycode;
#ifdef OGLES
			showRenderBuffer();
#endif
			if (ek == KEY_ESC) {
				gamepad_remap_cancel_capture();
				break;
			}
			if (captured != GP_UNBOUND) {
				staging[citem] = captured;
				for (i = 0; i < GP_NUM_ACTIONS; i++) {
					if (i != citem && staging[i] == captured) {
						staging[i] = GP_UNBOUND;
					}
				}
				gamepad_remap_cancel_capture();
				break;
			}
		}
		gamepad_remap_draw_prompt(NULL);
		for (i = 0; i < GP_NUM_ACTIONS; i++)
			gamepad_remap_draw_row(i, staging, staging_layout, i == citem);
	}

	Gamepad_remap_screen_active = 0;

	// This screen paints edge-to-edge (nm_draw_background(0,0,bm_w-1,bm_h-1) at the top),
	// unlike the stock Options menu, which is a smaller, content-sized box. Without
	// undoing that full-screen paint here, whatever this screen drew outside that
	// smaller box -- the title, the wider row highlights -- would just sit there
	// un-touched once do_options_menu() redraws only its own (smaller) box on top,
	// visible as this screen "still showing behind" the one that replaced it.
	//
	// The fix is a full-screen nm_restore_background(), NOT a flat black clear (which is
	// what used to be here): a flat clear does erase this screen's own drawing, but it
	// also destroys the real game/menu backdrop nm_draw_background() darkened on entry,
	// which is why the background turned solid black everywhere -- main menu, in-game,
	// even other menus -- from that point on. Restoring from the saved backdrop erases
	// this screen while leaving the actual scene underneath intact, same as every other
	// menu closing normally.
	nm_restore_background(0, 0, grd_curcanv->cv_bitmap.bm_w, grd_curcanv->cv_bitmap.bm_h);
#ifdef OGLES
	showRenderBuffer();
#endif

	grd_curcanv->cv_font = save_font;
	gr_set_current_canvas(save_canvas);
	game_flush_inputs();
	if (time_stopped) start_time();
}
