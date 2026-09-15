//
// Created by devin on 4/21/16.
//

#include <game.h>
#include <jni.h>
#include <stdlib.h>
#include "key.h"

#define NUM_BUTTONS 21
#define MAX_KEYS 2

#define ACCELERATE_BTN 0
#define REVERSE_BTN 1
#define SLIDE_LEFT_BTN 2
#define SLIDE_RIGHT_BTN 3
#define SLIDE_UP_BTN 4
#define SLIDE_DOWN_BTN 5
#define BANK_LEFT_BTN 6
#define BANK_RIGHT_BTN 7
#define FIRE_PRIMARY_BTN 8
#define FIRE_SECONDARY_BTN 9
#define TOGGLE_PRIMARY_BTN 10
#define TOGGLE_SECONDARY_BTN 11
#define FIRE_FLARE_BTN 12
#define ACCELERATE_SLIDE_LEFT_BTN 13
#define ACCELERATE_SLIDE_RIGHT_BTN 14
#define REVERSE_SLIDE_LEFT_BTN 15
#define REVERSE_SLIDE_RIGHT_BTN 16
#define MENU_BTN 17
#define MAP_BTN 18
#define REAR_VIEW_BTN 19
#define TOGGLE_COCKPIT_BTN 20

#define ACTION_DOWN 0
#define ACTION_UP 1
#define ACTION_MOVE 2
#define ACTION_POINTER_DOWN 5
#define ACTION_POINTER_UP 6

typedef struct GameButton {
	int x, y, w, h;
	unsigned char keys[MAX_KEYS];
	int nKeys;
	bool activateOnHover;
} GameButton;

GameButton buttons[NUM_BUTTONS];
int touchButtons[10];
int trackingTouch;

// Set from Java whenever a gamepad-class InputDevice is connected/disconnected.
// While true, the on-screen touch buttons are neither drawn nor hit-tested.
bool Gamepad_connected = false;

extern JavaVM *jvm;
extern jobject Activity;
extern ubyte gr_current_pal[256 * 3];
extern int touch_dx, touch_dy;

extern void key_handler(unsigned char scancode, bool down);

jfloat pt_conv(const char *method_name, jfloat arg) {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;
	jfloat retval;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, method_name, "(F)F");
	retval = (*env)->CallFloatMethod(env, Activity, method, arg);
	(*env)->DeleteLocalRef(env, clazz);
	return retval;
}

jfloat px_to_dp(jfloat px) {
	return pt_conv("pxToDp", px);
}

jfloat dp_to_px(jfloat dp) {
	return pt_conv("dpToPx", dp);
}

// Touch control size preference -- Options menu "Touch Scaling" slider (main/menu.c). 0-8,
// same convention as every other Options menu slider (Config_joystick_sensitivity etc.),
// mapped through Touch_scale_values[] below rather than used directly, matching the
// byte-array detail-level tables in main/menu.c. Persisted per-pilot in main/playsave.c,
// the same trailing-field pattern as Config_joystick_sensitivity/Config_invert_y.
//
// Default (index 2, 1.00x) matches the size buttonSizeBias now produces on an ordinary
// phone after the "on-screen touch controls scaled way too large" fix
// (DescentActivity.java) -- i.e. leaving this slider untouched changes nothing from
// today's (fixed) behavior. Deliberately biased toward the high end (6 steps up from
// default, only 2 down): touch targets are already comfortable at the default, and most
// players adjusting this at all will want them bigger, not smaller -- see the same bug
// report.
#define TOUCH_SCALE_DEFAULT_INDEX 2
static const float Touch_scale_values[9] = {
		0.90f, 0.95f, 1.00f, 1.10f, 1.20f, 1.35f, 1.50f, 1.65f, 1.80f
};
ubyte Config_touch_control_scale = TOUCH_SCALE_DEFAULT_INDEX;

// Cached logical (dp, pre-scale) render-buffer size, and the actual render-buffer pixel
// size, from the most recent init_buttons() call. layout_buttons() below re-reads these
// every time it runs so the touch-scaling slider (and a freshly-loaded pilot's saved
// preference) can redo the whole button layout on demand without needing a fresh w/h
// from Java. The px versions are only used to clamp the result on-screen -- see the
// bottom of layout_buttons() below.
static jint g_buttons_w_dp, g_buttons_h_dp;
static jint g_buttons_w_px, g_buttons_h_px;

