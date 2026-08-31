//
// Created by devin on 4/19/16.
//
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdlib.h>
#include <unwind.h>
#include <EGL/egl.h>
#include "gr.h"
#include "cfile.h"

JavaVM *jvm;
jobject Activity;
jobject Descent_view;
bool Want_pause;

extern int descent_main(int argc, char **argv);

extern void init_buttons(jint w, jint h);

// --- Minimal self-contained native crash handler ---
// The device's own tombstone/crash-dump service isn't producing a usable
// backtrace, so capture and log our own via libunwind + dladdr when we
// abort/segfault. This is intentionally standalone (no external deps).

typedef struct {
	void **current;
	void **end;
} BacktraceState;

static _Unwind_Reason_Code unwind_callback(struct _Unwind_Context *context, void *arg) {
	BacktraceState *state = (BacktraceState *) arg;
	uintptr_t pc = _Unwind_GetIP(context);
	if (pc) {
		if (state->current == state->end) {
			return _URC_END_OF_STACK;
		}
		*state->current++ = (void *) pc;
	}
	return _URC_NO_REASON;
}

static void crash_signal_handler(int sig) {
	const int max_frames = 40;
	void *buffer[max_frames];
	BacktraceState state = {buffer, buffer + max_frames};

	_Unwind_Backtrace(unwind_callback, &state);
	int frame_count = (int) (state.current - buffer);

	__android_log_print(ANDROID_LOG_FATAL, "DescentCrash",
						 "Caught signal %d -- backtrace (%d frames):", sig, frame_count);
	for (int i = 0; i < frame_count; ++i) {
		Dl_info info;
		if (dladdr(buffer[i], &info) && info.dli_fname) {
			__android_log_print(ANDROID_LOG_FATAL, "DescentCrash",
								 "  #%02d pc %p  %s (%s+%d)", i, buffer[i], info.dli_fname,
								 info.dli_sname ? info.dli_sname : "??",
								 info.dli_saddr ? (int) ((char *) buffer[i] - (char *) info.dli_saddr) : 0);
		} else {
			__android_log_print(ANDROID_LOG_FATAL, "DescentCrash",
								 "  #%02d pc %p  <unknown>", i, buffer[i]);
		}
	}

	// Restore default handling and re-raise so the process still dies normally.
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install_crash_handler() {
	signal(SIGABRT, crash_signal_handler);
	signal(SIGSEGV, crash_signal_handler);
	signal(SIGBUS, crash_signal_handler);
}

// Install as early as possible -- this runs the moment System.loadLibrary()
// loads the .so, well before descentMain() is ever called.
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
	install_crash_handler();
	return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_descentMain(JNIEnv *env, jclass type, jint w, jint h,
												  jobject activity, jobject descent_view, jobject asset_manager,
												  jstring document_path, jstring cache_path, jboolean quick_resume) {
	char ws[6], hs[6];

	// Init globals
	Want_pause = false;
	Activity = activity;
	Descent_view = descent_view;
	(*env)->GetJavaVM(env, &jvm);

	// Need to preserve the buffer for things to display corretly
	eglSurfaceAttrib(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_SWAP_BEHAVIOR,
					 EGL_BUFFER_PRESERVED);

	// Get Android things for accessing files
	Asset_manager = AAssetManager_fromJava(env, asset_manager);
	Document_path = (*env)->GetStringUTFChars(env, document_path, NULL);
	Cache_path = (*env)->GetStringUTFChars(env, cache_path, NULL);

	// Start Descent with the magical hacked command line args! "-quickresume" is added the same
	// way when this launch is the one immediately following a settings-triggered auto-restart
	// (see DescentActivity.restartAppForSettingsChange()) -- native's FindArg("-quickresume")
	// checks in main/inferno.c, main/gameseq.c and main/menu.c pick it up from here.
	sprintf(ws, "%d", (int) w);
	sprintf(hs, "%d", (int) h);
	init_buttons(w, h);
	if (quick_resume) {
		const char *args[] = {"", "-width", ws, "-height", hs, "-quickresume"};
		descent_main(6, (char **) args);
	} else {
		const char *args[] = {"", "-width", ws, "-height", hs};
		descent_main(5, (char **) args);
	}
}

JNIEXPORT void JNICALL Java_wootbeer_descent_DescentActivity_descentPause(JNIEnv *env, jclass type) {
	Want_pause = true;
}
