#include <jni.h>

#include <memory>
#include <string>

#include "engine.h"

using slowik::Engine;

static Engine* fromHandle(jlong h) { return reinterpret_cast<Engine*>(h); }

extern "C" {

JNIEXPORT jlong JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(new Engine());
}

JNIEXPORT void JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeDestroy(JNIEnv*, jobject, jlong h) {
    delete fromHandle(h);
}

JNIEXPORT jboolean JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeStart(
    JNIEnv* env, jobject, jlong h, jstring host, jint port, jboolean unprocessed) {
    const char* c = env->GetStringUTFChars(host, nullptr);
    std::string s(c);
    env->ReleaseStringUTFChars(host, c);
    return fromHandle(h)->start(s, port, unprocessed);
}

JNIEXPORT void JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeStop(JNIEnv*, jobject, jlong h) {
    fromHandle(h)->stop();
}

JNIEXPORT void JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeSetMuted(JNIEnv*, jobject, jlong h,
                                                                                      jboolean m) {
    fromHandle(h)->setMuted(m);
}

JNIEXPORT void JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeSetDeviceStatus(
    JNIEnv*, jobject, jlong h, jint battery, jint rssi, jboolean charging) {
    fromHandle(h)->setDeviceStatus(battery, rssi, charging);
}

// Returns doubles: peak, rms, packets, connected, live, opMute, loss, latency, jb,
// rate, sharing, preset, burst, cbFrames, perf, open, overruns
JNIEXPORT jdoubleArray JNICALL Java_pl_tajchert_slowik_audio_SlowikAudioEngine_nativeStats(JNIEnv* env, jobject,
                                                                                           jlong h) {
    auto s = fromHandle(h)->stats();
    const double v[] = {s.peak, s.rms, (double)s.packetsSent, (double)s.receiverConnected, (double)s.live,
                        (double)s.operatorMute, (double)s.lossPermille, (double)s.latencyMs, (double)s.jbMs,
                        (double)s.sampleRate, (double)s.sharingMode, (double)s.inputPreset, (double)s.framesPerBurst,
                        (double)s.framesPerCallback, (double)s.perfMode, (double)s.streamOpen, (double)s.overruns};
    jdoubleArray arr = env->NewDoubleArray(17);
    env->SetDoubleArrayRegion(arr, 0, 17, v);
    return arr;
}

}  // extern "C"