// Rebuilds buttons[] from the cached logical size and the current Touch_scale_values[]
// selection. Split out of init_buttons() below so it can also be re-run any time
// Config_touch_control_scale changes -- from the Options menu (do_options_menu(),
// main/menu.c) or right after a pilot's saved value loads (read_player_file(),
// main/playsave.c) -- without needing another w/h from Java.
static void layout_buttons(void) {
	int i;
	float menuSpacing;
	float scale = Touch_scale_values[Config_touch_control_scale];
	jint w = g_buttons_w_dp;
	jint h = g_buttons_h_dp;

	// Applies the touch-scaling slider to one dp literal below -- both a button's own
	// size (55, 70, ...) and the small offset that positions it near its corner (185,
	// 95, ...). Deliberately NOT applied to w/h themselves (the full logical screen
	// size): every position below is written as "distance from an edge" (a small
	// literal, or w/h minus one), and w/h stay exactly what the screen actually
	// measures however big Touch_scale_values[] gets -- so growing a button only ever
	// grows it and pushes it a little further from its corner, it never drags the
	// whole cluster toward the middle of the screen or past the far edge the way
	// naively scaling the final combined position (e.g. "w - 95") did. See the
	// "touch scaling just makes the controls go off screen" report.
#define SC(v) ((int) ((v) * scale))

	menuSpacing = (w - 150) / 3;

	// Define buttons
	buttons[ACCELERATE_BTN] = (struct GameButton) {SC(120), h - SC(185), SC(55), SC(55), {KEY_A}, 1, true};
	buttons[REVERSE_BTN] = (struct GameButton) {SC(120), h - SC(75), SC(55), SC(55), {KEY_Z}, 1, true};
	buttons[SLIDE_LEFT_BTN] = (struct GameButton) {SC(65), h - SC(130), SC(55), SC(55), {KEY_PAD1}, 1, true};
	buttons[SLIDE_RIGHT_BTN] = (struct GameButton) {SC(175), h - SC(130), SC(55), SC(55), {KEY_PAD3}, 1, true};
	buttons[SLIDE_UP_BTN] = (struct GameButton) {SC(25), h - SC(185), SC(35), SC(80), {KEY_PADMINUS}, 1, true};
	buttons[SLIDE_DOWN_BTN] = (struct GameButton) {SC(25), h - SC(100), SC(35), SC(80), {KEY_PADPLUS}, 1, true};
	buttons[BANK_LEFT_BTN] = (struct GameButton) {SC(65), h - SC(225), SC(80), SC(35), {KEY_Q}, 1, true};
	buttons[BANK_RIGHT_BTN] = (struct GameButton) {SC(150), h - SC(225), SC(80), SC(35), {KEY_E}, 1, true};
	buttons[ACCELERATE_SLIDE_LEFT_BTN] = (struct GameButton) {SC(65), h - SC(185), SC(55), SC(55),
															  {KEY_A, KEY_PAD1}, 2, true};
	buttons[ACCELERATE_SLIDE_RIGHT_BTN] = (struct GameButton) {SC(175), h - SC(185), SC(55), SC(55),
															   {KEY_A, KEY_PAD3}, 2, true};
	buttons[REVERSE_SLIDE_LEFT_BTN] = (struct GameButton) {SC(65), h - SC(75), SC(55), SC(55),
														   {KEY_Z, KEY_PAD1}, 2, true};
	buttons[REVERSE_SLIDE_RIGHT_BTN] = (struct GameButton) {SC(175), h - SC(75), SC(55), SC(55),
															{KEY_Z, KEY_PAD3}, 2, true};
	buttons[FIRE_PRIMARY_BTN] = (struct GameButton) {w - SC(95), h - SC(170), SC(70), SC(70),
													 {KEY_LCTRL}, 1, false};
	buttons[FIRE_SECONDARY_BTN] = (struct GameButton) {w - SC(175), h - SC(90), SC(70), SC(70),
													   {KEY_SPACEBAR}, 1, false};
	buttons[FIRE_FLARE_BTN] = (struct GameButton) {w - SC(85), h - SC(80), SC(50), SC(50), {KEY_F}, 1, false};
	buttons[TOGGLE_PRIMARY_BTN] = (struct GameButton) {w - SC(95), h - SC(225), SC(70), SC(40), {KEY_1}, 1, false};
	buttons[TOGGLE_SECONDARY_BTN] = (struct GameButton) {w - SC(230), h - SC(90), SC(40), SC(70),
														 {KEY_6}, 1, false};
	buttons[MENU_BTN] = (struct GameButton) {SC(25), SC(20), SC(25), SC(25), {KEY_ESC}, 1, false};
	buttons[MAP_BTN] = (struct GameButton) {SC(50) + (int) menuSpacing, SC(20), SC(25), SC(25),
											{KEY_TAB}, 1, false};
	buttons[TOGGLE_COCKPIT_BTN] = (struct GameButton) {SC(75) + (int) (menuSpacing * 2), SC(20), SC(25), SC(25),
													   {KEY_F3}, 1, false};
	buttons[REAR_VIEW_BTN] = (struct GameButton) {SC(100) + (int) (menuSpacing * 3), SC(20), SC(25), SC(25),
												  {KEY_R}, 1, false};

#undef SC

	for (i = 0; i < NUM_BUTTONS; ++i) {
		buttons[i].x = (int) dp_to_px(buttons[i].x);
		buttons[i].y = (int) dp_to_px(buttons[i].y);
		buttons[i].w = (int) dp_to_px(buttons[i].w);
		buttons[i].h = (int) dp_to_px(buttons[i].h);

		// Belt-and-suspenders safety clamp: whatever Touch_scale_values[] and this
		// device's density/screen size add up to, never let a button's box start off
		// the top/left of the render buffer or extend past its bottom/right. The
		// anchored-offset math above already keeps this from happening in ordinary
		// cases, but an unusually small/narrow screen at the slider's largest setting
		// could still run out of room -- clamping means the worst case is a button
		// pinned flush against the edge, not one that's partly or fully unreachable.
		if (buttons[i].x < 0) buttons[i].x = 0;
		if (buttons[i].y < 0) buttons[i].y = 0;
		if (buttons[i].x + buttons[i].w > g_buttons_w_px) buttons[i].x = g_buttons_w_px - buttons[i].w;
		if (buttons[i].y + buttons[i].h > g_buttons_h_px) buttons[i].y = g_buttons_h_px - buttons[i].h;
	}
}

