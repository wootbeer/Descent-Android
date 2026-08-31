//
// Created by devin on 4/17/16.
//

#include <EGL/egl.h>
#include <jni.h>
#include <string.h>
#include "game.h"
#include "gamefont.h"
#include "texmerge.h"

bool Surface_was_destroyed = false;

extern JavaVM *jvm;
extern jobject Descent_view;
extern bool Want_pause;
extern grs_bitmap nm_background;
extern int can_save_screen;

extern void draw_buttons();
extern void digi_close_digi();
extern void digi_init_digi();
extern void mouse_handler(short x, short y, bool down);

void getRenderBufferSize(GLint *width, GLint *height) {
	eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH, width);
	eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_HEIGHT, height);
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

			// Reset EGL context
			method = (*env)->GetMethodID(env, clazz, "initEgl", "()V");
			(*env)->CallVoidMethod(env, Descent_view, method);
			(*env)->DeleteLocalRef(env, clazz);
			eglSurfaceAttrib(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW),
							 EGL_SWAP_BEHAVIOR,
							 EGL_BUFFER_PRESERVED);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Hack to show stuff like menus
			if (Game_mode != GM_NORMAL || In_screen) {
				mouse_handler(-1, -1, true);
				mouse_handler(-1, -1, false);
			}

			Surface_was_destroyed = false;
		}

		Want_pause = false;
	} else {
		draw_buttons();
		eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_READ));
		can_save_screen = !can_save_screen;
	}
}

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_surfaceWasDestroyed(JNIEnv *env, jclass type) {
	Surface_was_destroyed = true;
}
