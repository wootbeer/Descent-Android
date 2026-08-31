//
// Created by devin on 4/17/16.
//

#include <jni.h>
#include "types.h"

extern JavaVM *jvm;
extern jobject Descent_view;

extern void key_handler(unsigned char scancode, bool down);

// Function_mode values, see lib/inferno.h -- only FMODE_GAME is needed here.
extern int Function_mode;
#define FMODE_GAME 2

// main/game.c. True whenever a newmenu-style screen (the in-game Options menu,
// a messagebox, etc.) is drawn as an overlay on top of gameplay, even though
// Function_mode stays FMODE_GAME the entire time -- e.g. the in-game Options
// menu opened mid-level via Config_menu_flag (see GameLoop()'s call site for
// do_options_menu() in main/game.c) never changes Function_mode, it just wraps
// the call with In_screen = true / In_screen = false. isInGame() alone can't
// tell menu-nav code "a menu wants keyboard-style input right now" in that
// case, since it only tracks Function_mode -- see isMenuOpen() below.
extern bool In_screen;

int textIsActive() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;
	jboolean retval;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentView");
	method = (*env)->GetMethodID(env, clazz, "textIsActive", "()Z");
	retval = (*env)->CallBooleanMethod(env, Descent_view, method);
	(*env)->DeleteLocalRef(env, clazz);
	return retval;
}

void activateText() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentView");
	method = (*env)->GetMethodID(env, clazz, "activateText", "()V");
	(*env)->CallVoidMethod(env, Descent_view, method);
	(*env)->DeleteLocalRef(env, clazz);
}

void deactivateText() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentView");
	method = (*env)->GetMethodID(env, clazz, "deactivateText", "()V");
	(*env)->CallVoidMethod(env, Descent_view, method);
	(*env)->DeleteLocalRef(env, clazz);
}

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_keyHandler(JNIEnv *env, jclass type, jchar c, jboolean down) {
	key_handler((unsigned char) c, down);
}

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_isInGame(JNIEnv *env, jclass type) {
	return (jboolean) (Function_mode == FMODE_GAME);
}

// True whenever ANY newmenu-style screen currently wants keyboard-style
// navigation input: the standalone main/options/pause menus (Function_mode !=
// FMODE_GAME) as well as the in-game Options overlay opened mid-level (Function_mode
// stays FMODE_GAME, but In_screen is true for as long as that overlay is up).
// DescentView.java uses this to decide when the analog sticks should behave as
// menu-navigation keys instead of flight controls -- see onGenericMotionEvent().
JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentView_isMenuOpen(JNIEnv *env, jclass type) {
	return (jboolean) (Function_mode != FMODE_GAME || In_screen);
}

// Set by DescentView.java whenever physical A is pressed while a menu wants
// keyboard-style input (isMenuOpen()) -- polled and cleared once per loop
// iteration by main/newmenu.c's menu loops (newmenu_do3() and
// newmenu_get_filename()), which treat it exactly like a touch/tap confirm on
// whatever item is currently highlighted. This is what lets A always mean
// "confirm" on any menu -- main menu, Options, the in-game pause overlay, the
// startup pilot-select screen -- even when A is currently bound to a gameplay
// action in the Remap Gamepad menu: DescentView.java only forwards A to that
// binding once no menu is open (see handleGamepadKey()).
static volatile int Gamepad_menu_confirm_pressed = 0;

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_gamepadMenuConfirm(JNIEnv *env, jclass type) {
	Gamepad_menu_confirm_pressed = 1;
}

// Poll-and-clear for native menu code to call directly (main/newmenu.c) -- not
// a JNI entry point, just a plain C helper next to the one above.
int gamepad_menu_confirm_poll(void) {
	int pressed = Gamepad_menu_confirm_pressed;
	Gamepad_menu_confirm_pressed = 0;
	return pressed;
}