void init_buttons(jint w, jint h) {
	g_buttons_w_px = w;
	g_buttons_h_px = h;
	g_buttons_w_dp = (jint) px_to_dp(w);
	g_buttons_h_dp = (jint) px_to_dp(h);
	layout_buttons();
}

// Called from main/menu.c (do_options_menu()) and main/playsave.c (read_player_file())
// whenever Config_touch_control_scale changes, so the on-screen controls immediately
// reflect it -- no app restart needed, unlike Render Scale/Force 4:3.
void touch_control_scale_changed(void) {
	layout_buttons();
}

void draw_buttons() {
	int i;
	grs_canvas *save_canv;

	if (Game_mode == GM_NORMAL && !In_screen && !Gamepad_connected) {
		save_canv = grd_curcanv;
		gr_set_current_canvas(NULL);
		gr_setcolor(BM_XRGB(63, 63, 63));
		Gr_scanline_darkening_level = 24;
		for (i = 0; i < NUM_BUTTONS; ++i) {
			if (i == 13) {
				Gr_scanline_darkening_level = 31;
			}
			if (i == 17) {
				Gr_scanline_darkening_level = 24;
			}
			gr_rect(buttons[i].x, buttons[i].y, buttons[i].x + buttons[i].w,
					buttons[i].y + buttons[i].h);
		}
		Gr_scanline_darkening_level = GR_FADE_LEVELS;
		gr_set_current_canvas(save_canv);
	}
}

bool point_in_button(jfloat x, jfloat y, const GameButton *button) {
	return x >= button->x && y >= button->y && x <= button->x + button->w && y <= button->y
																				  + button->h;
}

void handle_down(jint pointerId, jfloat x, jfloat y) {
	for (int i = 0; i < NUM_BUTTONS; ++i) {
		if (point_in_button(x, y, &buttons[i])) {
			touchButtons[pointerId] = -1;
			touchButtons[pointerId] = i;
			for (int j = 0; j < buttons[i].nKeys; ++j) {
				key_handler(buttons[i].keys[j], true);
			}
		}
	}
}

void handle_up(jint pointerId) {
	int buttonNumber;
	if (trackingTouch == pointerId) {
		trackingTouch = -1;
		touch_dx = touch_dy = 0;
	}
	buttonNumber = touchButtons[pointerId];
	if (buttonNumber == -1) {
		return;
	}
	for (int i = 0; i < buttons[buttonNumber].nKeys; ++i) {
		key_handler(buttons[buttonNumber].keys[i], false);
	}
	touchButtons[pointerId] = -1;
}

