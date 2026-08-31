//
// Created by devin on 4/17/16.
//

#include <fix.h>
#include <jni.h>
#include <kconfig.h>

extern JavaVM *jvm;
extern jobject Activity;

void startMotion() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "startMotion", "()V");
	(*env)->CallVoidMethod(env, Activity, method);
	(*env)->DeleteLocalRef(env, clazz);
}

void stopMotion() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "stopMotion", "()V");
	(*env)->CallVoidMethod(env, Activity, method);
	(*env)->DeleteLocalRef(env, clazz);
}

void getRotationRate(double *x, double *y, double *z) {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;
	jfloatArray acceleration;
	jfloat *accelerationElements;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "getRotationRate", "()[F");
	acceleration = (*env)->CallObjectMethod(env, Activity, method);
	accelerationElements = (*env)->GetFloatArrayElements(env, acceleration, 0);
	*x = accelerationElements[0];
	*y = accelerationElements[1];
	*z = accelerationElements[2];
	(*env)->ReleaseFloatArrayElements(env, acceleration, accelerationElements, 0);
	(*env)->DeleteLocalRef(env, clazz);
	(*env)->DeleteLocalRef(env, acceleration);
}

int haveGyroscope() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;
	jboolean gyroscope;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "haveGyroscope", "()Z");
	gyroscope = (*env)->CallBooleanMethod(env, Activity, method);
	(*env)->DeleteLocalRef(env, clazz);
	return gyroscope;
}

// Called when the title-screen "Render Scale" slider changes, so the new value is
// remembered (in Android's SharedPreferences) for sizing the render buffer on the next
// app launch -- the buffer itself can't safely be resized mid-session.
void setRenderScaleIndex(int index) {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "setRenderScaleIndex", "(I)V");
	(*env)->CallVoidMethod(env, Activity, method, (jint) index);
	(*env)->DeleteLocalRef(env, clazz);
}

// Called when the title-screen "Force 4:3" checkbox changes, so the new value is
// remembered (in Android's SharedPreferences) for shaping the render surface on the next
// app launch -- like render scale, the surface's shape can't safely change mid-session.
void setAspectRatio43(int enabled) {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "setAspectRatio43", "(Z)V");
	(*env)->CallVoidMethod(env, Activity, method, (jboolean) (enabled != 0));
	(*env)->DeleteLocalRef(env, clazz);
}

JNIEXPORT jboolean JNICALL Java_wootbeer_descent_DescentActivity_getUseGyroscope(JNIEnv *env, jclass type) {
	return Config_use_gyroscope;
}

// Called once, right after the title-screen Video Options submenu is exited, if Render Scale
// or Force 4:3 actually changed during that visit (see Video_settings_dirty in main/menu.c),
// and after the caller has already shown the player an in-game message explaining what's about
// to happen. Closes the app outright rather than trying to relaunch itself automatically --
// see the comment on this same method in DescentActivity.java for why an automatic relaunch
// isn't reliable here. It still marks the "-quickresume" flag for the player's next manual
// launch (skip the intro logos, auto-load the last pilot, land back on the main menu), so
// reopening it still feels quick even though it isn't fully automatic.
void restartAppForSettingsChange() {
	JNIEnv *env;
	jclass clazz;
	jmethodID method;

	(*jvm)->GetEnv(jvm, (void **) &env, JNI_VERSION_1_6);
	clazz = (*env)->FindClass(env, "wootbeer/descent/DescentActivity");
	method = (*env)->GetMethodID(env, clazz, "restartAppForSettingsChange", "()V");
	(*env)->CallVoidMethod(env, Activity, method);
	(*env)->DeleteLocalRef(env, clazz);
}
