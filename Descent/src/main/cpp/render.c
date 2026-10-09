//
// Created by devin on 4/17/16.
//

#include <EGL/egl.h>
#include <jni.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include "game.h"
#include "gamefont.h"
#include "texmerge.h"

bool Surface_was_destroyed = false;

// Bumped every time the EGL surface/context is torn down and recreated (app minimized and
// restored, etc.). Custom-drawn screens that paint their background once -- see
// do_remap_gamepad_menu() in gamepad_remap.c -- compare against this to know when
// everything they put on screen is gone and needs a full repaint.
int Surface_recreate_count = 0;

extern JavaVM *jvm;
extern jobject Descent_view;
extern bool Want_pause;
extern grs_bitmap nm_background;
extern int can_save_screen;
extern void ogles_draw_saved_screen(GLuint saved_screen_tex);

// 1 when the driver really keeps the previous frame's pixels across eglSwapBuffers() (what
// the game's menus/fades assume); 0 when it refused, in which case showRenderBuffer() below
// emulates it. See setPreservedSwapBehavior().
static int Swap_preserved = 1;
// Emulation state for Swap_preserved == 0: a copy of the frame that was just presented.
static GLuint Last_frame_tex = 0;
static GLint Last_frame_w = 0, Last_frame_h = 0;

extern void draw_buttons();
extern void digi_close_digi();
extern void digi_init_digi();
extern void mouse_handler(short x, short y, bool down);

void getRenderBufferSize(GLint *width, GLint *height) {
	eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH, width);
	eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_HEIGHT, height);
}

// Asks EGL to keep the previous frame's contents across eglSwapBuffers() -- the game depends on
// that (menus/briefings/fades read back the frame that was just presented, see
// ogles_save_screen()). Logs what the driver actually did, since some GPUs/drivers refuse.
void setPreservedSwapBehavior() {
	EGLDisplay display = eglGetCurrentDisplay();
	EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
	EGLint behavior = 0;
	EGLBoolean ok = eglSurfaceAttrib(display, surface, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED);
	EGLint err = ok ? EGL_SUCCESS : eglGetError();
	eglQuerySurface(display, surface, EGL_SWAP_BEHAVIOR, &behavior);
	Swap_preserved = (ok && behavior == EGL_BUFFER_PRESERVED);
	Last_frame_tex = 0;   // belongs to the previous context, if any
	Last_frame_w = Last_frame_h = 0;
	__android_log_print(ANDROID_LOG_INFO, "DescentGL",
						"preserved swap: request %s (error 0x%x), surface behavior now %s",
						ok ? "accepted" : "REFUSED", (unsigned) err,
						behavior == EGL_BUFFER_PRESERVED ? "PRESERVED" : "DESTROYED");
}

// Emulates EGL_BUFFER_PRESERVED on drivers that won't do it: after each swap the new back
// buffer holds stale/undefined pixels, but the rest of the game redraws only what changed
// (or reads the previous frame back for fades and menu backgrounds). So copy the finished
// frame into a texture just before the swap, and paint it straight back right after.
static void emulate_preserved_before_swap(void) {
	GLint w, h;
	getRenderBufferSize(&w, &h);
	if (w <= 0 || h <= 0) {
		return;
	}
	glEnable(GL_TEXTURE_2D);
	if (Last_frame_tex == 0 || w != Last_frame_w || h != Last_frame_h) {
		if (Last_frame_tex == 0) {
			glGenTextures(1, &Last_frame_tex);
		}
		glBindTexture(GL_TEXTURE_2D, Last_frame_tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, w, h, 0);
		Last_frame_w = w;
		Last_frame_h = h;
	} else {
		glBindTexture(GL_TEXTURE_2D, Last_frame_tex);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
	}
}

