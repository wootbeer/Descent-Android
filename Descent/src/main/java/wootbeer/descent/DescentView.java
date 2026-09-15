package wootbeer.descent;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.AssetManager;
import android.graphics.PixelFormat;
import android.graphics.Point;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Handler;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.InputMethodManager;

import java.util.HashSet;
import java.util.Set;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLContext;
import javax.microedition.khronos.egl.EGLDisplay;
import javax.microedition.khronos.egl.EGLSurface;
import javax.microedition.khronos.opengles.GL10;

public class DescentView extends SurfaceView implements KeyEvent.Callback, SurfaceHolder.Callback,
		InputManager.InputDeviceListener {
	// Analog-stick deadzone (fraction of full travel) before a direction is treated as "held".
	private static final float STICK_DEADZONE = 0.5f;
	// Trigger deadzone (0..1 range) before a trigger is treated as "held".
	private static final float TRIGGER_DEADZONE = 0.3f;

	private boolean descentRunning, paused, surfaceWasDestroyed, textActive;
	// True when a resume has been requested (via onResume()/surfaceCreated(), see
	// resumeRenderThread() below) that the render thread hasn't caught up to yet -- i.e. it
	// arrived before pauseRenderThread() was even entered for the pause it corresponds to.
	// Without this, that resumeRenderThread() call's notifyAll() has no one waiting to hear it
	// (Java's wait/notify doesn't queue a notify for a future wait()), so it's simply lost; the
	// render thread then reaches pauseRenderThread() moments later, starts waiting for a resume
	// that already happened, and blocks forever -- effectively freezing the app until killed.
	// Spamming minimize/resume rapidly makes this race far more likely to land, since it
	// shrinks the window native code needs to notice Want_pause and actually call
	// pauseRenderThread() before the next resume arrives.
	private boolean resumePending;
	private Context context;
	private DescentView thiz;
	private Handler mainHandler;
	private InputManager inputManager;
	private InputMethodManager imm;
	private final Object renderThreadObj = new Object();
	private final Set<Integer> connectedGamepadIds = new HashSet<>();
	// How many times the physical display resolution the game is actually rendered at. The
	// SurfaceHolder's buffer is fixed to this larger size and the system compositor downscales
	// it to fit the screen, which acts as supersampling -- smoother edges, less texture shimmer.
	private final float renderScale;
	// True only for the one relaunch immediately following a settings-triggered auto-restart
	// (see DescentActivity.restartAppForSettingsChange()) -- passed straight through to
	// descentMain() as the "-quickresume" native arg, which skips the intro logos, auto-loads
	// the last-used pilot, and drops the player straight into the Options menu. False for every
	// normal launch.
	private final boolean quickResume;
	private Point size;
	private SurfaceHolder holder;

	// Digital (edge-triggered) state derived from analog stick/trigger axes, used so that
	// keyHandler() is only called on a press/release transition rather than every motion event.
	private boolean slideLeftDown, slideRightDown, slideUpDown, slideDownDown;
	private boolean turnLeftDown, turnRightDown, pitchUpDown, pitchDownDown;
	private boolean thrustForwardDown, thrustReverseDown;
	private boolean fireTriggerPrimaryDown, fireTriggerSecondaryDown;
	// D-pad reported as a hat-switch axis pair (some gamepads, including this device's,
	// send the D-pad this way instead of/alongside KEYCODE_DPAD_* key events) and the
	// always-on, layout-independent left-stick menu navigator -- both kept as separate
	// edge-detection state from the gameplay fields above so the two dispatch sites
	// never stomp on each other's "was this held last frame" tracking.
	private boolean hatLeftDown, hatRightDown, hatUpDown, hatDownDown;
	private boolean menuNavLeftDown, menuNavRightDown, menuNavUpDown, menuNavDownDown;
	private boolean menuNavRsLeftDown, menuNavRsRightDown, menuNavRsUpDown, menuNavRsDownDown;
	// Digital press/release synthesized from the LT/RT analog trigger axes crossing
	// TRIGGER_DEADZONE, so the Remap Gamepad screen's capture step (which only ever sees
	// discrete button events via gamepadButtonRaw()) can also see a trigger "press" --
	// see GP_TRIGGER_LT/RT in gamepad_remap.c for why this needs synthesizing at all.
	private boolean triggerLtRemapDown, triggerRtRemapDown;

	// Mirrors GP_TRIGGER_LT/GP_TRIGGER_RT in gamepad_remap.c -- keep these two in sync.
	private static final int GP_TRIGGER_LT = 1001;
	private static final int GP_TRIGGER_RT = 1002;

	public DescentView(Context context, float renderScale, boolean quickResume) {
		super(context);
		this.context = context;
		this.renderScale = renderScale;
		this.quickResume = quickResume;
		this.descentRunning = false;
		this.holder = getHolder();
		// Force the underlying Surface to an opaque pixel format. The EGL config picked in
		// initEgl() below asks for an alpha channel (EGL_ALPHA_SIZE), which -- unless the
		// SurfaceHolder itself is explicitly pinned opaque -- lets the platform treat this
		// SurfaceView as translucent-capable and hand it its own composited/hardware-overlay
		// plane. Every time that plane gets (re)negotiated -- a fresh SurfaceView after an
		// app restart in Force 4:3 mode, or the layout pass triggered by the on-screen
		// keyboard showing -- any tiny seam between the requested buffer size and the actual
		// on-screen region shows through as a thin stray-colored line, since there's nothing
		// opaque guaranteed behind it. Pinning this to OPAQUE up front removes that whole
		// class of artifact regardless of what the EGL side asks for.
		holder.setFormat(PixelFormat.OPAQUE);
		this.thiz = this;
		this.textActive = false;
		this.imm = (InputMethodManager) context.getSystemService(Context.INPUT_METHOD_SERVICE);
		this.mainHandler = new Handler(context.getMainLooper());
		this.setFocusableInTouchMode(true);
		if (Build.VERSION.SDK_INT >= 26) {
			// setFocusableInTouchMode(true) above (plus the requestFocus() calls in
			// activateText() below, and Android's own first-touch/first-key focus assignment
			// for a freshly created view) means this view can gain platform "focus" during
			// normal play -- not just while the on-screen keyboard is up. On API 26+, Android
			// draws a system default focus highlight (a themed glow/border) around any View
			// that has focus and doesn't supply its own highlight drawable, and this app's
			// theme (DescentTheme, see res/values/styles.xml) never defines one. That default
			// highlight is drawn by the platform outside of this view's own onDraw/GL content
			// (which is also why it wouldn't show up in a screenshot capture of just the app's
			// rendered frame), and matches every symptom reported for the thin colored border
			// seen around the keyboard/main-menu/intro: it needs an initial input/focus event
			// to appear, and it tracks the view's on-screen bounds exactly. Turning it off here
			// stops Android from drawing that highlight at all, while leaving actual keyboard
			// input handling (which only needs this view to be focusable, not highlighted)
			// completely unaffected.
			setDefaultFocusHighlightEnabled(false);
		}
		holder.addCallback(this);

		this.inputManager = (InputManager) context.getSystemService(Context.INPUT_SERVICE);
		if (inputManager != null) {
			inputManager.registerInputDeviceListener(this, mainHandler);
		}
		scanForGamepads();
	}

	// --- Gamepad connection tracking -------------------------------------------------------

	private static boolean isGamepad(InputDevice device) {
		if (device == null) {
			return false;
		}
		int sources = device.getSources();
		return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
	}

	private void scanForGamepads() {
		connectedGamepadIds.clear();
		for (int deviceId : InputDevice.getDeviceIds()) {
			if (isGamepad(InputDevice.getDevice(deviceId))) {
				connectedGamepadIds.add(deviceId);
			}
		}
		setGamepadConnected(!connectedGamepadIds.isEmpty());
	}

	@Override
	public void onInputDeviceAdded(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
			setGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceRemoved(int deviceId) {
		if (connectedGamepadIds.remove(deviceId)) {
			setGamepadConnected(!connectedGamepadIds.isEmpty());
		}
	}

	@Override
	public void onInputDeviceChanged(int deviceId) {
		if (isGamepad(InputDevice.getDevice(deviceId))) {
			connectedGamepadIds.add(deviceId);
		} else {
			connectedGamepadIds.remove(deviceId);
		}
		setGamepadConnected(!connectedGamepadIds.isEmpty());
	}

	// --- Gamepad analog input (sticks + triggers) -------------------------------------------

	@Override
	public boolean onGenericMotionEvent(MotionEvent event) {
		int source = event.getSource();
		boolean fromGamepad = (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
				|| (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD;
		if (fromGamepad && event.getAction() == MotionEvent.ACTION_MOVE) {
			float lx = event.getAxisValue(MotionEvent.AXIS_X);
			float ly = event.getAxisValue(MotionEvent.AXIS_Y);
			float rx = event.getAxisValue(MotionEvent.AXIS_Z);
			float ry = event.getAxisValue(MotionEvent.AXIS_RZ);
			float lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER);
			float rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER);
			if (lt == 0f && rt == 0f) {
				// Some gamepads report the triggers as brake/gas instead of ltrigger/rtrigger.
				lt = event.getAxisValue(MotionEvent.AXIS_BRAKE);
				rt = event.getAxisValue(MotionEvent.AXIS_GAS);
			}
			float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
			float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);

			// Let the Remap Gamepad screen (and any action a player binds to LT/RT there)
			// see the triggers as a button, same as every other input this method already
			// promotes to a discrete press/release -- independent of, and in addition to,
			// whatever Standard/Modern layout already does with lt/rt below (mirrors how
			// the 7 real buttons have never had any other fixed function to conflict with).
			triggerLtRemapDown = updateDigitalAxisRaw(triggerLtRemapDown, lt > TRIGGER_DEADZONE, GP_TRIGGER_LT);
			triggerRtRemapDown = updateDigitalAxisRaw(triggerRtRemapDown, rt > TRIGGER_DEADZONE, GP_TRIGGER_RT);

			// D-pad, as a hat-switch axis pair: some gamepads (this device included) send
			// D-pad presses this way instead of KEYCODE_DPAD_* key events, which is why
			// the KEYCODE_DPAD_* cases in handleGamepadKey() below alone weren't enough --
			// the key events for this device's D-pad may simply never arrive. Routed
			// straight to keyHandler() via the same dpadScancode() the KEYCODE_DPAD_*
			// case uses, so both paths agree on menu-nav-vs-Modern-gameplay-backup.
			hatLeftDown = updateDigitalAxis(hatLeftDown, hatX < -STICK_DEADZONE, dpadScancode(KeyEvent.KEYCODE_DPAD_LEFT));
			hatRightDown = updateDigitalAxis(hatRightDown, hatX > STICK_DEADZONE, dpadScancode(KeyEvent.KEYCODE_DPAD_RIGHT));
			hatUpDown = updateDigitalAxis(hatUpDown, hatY < -STICK_DEADZONE, dpadScancode(KeyEvent.KEYCODE_DPAD_UP));
			hatDownDown = updateDigitalAxis(hatDownDown, hatY > STICK_DEADZONE, dpadScancode(KeyEvent.KEYCODE_DPAD_DOWN));

			// Both sticks always navigate any menu -- the standalone main/options/pause
			// menus AND the in-game Options overlay opened mid-level (which never leaves
			// gameplay's Function_mode, so isInGame() alone can't see it -- see isMenuOpen()
			// in keyboard.c). This used to be left-stick-only and gated on !isInGame(),
			// which meant: (a) the right stick had no dedicated nav of its own -- it only
			// happened to move the menu cursor when Modern layout put turn/pitch on it, and
			// only by sharing scancodes with the gameplay dispatch below, which kept running
			// underneath the menu and fought with it (holding the "turn" stick past the
			// deadzone drove the menu cursor via a held key with no gameplay-side release,
			// which is what made it feel like it was "jumping"/selecting too fast; the other
			// stick's axis, still bound to slide/thrust scancodes newmenu doesn't treat as
			// navigation, did nothing at all); and (b) even the left stick's dedicated nav
			// went dead specifically in the in-game Options overlay, since isInGame() reads
			// true there. Both sticks now get their own exclusive, edge-triggered nav here --
			// the same treatment the D-pad already has, which is why the D-pad never had
			// this problem -- and the gameplay dispatch below is fully suppressed while a
			// menu is open, so neither stick can leak flight-control scancodes into it.
			// Always uses the un-inverted up/down mapping regardless of Invert Y: that's a
			// flight-camera preference and should never change which way a menu scrolls.
			boolean menuOpen = isMenuOpen();

			menuNavLeftDown = updateDigitalAxis(menuNavLeftDown, menuOpen && lx < -STICK_DEADZONE, (char) 0xCB);  // KEY_LEFT
			menuNavRightDown = updateDigitalAxis(menuNavRightDown, menuOpen && lx > STICK_DEADZONE, (char) 0xCD); // KEY_RIGHT
			menuNavUpDown = updateDigitalAxis(menuNavUpDown, menuOpen && ly < -STICK_DEADZONE, (char) 0xC8);      // KEY_UP
			menuNavDownDown = updateDigitalAxis(menuNavDownDown, menuOpen && ly > STICK_DEADZONE, (char) 0xD0);   // KEY_DOWN

			menuNavRsLeftDown = updateDigitalAxis(menuNavRsLeftDown, menuOpen && rx < -STICK_DEADZONE, (char) 0xCB);  // KEY_LEFT
			menuNavRsRightDown = updateDigitalAxis(menuNavRsRightDown, menuOpen && rx > STICK_DEADZONE, (char) 0xCD); // KEY_RIGHT
			menuNavRsUpDown = updateDigitalAxis(menuNavRsUpDown, menuOpen && ry < -STICK_DEADZONE, (char) 0xC8);      // KEY_UP
			menuNavRsDownDown = updateDigitalAxis(menuNavRsDownDown, menuOpen && ry > STICK_DEADZONE, (char) 0xD0);   // KEY_DOWN

			// Which physical inputs drive which logical functions is swappable from the
			// Remap Gamepad screen ("Stick Layout": Standard/Modern) -- native code owns
			// the persisted setting, this just reads it each motion event. The deadzones,
			// edge-detection state (turnLeftDown etc.) and target scancodes are the same
			// either way; only which raw axis/trigger feeds which logical function changes.
			boolean modern = isModernStickLayout();

			// Options menu "Invert Y" checkbox -- on (the default, matching existing
			// behavior unchanged) leaves the pitch stick's up/down comparisons as they've
			// always been; off flips just that axis's comparisons, on whichever physical
			// stick currently drives pitch under the active Stick Layout (left in Standard,
			// right in Modern). Nothing else -- turn, slide, thrust -- is affected.
			boolean invertY = isInvertYEnabled();

			// Every condition below is gated on !menuOpen (rather than skipping this whole
			// block while a menu is open) so that a key already held when the menu opened
			// -- e.g. the player was mid-turn when they paused -- gets a proper release call
			// instead of staying stuck down in native's key state until some later motion
			// event happens to change the raw axis again.
			if (!modern) {
				// Standard: left stick turns/pitches, right stick slides, triggers thrust.
				turnLeftDown = updateDigitalAxis(turnLeftDown, !menuOpen && lx < -STICK_DEADZONE, (char) 0xCB);  // KEY_LEFT
				turnRightDown = updateDigitalAxis(turnRightDown, !menuOpen && lx > STICK_DEADZONE, (char) 0xCD); // KEY_RIGHT
				if (invertY) {
					pitchUpDown = updateDigitalAxis(pitchUpDown, !menuOpen && ly < -STICK_DEADZONE, (char) 0xC8);    // KEY_UP
					pitchDownDown = updateDigitalAxis(pitchDownDown, !menuOpen && ly > STICK_DEADZONE, (char) 0xD0); // KEY_DOWN
				} else {
					pitchUpDown = updateDigitalAxis(pitchUpDown, !menuOpen && ly > STICK_DEADZONE, (char) 0xC8);     // KEY_UP
					pitchDownDown = updateDigitalAxis(pitchDownDown, !menuOpen && ly < -STICK_DEADZONE, (char) 0xD0); // KEY_DOWN
				}

				slideLeftDown = updateDigitalAxis(slideLeftDown, !menuOpen && rx < -STICK_DEADZONE, (char) 0x4F);  // KEY_PAD1
				slideRightDown = updateDigitalAxis(slideRightDown, !menuOpen && rx > STICK_DEADZONE, (char) 0x51); // KEY_PAD3
				slideUpDown = updateDigitalAxis(slideUpDown, !menuOpen && ry < -STICK_DEADZONE, (char) 0x4A);      // KEY_PADMINUS
				slideDownDown = updateDigitalAxis(slideDownDown, !menuOpen && ry > STICK_DEADZONE, (char) 0x4E);   // KEY_PADPLUS

				thrustForwardDown = updateDigitalAxis(thrustForwardDown, !menuOpen && rt > TRIGGER_DEADZONE, (char) 0x1E); // KEY_A
				thrustReverseDown = updateDigitalAxis(thrustReverseDown, !menuOpen && lt > TRIGGER_DEADZONE, (char) 0x2C); // KEY_Z
			} else {
				// Modern: left stick is dedicated fully to movement (strafe left/right on
				// its X axis, forward/reverse thrust on its Y axis -- up = forward, down =
				// reverse), right stick is dedicated fully to looking (turn/pitch). Triggers
				// fire instead of driving thrust here: RT = Fire Primary, LT = Fire
				// Secondary, the common "modern" twin-stick-plus-triggers scheme. These fire
				// the same fixed scancodes the "Fire Primary"/"Fire Secondary" rows in the
				// Remap Gamepad screen always trigger (see Gamepad_remap_actions in
				// gamepad_remap.c) -- independent of whatever physical BUTTON is currently
				// bound to those actions, so trigger-fire and button-fire both keep working
				// side by side no matter how the 7 buttons are remapped.
				//
				// Note: vertical strafe (slide up/down) has no stick or trigger driving it
				// in this layout -- both stick Y axes and both triggers are already spoken
				// for above.
				turnLeftDown = updateDigitalAxis(turnLeftDown, !menuOpen && rx < -STICK_DEADZONE, (char) 0xCB);  // KEY_LEFT
				turnRightDown = updateDigitalAxis(turnRightDown, !menuOpen && rx > STICK_DEADZONE, (char) 0xCD); // KEY_RIGHT
				if (invertY) {
					pitchUpDown = updateDigitalAxis(pitchUpDown, !menuOpen && ry < -STICK_DEADZONE, (char) 0xC8);    // KEY_UP
					pitchDownDown = updateDigitalAxis(pitchDownDown, !menuOpen && ry > STICK_DEADZONE, (char) 0xD0); // KEY_DOWN
				} else {
					pitchUpDown = updateDigitalAxis(pitchUpDown, !menuOpen && ry > STICK_DEADZONE, (char) 0xC8);     // KEY_UP
					pitchDownDown = updateDigitalAxis(pitchDownDown, !menuOpen && ry < -STICK_DEADZONE, (char) 0xD0); // KEY_DOWN
				}

				slideLeftDown = updateDigitalAxis(slideLeftDown, !menuOpen && lx < -STICK_DEADZONE, (char) 0x4F);  // KEY_PAD1
				slideRightDown = updateDigitalAxis(slideRightDown, !menuOpen && lx > STICK_DEADZONE, (char) 0x51); // KEY_PAD3

				thrustForwardDown = updateDigitalAxis(thrustForwardDown, !menuOpen && ly < -STICK_DEADZONE, (char) 0x1E); // KEY_A
				thrustReverseDown = updateDigitalAxis(thrustReverseDown, !menuOpen && ly > STICK_DEADZONE, (char) 0x2C);  // KEY_Z

				fireTriggerPrimaryDown = updateDigitalAxis(fireTriggerPrimaryDown, !menuOpen && rt > TRIGGER_DEADZONE, (char) 0x1D);   // KEY_LCTRL
				fireTriggerSecondaryDown = updateDigitalAxis(fireTriggerSecondaryDown, !menuOpen && lt > TRIGGER_DEADZONE, (char) 0x39); // KEY_SPACEBAR
			}

			return true;
		}
		return super.onGenericMotionEvent(event);
	}

	private boolean updateDigitalAxis(boolean wasDown, boolean isDown, char key) {
		if (isDown != wasDown) {
			keyHandler(key, isDown);
		}
		return isDown;
	}

	// Same edge-detection idiom as updateDigitalAxis() above, but forwarding through
	// gamepadButtonRaw() (an Android keyCode) instead of keyHandler() (a Descent
	// scancode) -- used for the two synthetic trigger "buttons" below, so the Remap
	// Gamepad screen's capture step can see a trigger pull the same way it already sees
	// a real button press.
	private boolean updateDigitalAxisRaw(boolean wasDown, boolean isDown, int keyCode) {
		if (isDown != wasDown) {
			gamepadButtonRaw(keyCode, isDown);
		}
		return isDown;
	}

	// D-pad's target scancode for one direction, shared by both places the D-pad reaches
	// this code (discrete KEYCODE_DPAD_* events in handleGamepadKey(), and the hat-axis
	// fallback in onGenericMotionEvent()): while a menu is open (or in Standard layout)
	// it's always the menu-nav/turn-pitch key; in actual gameplay under Modern layout it
	// switches to whatever the left stick does there instead, so D-pad keeps working as
	// a backup no matter which layout is active.
	private char dpadScancode(int keyCode) {
		// !isMenuOpen() (not just isInGame()) so the in-game Options overlay -- which
		// never leaves Function_mode==FMODE_GAME -- correctly falls back to menu-nav
		// scancodes for the D-pad too, instead of Modern's movement scancodes.
		boolean modernGameplay = isInGame() && !isMenuOpen() && isModernStickLayout();
		switch (keyCode) {
			case KeyEvent.KEYCODE_DPAD_UP:
				return (char) (modernGameplay ? 0x1E : 0xC8);   // KEY_A (thrust fwd) : KEY_UP
			case KeyEvent.KEYCODE_DPAD_DOWN:
				return (char) (modernGameplay ? 0x2C : 0xD0);   // KEY_Z (thrust rev) : KEY_DOWN
			case KeyEvent.KEYCODE_DPAD_LEFT:
				return (char) (modernGameplay ? 0x4F : 0xCB);   // KEY_PAD1 (slide L) : KEY_LEFT
			case KeyEvent.KEYCODE_DPAD_RIGHT:
			default:
				return (char) (modernGameplay ? 0x51 : 0xCD);   // KEY_PAD3 (slide R) : KEY_RIGHT
		}
	}

	// --- Gamepad buttons (routed through onKeyDown/onKeyUp below) --------------------------

	private boolean handleGamepadKey(int keyCode, boolean down, KeyEvent event) {
		int source = event.getSource();
		boolean fromGamepad = (source & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
				|| (source & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
		if (!fromGamepad) {
			return false;
		}
		char key;
		switch (keyCode) {
			case KeyEvent.KEYCODE_BUTTON_A:
			case KeyEvent.KEYCODE_BUTTON_B:
				// A and B are the universal menu confirm/cancel buttons -- matching what
				// tapping the screen does -- on ANY menu (main menu, Options, the in-game
				// pause overlay, the startup pilot-select screen, etc.), regardless of
				// what they're currently bound to in the Remap Gamepad menu. Only in
				// actual gameplay (no menu open) do they fall through to their remapped
				// action below. This also subsumes the old anti-minimize workaround (some
				// launchers treat an unclaimed "A" outside gameplay as a request to
				// minimize the app, since !isInGame() always implies isMenuOpen()) --
				// returning true here still keeps that swallowed, but now A actually does
				// something useful instead of being silently eaten.
				//
				// Exception: the Remap Gamepad screen has its own dedicated capture loop
				// that already reads physical A/B itself via gamepadButtonRaw() below
				// (isInRemapGamepadScreen()), so it's excluded here to avoid fighting with
				// that screen's own confirm/capture handling.
				if (isMenuOpen() && !isInRemapGamepadScreen()) {
					if (keyCode == KeyEvent.KEYCODE_BUTTON_A) {
						if (down) {
							gamepadMenuConfirm();
						}
					} else {
						keyHandler((char) 0x01, down); // KEY_ESC -- universal menu cancel
					}
					return true;
				}
				// fall through -- remappable, same as X/Y/L1/R1/THUMBL below.
			case KeyEvent.KEYCODE_BUTTON_X:
			case KeyEvent.KEYCODE_BUTTON_Y:
			case KeyEvent.KEYCODE_BUTTON_L1:
			case KeyEvent.KEYCODE_BUTTON_R1:
			case KeyEvent.KEYCODE_BUTTON_THUMBL:
				// These 7 buttons are user-remappable (see the "Remap Gamepad" Options
				// menu entry). Native code owns the actual keyCode<->action table and
				// the resulting keyHandler() dispatch -- see gamepadButtonRaw() below
				// and Descent/src/main/cpp/gamepad_remap.c.
				gamepadButtonRaw(keyCode, down);
				return true;
			case KeyEvent.KEYCODE_BUTTON_START:
				key = 0x01; // KEY_ESC - menu (fixed, not remappable)
				break;
			case KeyEvent.KEYCODE_BUTTON_SELECT:
				key = 0x0F; // KEY_TAB - map (fixed, not remappable)
				break;
			case KeyEvent.KEYCODE_DPAD_UP:
			case KeyEvent.KEYCODE_DPAD_DOWN:
			case KeyEvent.KEYCODE_DPAD_LEFT:
			case KeyEvent.KEYCODE_DPAD_RIGHT:
				// Whenever a menu is open, D-pad always means menu navigation
				// (KEY_UP/DOWN/LEFT/RIGHT) regardless of Stick Layout, so it can always
				// reach and drive every menu. In actual gameplay it backs up whatever the
				// left stick currently does under the active layout: turn/pitch in
				// Standard (unchanged, same keys either way), strafe/thrust in Modern.
				key = dpadScancode(keyCode);
				break;
			default:
				return false;
		}
		keyHandler(key, down);
		return true;
	}

	@SuppressLint("ClickableViewAccessibility")
	@Override
	public boolean onTouchEvent(final MotionEvent event) {
		int i, historySize, firstPointerIndex, numPointers;
		int action = event.getActionMasked();
		float prevX, prevY;
		boolean touchHandled = false;

		historySize = event.getHistorySize();
		if (action == MotionEvent.ACTION_POINTER_DOWN || action == MotionEvent.ACTION_POINTER_UP) {
			firstPointerIndex = event.getActionIndex();
		} else {
			firstPointerIndex = 0;
		}
		if (action == MotionEvent.ACTION_MOVE) {
			numPointers = event.getPointerCount();
		} else {
			numPointers = 1;
		}
		// Touch coordinates arrive in the View's on-screen pixel space, but the game itself
		// renders (and hit-tests its buttons) at renderScale times that resolution -- scale
		// coordinates up to match so touch/mouse input still lines up with what's drawn.
		for (i = firstPointerIndex; i < numPointers + firstPointerIndex; ++i) {
			if (historySize > 0) {
				prevX = event.getHistoricalX(i, 0);
				prevY = event.getHistoricalY(i, 0);
			} else {
				prevX = event.getX(i);
				prevY = event.getY(i);
			}
			touchHandled |= touchHandler(event.getActionMasked(), event.getPointerId(i),
					event.getX(i) * renderScale, event.getY(i) * renderScale,
					prevX * renderScale, prevY * renderScale);
		}
		if (!touchHandled && (action == MotionEvent.ACTION_DOWN ||
				action == MotionEvent.ACTION_UP)) {
			mouseHandler((short) (event.getX() * renderScale), (short) (event.getY() * renderScale),
					action == MotionEvent.ACTION_DOWN);
			return true;
		} else {
			mouseSetPos((short) (event.getX() * renderScale), (short) (event.getY() * renderScale));
		}
		return touchHandled;
	}

	@SuppressWarnings("unused")
	private boolean textIsActive() {
		return textActive;
	}

	@SuppressWarnings("unused")
	private void activateText() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				requestFocus();
				imm.showSoftInput(thiz, InputMethodManager.SHOW_FORCED);
				textActive = true;
			}
		});
	}

	@SuppressWarnings("unused")
	private void deactivateText() {
		mainHandler.post(new Runnable() {
			@Override
			public void run() {
				imm.hideSoftInputFromWindow(getWindowToken(), 0);
				clearFocus();
				textActive = false;
			}
		});
	}

	@Override
	public boolean onKeyUp(int keyCode, KeyEvent event) {
		if (handleGamepadKey(keyCode, false, event)) {
			return true;
		}
		if (keyCode == KeyEvent.KEYCODE_DEL) {
			keyHandler((char) 0x0E, false);
		} else if (keyCode == KeyEvent.KEYCODE_ENTER) {
			keyHandler((char) 0x1C, false);
		} else {
			keyHandler((char) event.getUnicodeChar(), false);
		}
		return event.getUnicodeChar() != 0;
	}

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (handleGamepadKey(keyCode, true, event)) {
			return true;
		}
		if (keyCode == KeyEvent.KEYCODE_DEL) {
			keyHandler((char) 0x0E, true);
		} else if (keyCode == KeyEvent.KEYCODE_ENTER) {
			keyHandler((char) 0x1C, true);
		} else {
			keyHandler((char) event.getUnicodeChar(), true);
		}
		return event.getUnicodeChar() != 0;
	}

	@Override
	public boolean onKeyPreIme(int keyCode, KeyEvent event) {
		if (keyCode == KeyEvent.KEYCODE_BACK && textActive) {
			keyHandler((char) 0x01, true);
			keyHandler((char) 0x01, false);
			return true;
		}
		return false;
	}

	@Override
	public void surfaceCreated(SurfaceHolder holder) {
		if (!descentRunning) {
			new Thread(new Runnable() {
				@Override
				public void run() {
					// Render at renderScale times this view's own on-screen size -- not
					// necessarily the full physical display. Normally the two are the same
					// (DescentActivity gives this view the whole screen), but in "Force 4:3"
					// mode DescentActivity instead lays this view out as a smaller, centered
					// 4:3 rectangle with black bars around it, and reading the view's actual
					// laid-out size here (rather than re-querying the raw display) is what
					// makes the render buffer automatically take on that same 4:3 shape, with
					// no aspect-ratio-mode logic needed in this class at all. By the time
					// surfaceCreated() fires, the view is guaranteed to have already been
					// through layout, so getWidth()/getHeight() are valid here.
					//
					// The SurfaceHolder's buffer is fixed to this size in initEgl(), and the
					// system compositor scales it to fill this view's on-screen bounds when
					// compositing.
					size = new Point(Math.round(getWidth() * renderScale),
							Math.round(getHeight() * renderScale));
					initEgl();

					// Start Descent!
					descentMain(size.x, size.y, context, thiz, context.getAssets(),
							context.getFilesDir().getAbsolutePath(),
							context.getCacheDir().getAbsolutePath(), quickResume);
				}
			}).start();
			descentRunning = true;
		} else {
			this.holder = holder;
			resumeRenderThread();
		}
	}

	@Override
	public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
		// This used to be a no-op. The render buffer is a FIXED size (see surfaceCreated()
		// above) and the system compositor scales it to fill this view's actual on-screen
		// bounds every time it composites a frame -- but this view's bounds aren't
		// necessarily fixed for the life of the app: immersive mode (setImmersive() in
		// DescentActivity) gets briefly interrupted by things like the on-screen keyboard
		// showing, or the system bars reappearing on the first touch/key after launch, and
		// Android calls this method exactly when that kind of geometry change happens. Doing
		// nothing here meant the buffer's pinned size could end up compositing against
		// slightly different view bounds than it was created for, and because
		// EGL_SWAP_BEHAVIOR is EGL_BUFFER_PRESERVED (menus intentionally redraw only the
		// parts that changed, not a full clear every frame -- see the "Redraw everything..."
		// loop in newmenu_do3()), a single bad frame from that mismatch never got painted
		// over again -- a thin stray-colored line stuck permanently at the buffer's edge.
		//
		// Re-pinning the buffer to its original render size (not the width/height Android
		// passes in here, which is the view's new on-screen size, not the resolution we
		// actually want to render at) forces the compositor to recompute a fresh scale
		// against the view's current bounds immediately, so a transient bounds change
		// doesn't leave a lasting seam. The GL viewport itself doesn't need to be touched
		// here -- it already covers this same fixed buffer size and that never changes --
		// and it couldn't safely be re-applied from here anyway: this callback runs on the
		// UI thread, not the render thread that actually owns the EGL context (see the
		// background Thread started in surfaceCreated() above), and GL calls are only valid
		// on the thread their context is current on.
		if (size != null && holder != null) {
			holder.setFixedSize(size.x, size.y);
		}
	}

	@Override
	public void surfaceDestroyed(SurfaceHolder holder) {
		surfaceWasDestroyed = true;
		surfaceWasDestroyed();
	}

	public boolean getSurfaceWasDestroyed() {
		return surfaceWasDestroyed;
	}

	public void resumeRenderThread() {
		synchronized (renderThreadObj) {
			if (paused) {
				paused = false;
				renderThreadObj.notifyAll();
			} else {
				// The render thread hasn't actually paused yet (still mid-frame, or native
				// code hasn't observed Want_pause and called pauseRenderThread() yet) -- there's
				// no one to notify. Remember this resume so the upcoming pauseRenderThread()
				// call recognizes it's already stale and returns immediately instead of
				// blocking on a notify that already happened. See resumePending's declaration.
				resumePending = true;
			}
			surfaceWasDestroyed = false;
		}
	}

	@SuppressWarnings("unused")
	private void pauseRenderThread() {
		synchronized (renderThreadObj) {
			if (resumePending) {
				// A resume for this pause already arrived before we got here -- see
				// resumePending's declaration. Consume it and skip pausing entirely.
				resumePending = false;
				return;
			}
			paused = true;
			while (paused) {
				try {
					renderThreadObj.wait();
				} catch (InterruptedException e) {
					Thread.currentThread().interrupt();
					break;
				}
			}
		}
	}

	/**
	 * Initializes EGL for the current thread
	 */
	private void initEgl() {
		int EGL_CONTEXT_CLIENT_VERSION = 0x3098;
		int[] num_config = new int[1];
		final EGLConfig[] configs = new EGLConfig[1];
		int[] attrib_list = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL10.EGL_NONE};
		EGL10 egl;
		EGLConfig eglConfig;
		EGLContext eglContext;
		EGLDisplay eglDisplay;
		EGLSurface eglSurface;
		GL10 gl;

		// NOTE: this method does NOT need to destroy a previous context/surface of its own --
		// render.c's showRenderBuffer() already does that (eglDestroySurface/eglDestroyContext/
		// eglTerminate) on the native side before it calls back into this method via JNI on
		// every pause/resume cycle. An earlier version of this method duplicated that teardown
		// here, which double-destroyed the same EGL objects -- harmless-looking under a single
		// slow pause/resume, but undefined behavior at the driver level, and the likely cause
		// of a crash reproduced by rapidly spamming minimize/resume. Do not reintroduce it.

		// Fix the underlying Surface's buffer to the (possibly supersampled) render
		// resolution rather than the View's on-screen layout size. Re-applied every time
		// initEgl() runs (including the native-triggered re-create after a pause/resume),
		// since a fresh Surface starts back at its default (unscaled) size.
		//
		// This has to actually happen on the main/UI thread, not this one -- this method
		// always runs on the background render thread started in surfaceCreated() (including
		// when native code re-invokes it via JNI after a pause/resume, see showRenderBuffer()
		// in render.c), but SurfaceView.setFixedSize() calls requestLayout() whenever the
		// requested size actually changes, and once this view's window is already fully
		// attached, that propagates straight up to ViewRootImpl.requestLayout(), which throws
		// CalledFromWrongThreadException off the main thread. A normal cold launch calls
		// setContentView() well before the window is fully attached, so there was enough of a
		// head start before this view's first surfaceCreated() that the race went unnoticed --
		// but launching straight into the game via the "pick your data folder" first-run flow
		// (DescentActivity.launchGame(), invoked from a background copy thread's
		// runOnUiThread()) replaces an *already* fully-attached window's content view, so this
		// call landed while the view was already attached and crashed reliably: black screen,
		// then the whole process getting killed. Posting the call to the main thread and
		// blocking this thread until it's actually been applied keeps this safe on every launch
		// path while still guaranteeing the fixed size is in place before eglCreateWindowSurface()
		// below reads it off this same holder.
		if (size != null) {
			final Point fixedSize = size;
			final Object fixedSizeDone = new Object();
			final boolean[] applied = {false};
			synchronized (fixedSizeDone) {
				mainHandler.post(new Runnable() {
					@Override
					public void run() {
						holder.setFixedSize(fixedSize.x, fixedSize.y);
						synchronized (fixedSizeDone) {
							applied[0] = true;
							fixedSizeDone.notifyAll();
						}
					}
				});
				while (!applied[0]) {
					try {
						fixedSizeDone.wait();
					} catch (InterruptedException e) {
						Thread.currentThread().interrupt();
						break;
					}
				}
			}
		}

		egl = (EGL10) EGLContext.getEGL();
		eglDisplay = egl.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY);
		egl.eglInitialize(eglDisplay, new int[]{1, 0});
		egl.eglChooseConfig(eglDisplay, new int[]{
				EGL10.EGL_RED_SIZE, 8,
				EGL10.EGL_GREEN_SIZE, 8,
				EGL10.EGL_BLUE_SIZE, 8,
				// No alpha channel -- this surface is always fully opaque (the game clears
				// to opaque black below and every menu/HUD element is drawn as part of that
				// same opaque scene), and requesting one here was what let the Surface get
				// treated as translucent-capable in the first place (see the setFormat(OPAQUE)
				// comment in the constructor above) -- the root cause of the thin colored
				// line that could appear at this view's edge after a surface/layout change.
				EGL10.EGL_ALPHA_SIZE, 0,
				EGL10.EGL_DEPTH_SIZE, 16,
				EGL10.EGL_STENCIL_SIZE, 0,
				EGL10.EGL_NONE}, configs, 1, num_config);
		eglConfig = configs[0];
		eglContext = egl.eglCreateContext(eglDisplay, eglConfig, EGL10.EGL_NO_CONTEXT, attrib_list);
		eglSurface = egl.eglCreateWindowSurface(eglDisplay, eglConfig, holder, null);
		egl.eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
		gl = (GL10) eglContext.getGL();

		// Clear to back
		gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		gl.glClear(GL10.GL_COLOR_BUFFER_BIT | GL10.GL_DEPTH_BUFFER_BIT);

		// Enable culling
		gl.glEnable(GL10.GL_CULL_FACE);
		gl.glCullFace(GL10.GL_BACK);

		// Viewport is screen bounds
		gl.glViewport(0, 0, size.x, size.y);

		// Enable blending for alphas
		gl.glEnable(GL10.GL_BLEND);
		gl.glBlendFunc(GL10.GL_SRC_ALPHA, GL10.GL_ONE_MINUS_SRC_ALPHA);
	}

	private static native void keyHandler(char key, boolean down);

	private static native void mouseHandler(short x, short y, boolean down);

	private static native void mouseSetPos(short x, short y);

	private static native boolean touchHandler(int action, int pointerId, float x, float y,
											   float prevX, float prevY);

	private static native void surfaceWasDestroyed();

	private static native boolean isInGame();

	private static native boolean isInRemapGamepadScreen();

	private static native boolean isModernStickLayout();

	private static native boolean isInvertYEnabled();

	private static native boolean isMenuOpen();

	private static native void gamepadMenuConfirm();

	private static native void setGamepadConnected(boolean connected);

	private static native void gamepadButtonRaw(int keyCode, boolean down);

	private static native void descentMain(int w, int h, Context activity,
										   DescentView descentView,
										   AssetManager assetManager, String documentPath,
										   String cachePath, boolean quickResume);
}
