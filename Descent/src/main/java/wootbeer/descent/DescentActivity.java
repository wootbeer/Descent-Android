package wootbeer.descent;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Point;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.MediaPlayer;
import android.net.Uri;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.provider.DocumentsContract;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.Gravity;
import android.view.Surface;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Created by devin on 4/17/16.
 */
public class DescentActivity extends Activity implements SensorEventListener {
	// Descent's own game data files -- Parallax's copyrighted DESCENT.HOG/DESCENT.PIG are no
	// longer bundled in the APK. Instead the user picks the folder containing their own copy
	// via Storage Access Framework, and we copy the two files into our private app storage.
	private static final String DATA_FILENAME_HOG = "DESCENT.HOG";
	private static final String DATA_FILENAME_PIG = "DESCENT.PIG";
	private static final int REQUEST_CODE_OPEN_DATA_FOLDER = 4242;

	// How many times the physical display resolution to render at -- set from the game's own
	// title-screen "Detail Level Customization" menu (see setRenderScaleIndex() below).
	private static final String PREFS_NAME = "descent_settings";
	private static final String PREF_RENDER_SCALE = "render_scale_index";
	private static final float[] RENDER_SCALE_OPTIONS = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f};

	// "Force 4:3" -- set from the game's own title-screen "Detail Level Customization" menu
	// (see setAspectRatio43() below). Like render scale, this reshapes the render surface
	// itself, which can only safely happen once at app startup.
	private static final String PREF_ASPECT_RATIO_43 = "aspect_ratio_43";

	// Set right before this app kills and relaunches itself (see restartAppForSettingsChange()
	// below) so the fresh launch knows to resume seamlessly instead of showing the intro logos
	// and pilot picker again -- see launchGame() below and the "-quickresume" native arg it
	// passes into descentMain().
	private static final String PREF_QUICK_RESUME = "pending_quickresume";

	private DescentView descentView;
	private MediaPlayer mediaPlayer;
	private Sensor gyroscopeSensor;
	private SensorManager sensorManager;
	private File hogFile, pigFile;
	private float buttonSizeBias;
	private float[] acceleration;
	private int mediaPlayerPosition;
	private int refreshPeriodUs;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		DisplayMetrics metrics;
		Resources resources;

		super.onCreate(savedInstanceState);

		// Enable immersive mode and make sure it's enabled whenever we're fullscreen
		setImmersive();
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setOnSystemUiVisibilityChangeListener(
					new View.OnSystemUiVisibilityChangeListener() {
						@Override
						public void onSystemUiVisibilityChange(int visibility) {
							if ((visibility & View.SYSTEM_UI_FLAG_FULLSCREEN) == 0) {
								setImmersive();
							}
						}
					});
		}

		// Calculate button size bias; want slightly bigger touch controls on genuinely bigger
		// (tablet-class) screens, without inflating them on an ordinary phone.
		//
		// This used to be a single divide-by-5.5-then-clamp-to-1.4 formula, tuned against
		// phones from around when this port was written (~2016, typically 4.5-5.5" diagonal).
		// Phones have gotten bigger since -- a normal 6-7" phone today has a landscape
		// width+height-in-inches sum well past that formula's old threshold, so it was hitting
		// its max 1.4x bonus on nearly every current phone, not just tablets. Layered on top of
		// today's much higher typical screen density, on-screen touch controls sized in dp (see
		// dpToPx()/pxToDp() below, and their only other caller, controls.c's init_buttons())
		// were coming out 3-3.5x their nominal dp size instead of the ~2-2.5x this was likely
		// designed around -- ballooning into a checkerboard that swallowed most of the screen
		// on modern phone hardware. See the "GUI for on-screen touch controls scaled way too
		// large" bug report.
		//
		// Below PHONE_SIZE_IN (typical big-phone landscape sum), no bonus at all -- bias stays
		// 1.0, so dpToPx()/pxToDp() are plain, unmodified density conversions and on-screen
		// button dp sizes mean exactly what they say. From there it ramps linearly up to
		// MAX_BIAS by TABLET_SIZE_IN (roughly an 8-10" tablet) and clamps at that beyond.
		final float PHONE_SIZE_IN = 9.5f;
		final float TABLET_SIZE_IN = 14.0f;
		final float MAX_BIAS = 1.15f;
		resources = getResources();
		metrics = resources.getDisplayMetrics();
		float sumInches = metrics.widthPixels / metrics.xdpi + metrics.heightPixels / metrics.ydpi;
		float t = (sumInches - PHONE_SIZE_IN) / (TABLET_SIZE_IN - PHONE_SIZE_IN);
		buttonSizeBias = (float) Math.min(Math.max(1.0f + t * (MAX_BIAS - 1.0f), 1.0f), MAX_BIAS);

		// Set up gyroscope
		sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);
		if (sensorManager != null) {
			gyroscopeSensor = sensorManager.getDefaultSensor(Sensor.TYPE_GYROSCOPE);
		}

		final Display display = ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
		final float refreshRate = display.getRefreshRate();
		refreshPeriodUs = (int) (1.0 / refreshRate * 1000000);

		// Create media player for MIDI
		mediaPlayer = new MediaPlayer();

		hogFile = new File(getFilesDir(), DATA_FILENAME_HOG);
		pigFile = new File(getFilesDir(), DATA_FILENAME_PIG);

		if (hogFile.exists() && pigFile.exists()) {
			launchGame();
		} else {
			showDataPicker(null);
		}
	}

	/**
	 * Creates the OpenGL ES view and starts the native game engine. Only safe to call once
	 * DESCENT.HOG and DESCENT.PIG are present in {@link #getFilesDir()}.
	 */
	private void launchGame() {
		// True only for the single launch immediately following a settings-triggered restart
		// (see restartAppForSettingsChange() below) -- clear it immediately so it can't
		// accidentally stick around and affect a later, normal launch.
		boolean quickResume = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getBoolean(PREF_QUICK_RESUME, false);
		if (quickResume) {
			getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().remove(PREF_QUICK_RESUME).apply();
		}

		descentView = new DescentView(this, RENDER_SCALE_OPTIONS[getRenderScaleIndex()], quickResume);

		if (getAspectRatio43()) {
			// Give the SurfaceView a genuine 4:3 shape instead of letting it fill the whole
			// screen, and center it over a black background -- the rest of the screen (the
			// bars on the sides on a wider device) is just this container's background
			// showing through. DescentView itself doesn't need to know about any of this: it
			// sizes its render buffer from its own actual on-screen size (see
			// DescentView.surfaceCreated()), so it automatically renders correctly into
			// whatever shape it's given here, exactly like it already does for a full-screen
			// Normal-mode view.
			WindowManager wm = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
			Display display = wm.getDefaultDisplay();
			Point displaySize = new Point();
			if (Build.VERSION.SDK_INT >= 19) {
				display.getRealSize(displaySize);
			} else {
				display.getSize(displaySize);
			}

			// Largest true 4:3 rectangle that fits within the physical screen -- keep the
			// full height and narrow the width on a device wider than 4:3 (bars on the
			// sides, e.g. this device), or the reverse if a device is ever narrower than 4:3.
			int fitWidth = displaySize.y * 4 / 3;
			int fitHeight = displaySize.x * 3 / 4;
			int viewWidth, viewHeight;
			if (fitWidth <= displaySize.x) {
				viewWidth = fitWidth;
				viewHeight = displaySize.y;
			} else {
				viewWidth = displaySize.x;
				viewHeight = fitHeight;
			}

			FrameLayout container = new FrameLayout(this);
			container.setBackgroundColor(Color.BLACK);
			container.addView(descentView, new FrameLayout.LayoutParams(viewWidth, viewHeight, Gravity.CENTER));
			setContentView(container);
		} else {
			setContentView(descentView);
		}

		// Keep the screen from going to sleep
		getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
	}

	// --- Render scale (supersampling for a sharper image) -----------------------------------
	// Set from the game's own title-screen "Detail Level Customization" menu (see
	// do_detail_level_menu_custom() in main/menu.c) via the setRenderScaleIndex() JNI call
	// below. Not offered from the in-game pause menu, since this engine only sizes its render
	// buffer once at startup -- a new value here only takes effect on the next app launch.

	private int getRenderScaleIndex() {
		int index = getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getInt(PREF_RENDER_SCALE, 0);
		return (index >= 0 && index < RENDER_SCALE_OPTIONS.length) ? index : 0;
	}

	@SuppressWarnings("unused")
	private void setRenderScaleIndex(int index) {
		if (index < 0 || index >= RENDER_SCALE_OPTIONS.length) {
			return;
		}
		// commit(), not apply() -- do_detail_level_menu_custom() (main/menu.c) can trigger
		// restartAppForSettingsChange() moments after this returns, which kills this process.
		// apply()'s write to disk happens asynchronously on a background thread; if that
		// hadn't finished flushing yet when the process died, the change would be silently
		// lost and the next launch would come back up with the old value -- exactly what was
		// happening before this was commit().
		getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().putInt(PREF_RENDER_SCALE, index).commit();
	}

	// --- Force 4:3 (pillarboxed aspect ratio) ------------------------------------------------
	// Set from the same title-screen "Detail Level Customization" menu (see
	// do_detail_level_menu_custom() in main/menu.c) via the setAspectRatio43() JNI call below.
	// Not offered from the in-game pause menu, since this engine only sizes/shapes its render
	// surface once at startup -- a new value here only takes effect on the next app launch.

	private boolean getAspectRatio43() {
		return getSharedPreferences(PREFS_NAME, MODE_PRIVATE).getBoolean(PREF_ASPECT_RATIO_43, false);
	}

	@SuppressWarnings("unused")
	private void setAspectRatio43(boolean enabled) {
		// commit(), not apply() -- see the comment in setRenderScaleIndex() above; the same
		// restart-can-follow-almost-immediately race applies here.
		getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().putBoolean(PREF_ASPECT_RATIO_43, enabled).commit();
	}

	// --- Settings-triggered close ------------------------------------------------------------
	// Render Scale and Force 4:3 above both reshape the render surface, which this old engine
	// only ever sets up once at process startup -- there's no way to apply either one without a
	// fresh process. Auto-relaunching to the foreground automatically was tried a few different
	// ways (a direct startActivity()+kill, then an AlarmManager-scheduled one) and none landed
	// reliably: this Activity is android:launchMode="singleTask", so a same-process relaunch
	// attempt just raced our own still-running instance, and even once that race was removed,
	// Android's background-activity-start restrictions kept the relaunch from reliably coming to
	// the foreground -- it landed minimized in Recents instead. Chasing full reliability there
	// means fighting a deliberate, version-shifting OS security boundary, not fixing a bug.
	//
	// So instead: the native side (see the restartAppForSettingsChange() JNI shim in motion.c)
	// shows the player a quick in-game message explaining what's about to happen, then calls
	// this, which marks PREF_QUICK_RESUME (so the player's next manual launch still skips the
	// intro logos and pilot picker and lands back on the main menu -- still quick, just not
	// automatic) and closes the app outright.

	@SuppressWarnings("unused")
	private void restartAppForSettingsChange() {
		// commit(), not apply() -- the process is about to die, so this write needs to actually
		// be on disk before that happens rather than queued on a background thread that dies
		// with it.
		getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit().putBoolean(PREF_QUICK_RESUME, true).commit();

		// This app has no graceful Android shutdown path anywhere else -- normal quit already
		// goes straight from native code to exit(0) with no Activity lifecycle involved (see
		// main/inferno.c). Killing the process directly here is consistent with that.
		Process.killProcess(Process.myPid());
	}

	// --- User-supplied game data (Storage Access Framework) --------------------------------

	private void showDataPicker(String errorMessage) {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);
		int pad = (int) dpToPx(24);
		layout.setPadding(pad, pad, pad, pad);

		TextView title = new TextView(this);
		title.setText("Descent game data needed");
		title.setTextColor(Color.WHITE);
		title.setTextSize(22);
		title.setGravity(Gravity.CENTER);
		layout.addView(title);

		TextView message = new TextView(this);
		message.setText("Select the folder that contains your own copy of DESCENT.HOG and DESCENT.PIG.");
		message.setTextColor(Color.LTGRAY);
		message.setGravity(Gravity.CENTER);
		message.setPadding(0, (int) dpToPx(16), 0, (int) dpToPx(16));
		layout.addView(message);

		if (errorMessage != null) {
			TextView error = new TextView(this);
			error.setText(errorMessage);
			error.setTextColor(Color.rgb(255, 120, 120));
			error.setGravity(Gravity.CENTER);
			error.setPadding(0, 0, 0, (int) dpToPx(16));
			layout.addView(error);
		}

		Button chooseButton = new Button(this);
		chooseButton.setText("Choose Folder");
		chooseButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				openFolderPicker();
			}
		});
		layout.addView(chooseButton);

		setContentView(layout);
	}

	private void showCopyingProgress() {
		LinearLayout layout = new LinearLayout(this);
		layout.setOrientation(LinearLayout.VERTICAL);
		layout.setGravity(Gravity.CENTER);
		layout.setBackgroundColor(Color.BLACK);

		ProgressBar progressBar = new ProgressBar(this);
		layout.addView(progressBar);

		TextView text = new TextView(this);
		text.setText("Copying game data...");
		text.setTextColor(Color.WHITE);
		text.setGravity(Gravity.CENTER);
		text.setPadding(0, (int) dpToPx(16), 0, 0);
		layout.addView(text);

		setContentView(layout);
	}

	private void openFolderPicker() {
		if (Build.VERSION.SDK_INT < 21) {
			showDataPicker("This Android version can't select external files. Please update the app's " +
					"bundled assets instead.");
			return;
		}
		Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
		startActivityForResult(intent, REQUEST_CODE_OPEN_DATA_FOLDER);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);
		if (requestCode == REQUEST_CODE_OPEN_DATA_FOLDER) {
			if (resultCode != RESULT_OK || data == null || data.getData() == null) {
				// User cancelled -- leave the picker screen showing so they can try again.
				return;
			}
			copyDataFilesFromTree(data.getData());
		}
	}

	private void copyDataFilesFromTree(final Uri treeUri) {
		showCopyingProgress();
		new Thread(new Runnable() {
			@Override
			public void run() {
				final String error = doCopyDataFiles(treeUri);
				runOnUiThread(new Runnable() {
					@Override
					public void run() {
						if (error != null) {
							showDataPicker(error);
						} else {
							launchGame();
						}
					}
				});
			}
		}).start();
	}

	/**
	 * Runs on a background thread. Returns null on success, or a user-facing error message.
	 */
	private String doCopyDataFiles(Uri treeUri) {
		File hogTemp = new File(getFilesDir(), DATA_FILENAME_HOG + ".tmp");
		File pigTemp = new File(getFilesDir(), DATA_FILENAME_PIG + ".tmp");
		try {
			Uri hogUri = findChildDocument(treeUri, DATA_FILENAME_HOG);
			Uri pigUri = findChildDocument(treeUri, DATA_FILENAME_PIG);
			if (hogUri == null || pigUri == null) {
				StringBuilder missing = new StringBuilder("Couldn't find ");
				if (hogUri == null) missing.append(DATA_FILENAME_HOG);
				if (hogUri == null && pigUri == null) missing.append(" or ");
				if (pigUri == null) missing.append(DATA_FILENAME_PIG);
				missing.append(" in that folder. Please pick a folder that contains both files.");
				return missing.toString();
			}
			copyUriToFile(hogUri, hogTemp);
			copyUriToFile(pigUri, pigTemp);
			if (!hogTemp.renameTo(hogFile) || !pigTemp.renameTo(pigFile)) {
				return "Couldn't finish copying the data files. Please try again.";
			}
			return null;
		} catch (IOException e) {
			//noinspection ResultOfMethodCallIgnored
			hogTemp.delete();
			//noinspection ResultOfMethodCallIgnored
			pigTemp.delete();
			return "Couldn't copy the data files: " + e.getMessage();
		}
	}

	private Uri findChildDocument(Uri treeUri, String displayName) {
		String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
		Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeDocId);
		Cursor cursor = getContentResolver().query(childrenUri, new String[]{
				DocumentsContract.Document.COLUMN_DOCUMENT_ID,
				DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null);
		if (cursor == null) {
			return null;
		}
		try {
			while (cursor.moveToNext()) {
				String name = cursor.getString(1);
				if (displayName.equalsIgnoreCase(name)) {
					String docId = cursor.getString(0);
					return DocumentsContract.buildDocumentUriUsingTree(treeUri, docId);
				}
			}
		} finally {
			cursor.close();
		}
		return null;
	}

	private void copyUriToFile(Uri uri, File destination) throws IOException {
		InputStream in = null;
		OutputStream out = null;
		try {
			in = getContentResolver().openInputStream(uri);
			if (in == null) {
				throw new IOException("could not open " + uri);
			}
			out = new FileOutputStream(destination);
			byte[] buffer = new byte[64 * 1024];
			int read;
			while ((read = in.read(buffer)) != -1) {
				out.write(buffer, 0, read);
			}
			out.flush();
		} finally {
			if (in != null) {
				try {
					in.close();
				} catch (IOException ignored) {
				}
			}
			if (out != null) {
				try {
					out.close();
				} catch (IOException ignored) {
				}
			}
		}
	}

	@Override
	protected void onPause() {
		super.onPause();
		// descentView (and the native engine) only exist once the game data files are in
		// place -- onPause can fire earlier than that, e.g. while the SAF folder picker or
		// the data copy is in progress.
		if (descentView != null) {
			descentPause();
			mediaPlayer.pause();
			mediaPlayerPosition = mediaPlayer.getCurrentPosition();
			stopMotion();
		}
	}

	@Override
	protected void onResume() {
		super.onResume();
		setImmersive();
		if (descentView != null) {
			if (!descentView.getSurfaceWasDestroyed()) {
				descentView.resumeRenderThread();
			}
			mediaPlayer.seekTo(mediaPlayerPosition);
			mediaPlayer.start();
			if (getUseGyroscope()) {
				startMotion();
			}
		}
	}

	@Override
	public void onSensorChanged(SensorEvent event) {
		acceleration = event.values;
	}

	@Override
	public void onAccuracyChanged(Sensor sensor, int accuracy) {

	}

	@SuppressWarnings("unused")
	private float[] getRotationRate() {
		if (haveGyroscope()) {
			if (getWindowManager().getDefaultDisplay().getRotation() == Surface.ROTATION_90) {
				acceleration[0] *= -1;
				acceleration[1] *= -1;
			}
			return acceleration;
		} else {
			return new float[] {0, 0, 0};
		}
	}

	private boolean haveGyroscope() {
		return gyroscopeSensor != null;
	}

	private void startMotion() {
		sensorManager.registerListener(this, gyroscopeSensor, refreshPeriodUs);
	}

	private void stopMotion() {
		sensorManager.unregisterListener(this);
	}

	@SuppressWarnings("unused")
	private void playMidi(String path, boolean looping) {
		File file = new File(path);
		FileDescriptor fd;
		FileInputStream fos;

		try {
			fos = new FileInputStream(file);
			fd = fos.getFD();
			mediaPlayer.setDataSource(fd);
			mediaPlayer.prepare();
		} catch (IOException e) {
			e.printStackTrace();
		}
		mediaPlayer.setLooping(looping);
		mediaPlayer.start();
	}

	@SuppressWarnings("unused")
	private void stopMidi() {
		mediaPlayer.stop();
		mediaPlayer.reset();
	}

	@SuppressWarnings("unused")
	private void setMidiVolume(float volume) {
		mediaPlayer.setVolume(volume, volume);
	}

	@SuppressWarnings("unused")
	private float dpToPx(float dp) {
		Resources resources = getResources();
		DisplayMetrics metrics = resources.getDisplayMetrics();
		return dp * (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	@SuppressWarnings("unused")
	private float pxToDp(float px) {
		Resources resources = getResources();
		DisplayMetrics metrics = resources.getDisplayMetrics();
		return px / (((float) metrics.densityDpi / DisplayMetrics.DENSITY_DEFAULT) * buttonSizeBias);
	}

	/**
	 * Enables immersive mode, hiding navigation controls
	 */
	private void setImmersive() {
		if (Build.VERSION.SDK_INT >= 19) {
			getWindow().getDecorView().setSystemUiVisibility(
					View.SYSTEM_UI_FLAG_LAYOUT_STABLE
							| View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
							| View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
							| View.SYSTEM_UI_FLAG_FULLSCREEN
							| View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
		}
	}

	private static native void descentPause();

	private static native boolean getUseGyroscope();

	static {
		System.loadLibrary("descent");
	}
}