static void emulate_preserved_after_swap(void) {
	GLboolean depth, cull, blend;
	if (Last_frame_tex == 0) {
		return;
	}
	depth = glIsEnabled(GL_DEPTH_TEST);
	cull = glIsEnabled(GL_CULL_FACE);
	blend = glIsEnabled(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	ogles_draw_saved_screen(Last_frame_tex);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	if (depth) glEnable(GL_DEPTH_TEST);
	if (cull) glEnable(GL_CULL_FACE);
	if (blend) glEnable(GL_BLEND);
}

void showRenderBuffer() {
	int i;
	EGLContext eglContext;
	EGLDisplay eglDisplay;
	EGLSurface eglSurface;
	grs_font *font;
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	// Surface_was_destroyed is checked here too, not just Want_pause: the SurfaceHolder
	// callbacks (surfaceDestroyed()/surfaceCreated()) are a separate Android lifecycle from
	// the Activity's onPause()/onResume() (which is the only thing that sets Want_pause, see
	// DescentActivity.descentPause()), and under rapid pause/resume spamming the surface can
	// get torn down without Want_pause having been set yet for that same cycle. Without this,
	// this function fell through to the "normal frame" branch below and kept calling
	// eglSwapBuffers()/drawing against a Surface Android had already abandoned -- which fails
	// instantly instead of blocking for vsync, so the render loop spun as fast as the CPU
	// allowed, logging thousands of "BufferQueue has been abandoned" failures within
	// milliseconds until some later Want_pause finally caught up.
	if (Want_pause || Surface_was_destroyed) {
		// Save this in case we need to destroy it later
		eglContext = eglGetCurrentContext();
		eglDisplay = eglGetCurrentDisplay();
		eglSurface = eglGetCurrentSurface(EGL_DRAW);

		// Close digi so another application can use the OpenSL ES objects
		digi_close_digi();

		(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
		clazz = (*env)->FindClass(env, "wootbeer/descent/DescentView");

		// Pause this thread
		method = (*env)->GetMethodID(env, clazz, "pauseRenderThread", "()V");
		(*env)->CallVoidMethod(env, Descent_view, method);

		digi_init_digi();

		if (Surface_was_destroyed) {
			// Purge all texture assets, since the EGL context will be blown away
			for (i = 0; i < MAX_FONTS; ++i) {
				font = Gamefonts[i];
				if (!font || !font->ft_ogles_texes)
					continue;
				glDeleteTextures(font->ft_maxchar - font->ft_minchar, font->ft_ogles_texes);
				memset(font->ft_ogles_texes, 0,
					   (font->ft_maxchar - font->ft_minchar) * sizeof(GLuint));
			}
			for (i = 0; i < MAX_BITMAP_FILES; ++i) {
				glDeleteTextures(1, &GameBitmaps[i].bm_ogles_tex_id);
				GameBitmaps[i].bm_ogles_tex_id = 0;
			}
			texmerge_close();
			texmerge_init(50);
			glDeleteTextures(1, &nm_background.bm_ogles_tex_id);
			nm_background.bm_ogles_tex_id = 0;

			// Blow away EGL surface and context
			eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroySurface(eglDisplay, eglSurface);
			eglDestroyContext(eglDisplay, eglContext);
			eglTerminate(eglDisplay);

			// Reset EGL context. initEgl() can end up with no current context if the new Surface was
			// already destroyed again by the time it ran (apps being switched in and out quickly);
			// every GL call after that crashes the process, so keep retrying until it takes -- waiting
			// for the next surfaceCreated() whenever the surface is known to be gone again.
			method = (*env)->GetMethodID(env, clazz, "initEgl", "()V");
			{
				int attempts = 0;
				while (1) {
					jmethodID pause_method, valid_method;
					Surface_was_destroyed = false;
					(*env)->CallVoidMethod(env, Descent_view, method);
					if ((*env)->ExceptionCheck(env)) {
						(*env)->ExceptionClear(env);
					}
					if (eglGetCurrentContext() != EGL_NO_CONTEXT &&
						eglGetCurrentSurface(EGL_DRAW) != EGL_NO_SURFACE) {
						break;
					}
					valid_method = (*env)->GetMethodID(env, clazz, "surfaceIsValid", "()Z");
					if (!(*env)->CallBooleanMethod(env, Descent_view, valid_method)) {
						// Wait for the next surfaceCreated() (which resumes this thread)
						pause_method = (*env)->GetMethodID(env, clazz, "pauseRenderThread", "()V");
						(*env)->CallVoidMethod(env, Descent_view, pause_method);
					} else {
						usleep(50000);
					}
					if (++attempts > 200) {
						break;
					}
				}
			}
			(*env)->DeleteLocalRef(env, clazz);
			if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
				// Still no context after every retry; flag it so the next frame tries again
				// rather than drawing into nothing.
				Surface_was_destroyed = true;
				return;
			}
			setPreservedSwapBehavior();
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			Surface_recreate_count++;

			// Hack to show stuff like menus
			if (Game_mode != GM_NORMAL || In_screen) {
				mouse_handler(-1, -1, true);
				mouse_handler(-1, -1, false);
			}

			// (Surface_was_destroyed was cleared just before initEgl() above, so a destroy that
			// happens after that point is still pending for the next frame.)
		}

		Want_pause = false;
	} else {
		draw_buttons();
		if (!Swap_preserved) {
			emulate_preserved_before_swap();
		}
		eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_READ));
		if (!Swap_preserved) {
			emulate_preserved_after_swap();
		}
		can_save_screen = !can_save_screen;
	}
}

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_surfaceWasDestroyed(JNIEnv *env, jclass type) {
	Surface_was_destroyed = true;
}
