# Particle-effect slowdown debug logging (removed 2026-09, kept for reuse)

This was added to chase down a particle-effect slowdown a specific user reported, then
removed once it became clear that user was never going to actually capture and send back
`particle_debug.log` -- so it had been sitting active (and adding real overhead: an
`fflush()` on every single texture bind) in every build since with no upside. Not deleted
outright -- if a similar rendering-performance report comes in again, this is a ready-made
tool: paste the three blocks below back into place and it's live again.

## What it did

- On every app launch (`DescentView.surfaceCreated()`), opened `particle_debug.log` in the
  app's *external* files dir (`context.getExternalFilesDir(null)`) -- reachable from a
  normal file manager or over USB, no root/adb needed, so a non-developer could retrieve it.
- Every OGLES texture bind (`ogles_bm_bind_teximage_2d()` in `texmap/ogles/oglestex.c` --
  fires for essentially every sprite/particle draw) timed itself and appended a line: hit/miss
  (cheap rebind vs. full decode+upload), time spent in the `glIsTexture()` check alone, total
  time in the whole bind call, bitmap dimensions, and whether the source was RLE-compressed.
- `render.c`'s `showRenderBuffer()` dropped a `---frame N---` marker line into the same log
  on every presented frame, so bursts of binds could be matched back to the frame they
  happened in.

## Where it lived (3 files) -- exact code to restore

### `texmap/ogles/oglestex.c`

Right after the `#include "oglestex.h"` line, before `extern ubyte gr_current_pal[256*3];`:

```c
// TEMPORARY: particle-effect slowdown investigation (see ParticleDebug notes below and
// the matching call in DescentView.java's surfaceCreated()). Remove this whole block --
// particle_debug_log/particle_debug_init()/particle_debug_mark_frame(), the timing
// added to ogles_bm_bind_teximage_2d() below, the JNI declaration in DescentView.java,
// and the particle_debug_mark_frame() call in render.c -- once the investigation is
// done.
#include <jni.h>
#include <stdio.h>
#include <time.h>

static FILE *particle_debug_log = NULL;
static int particle_debug_frame = 0;

static long long particle_debug_now_us() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long) ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

// Called once from Java right before descentMain() starts, with
// context.getExternalFilesDir(null) -- that's app-specific storage on the shared/external
// partition, so (unlike internal storage) it's reachable from a normal file manager or
// over USB without root or adb, which is what makes it easy for someone who isn't a
// developer to actually retrieve this file.
JNIEXPORT void JNICALL Java_wootbeer_descent_DescentView_particleDebugInit(JNIEnv *env, jclass type, jstring log_dir) {
	const char *dir = (*env)->GetStringUTFChars(env, log_dir, NULL);
	char path[512];
	snprintf(path, sizeof(path), "%s/particle_debug.log", dir);
	particle_debug_log = fopen(path, "w");
	if (particle_debug_log) {
		fprintf(particle_debug_log,
				"# t=microsecond timestamp (monotonic clock, arbitrary origin)\n"
				"# hit=1 if the texture was already uploaded (cheap rebind), 0 if this call\n"
				"#   had to decode + upload it for the first time\n"
				"# isTexUs=microseconds spent in the glIsTexture() cache-check call alone\n"
				"# totalUs=microseconds spent in the whole bind call (includes the upload on a\n"
				"#   miss, so misses are expected to have a much larger totalUs than hits)\n"
				"# w/h=bitmap dimensions, rle=1 if it was RLE-compressed source data\n"
				"# ---frame N--- lines mark each time a frame is presented, so bursts of binds\n"
				"#   can be matched up to the frame they happened in\n");
		fflush(particle_debug_log);
	}
	(*env)->ReleaseStringUTFChars(env, log_dir, dir);
}

// Called once per presented frame from render.c's showRenderBuffer().
void particle_debug_mark_frame() {
	if (particle_debug_log) {
		fprintf(particle_debug_log, "---frame %d--- t=%lld\n", particle_debug_frame++,
				particle_debug_now_us());
		fflush(particle_debug_log);
	}
}
```

And inside `ogles_bm_bind_teximage_2d()`, wrap the existing `glIsTexture()` check/branch
like this (the `already_uploaded` check itself is NOT debug-only -- it drives the real
hit/miss branch -- only the timing calls and the final log write are):

```c
	// TEMPORARY: see the particle-debug block up top.
	long long debug_t_start = particle_debug_now_us();
	GLboolean already_uploaded = glIsTexture(bm->bm_ogles_tex_id);
	long long debug_t_after_istex = particle_debug_now_us();

	if (!already_uploaded) {
		... (unchanged upload path) ...
	} else {
		glBindTexture(GL_TEXTURE_2D, bm->bm_ogles_tex_id);
	}

	// TEMPORARY: see the particle-debug block up top.
	if (particle_debug_log) {
		fprintf(particle_debug_log, "t=%lld hit=%d isTexUs=%lld totalUs=%lld w=%d h=%d rle=%d\n",
				debug_t_start, already_uploaded ? 1 : 0,
				debug_t_after_istex - debug_t_start, particle_debug_now_us() - debug_t_start,
				bm->bm_w, bm->bm_h, (bm->bm_flags & BM_FLAG_RLE) ? 1 : 0);
		fflush(particle_debug_log);
	}
```

### `Descent/src/main/cpp/render.c`

Extern declaration, right after the other `extern` lines near the top of the file:

```c
// TEMPORARY: particle-effect slowdown investigation -- see the matching block in
// texmap/ogles/oglestex.c for what this actually does and the full removal list.
extern void particle_debug_mark_frame();
```

Call site, last line of the "normal frame" `else` branch in `showRenderBuffer()`:

```c
		eglSwapBuffers(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_READ));
		can_save_screen = !can_save_screen;
		particle_debug_mark_frame();  // TEMPORARY, see block near the top of this file
	}
```

### `Descent/src/main/java/wootbeer/descent/DescentView.java`

Native method declaration, alongside the other `native` declarations near the bottom of
the file:

```java
	// TEMPORARY: particle-effect slowdown investigation. Opens
	// particle_debug.log in the app's external files dir (visible to a normal file
	// manager / over USB, unlike internal storage) and times every ogles texture bind
	// call from then on. Remove this declaration, its call site in surfaceCreated()
	// below, and the native side in texmap/ogles/oglestex.c once the investigation is
	// done.
	private static native void particleDebugInit(String logDir);
```

Call site, in `surfaceCreated()`'s background thread, right after `initEgl()` and before
the "Start Descent!" comment / `descentMain()` call:

```java
					// TEMPORARY: see particleDebugInit()'s declaration above -- remove this
					// block along with it once the particle-slowdown investigation is done.
					java.io.File debugLogDir = context.getExternalFilesDir(null);
					if (debugLogDir != null) {
						particleDebugInit(debugLogDir.getAbsolutePath());
					}
```

## If reviving this for a future investigation

- Consider batching the `fflush()` calls (e.g. only flush on `particle_debug_mark_frame()`,
  not on every single bind) -- the per-bind `fflush()` is a synchronous disk write and can
  itself cause the kind of stutter this was meant to diagnose, especially in particle-heavy
  scenes.
- `getExternalFilesDir(null)` still requires the user to actually go find and send the file
  -- if remote access to a tester's device isn't realistic, a smaller in-app "share log"
  button (reading `particle_debug.log` and firing a share `Intent`) would remove that step
  and might be worth adding at the same time.