void handle_move(jint pointerId, jfloat x, jfloat y, jfloat prevX, jfloat prevY) {
	int currentButtonIndex, previousButtonIndex;
	int i, j;
	int nSharedKeys = 0;
	unsigned char sharedKeys[MAX_KEYS];
	bool ignoreKey;

	int prev;

	currentButtonIndex = previousButtonIndex = -1;

	// Track the touch for ship orientation
	// (go outside the movement pad so the player doesn't accedentally spin the ship)
	if (trackingTouch == -1 && x > buttons[BANK_RIGHT_BTN].x + buttons[BANK_RIGHT_BTN].w + 50) {
		trackingTouch = pointerId;
	} else if (trackingTouch == -1) {
		touch_dx = touch_dy = 0;
	}
	if (trackingTouch == pointerId) {
		touch_dx = (int) (x - prevX);
		touch_dy = (int) (prevY - y);
	}

	// Get the button the touch is currently in
	for (i = 0; i < NUM_BUTTONS; ++i) {
		if (point_in_button(x, y, &buttons[i])) {
			currentButtonIndex = i;
			break;
		}
	}

	// Find the button the touch was previously in
	prev = touchButtons[pointerId];
	if (prev != -1) {
		previousButtonIndex = prev;
	}

	// Might press button if we hover over it
	if (currentButtonIndex > -1 && previousButtonIndex == -1 &&
		buttons[currentButtonIndex].activateOnHover) {
		touchButtons[pointerId] = -1;
		touchButtons[pointerId] = i;
		for (i = 0; i < buttons[currentButtonIndex].nKeys; ++i) {
			key_handler(buttons[currentButtonIndex].keys[i], true);
		}
	}

		// We changed buttons. Let the fun begin!
	else if (currentButtonIndex > -1 && previousButtonIndex > -1 &&
			 currentButtonIndex != previousButtonIndex) {
		touchButtons[pointerId] = -1;
		touchButtons[pointerId] = currentButtonIndex;

		// Find the keys the two buttons have in common
		for (i = 0; i < buttons[currentButtonIndex].nKeys; ++i) {
			for (j = 0; j < buttons[previousButtonIndex].nKeys; ++j) {
				if (buttons[currentButtonIndex].keys[i] == buttons[previousButtonIndex].keys[j]) {
					sharedKeys[nSharedKeys] = buttons[currentButtonIndex].keys[i];
					++nSharedKeys;
				}
			}
		}

		// Press all the keys that are in the new button that weren't in the old one
		for (i = 0; i < buttons[currentButtonIndex].nKeys; ++i) {
			ignoreKey = false;
			for (j = 0; j < nSharedKeys; ++j) {
				if (buttons[currentButtonIndex].keys[i] == sharedKeys[j]) {
					ignoreKey = true;
					break;
				}
			}
			if (!ignoreKey) {
				key_handler(buttons[currentButtonIndex].keys[i], true);
			}
		}

		// Release all the keys that were in the old button that aren't in the new one
		for (i = 0; i < buttons[previousButtonIndex].nKeys; ++i) {
			ignoreKey = false;
			for (j = 0; j < nSharedKeys; ++j) {
				if (buttons[previousButtonIndex].keys[i] == sharedKeys[j]) {
					ignoreKey = true;
					break;
				}
			}
			if (!ignoreKey) {
				key_handler(buttons[previousButtonIndex].keys[i], false);
			}
		}
	}
}

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_touchHandler(JNIEnv *env, jclass type,
													   jint action, jint pointerId,
													   jfloat x, jfloat y, jfloat prevX,
													   jfloat prevY) {
	if (Game_mode == GM_NORMAL && !In_screen && !Gamepad_connected) {
		switch (action) {
			case ACTION_DOWN:
			case ACTION_POINTER_DOWN:
				handle_down(pointerId, x, y);
				break;
			case ACTION_UP:
			case ACTION_POINTER_UP:
				handle_up(pointerId);
				break;
			case ACTION_MOVE:
				handle_move(pointerId, x, y, prevX, prevY);
				break;
			default:
				return false;
		}
		return true;
	} else {
		return false;
	}
}

// Called from Java whenever the set of connected gamepad-class InputDevices
// changes (including the initial check at startup). While a gamepad is
// connected, the on-screen touch buttons are hidden and stop consuming touches.
JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_setGamepadConnected(JNIEnv *env, jclass type,
																			 jboolean connected) {
	Gamepad_connected = connected;
}