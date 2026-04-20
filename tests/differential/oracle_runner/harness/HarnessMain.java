package org.krkr2;

/**
 * Launcher for the differential-oracle harness when it needs a real
 * Android runtime (JVM + ActivityThread) underneath libkrkr2. Launched
 * via {@code app_process} like so:
 *
 * <pre>
 * LD_LIBRARY_PATH=/data/local/tmp \
 * CLASSPATH=/data/local/tmp/harness-launcher.dex \
 * app_process /data/local/tmp org.krkr2.HarnessMain \
 *     /data/local/tmp/libharness.so /data/local/tmp/libkrkr2.so
 * </pre>
 *
 * <p>{@code argv[0]} is the absolute path to libharness.so; {@code argv[1..]}
 * is forwarded to {@link #runRpcLoop}, whose first element is expected
 * to be the libkrkr2.so path.
 *
 * <p>The Java side is deliberately thin: it brings up just enough of the
 * framework ({@code Looper} + {@code ActivityThread}) that libkrkr2's
 * JNI-backed init path sees a live VM and Android context, then hands
 * control to the native RPC loop in libharness.so (which is the exact
 * same code as the bare-bionic harness-aarch64 binary).
 */
public final class HarnessMain {

    public static void main(String[] argv) {
        err("HarnessMain: bootstrap start");
        try {
            // Main looper must exist before ActivityThread.systemMain() —
            // systemMain calls Looper.prepareMainLooper itself in recent
            // Android, but being explicit doesn't hurt on older builds.
            try {
                Class<?> looperCls = Class.forName("android.os.Looper");
                looperCls.getMethod("prepareMainLooper").invoke(null);
            } catch (Throwable t) {
                err("HarnessMain: Looper.prepareMainLooper failed: " + t);
            }

            // ActivityThread.systemMain() — returns an ActivityThread with
            // a system Context attached. Reflection-only because the class
            // is @hide in the SDK.
            try {
                Class<?> atCls = Class.forName("android.app.ActivityThread");
                Object at = atCls.getMethod("systemMain").invoke(null);
                err("HarnessMain: ActivityThread.systemMain ok, at=" + at);
                // Leave ApplicationContext unset for now — we add it only if
                // libkrkr2's init path actually blocks on it (see plan
                // Phase 2, case B).
            } catch (Throwable t) {
                err("HarnessMain: ActivityThread.systemMain failed: " + t);
                t.printStackTrace(System.err);
                // Continue — maybe libkrkr2 only needs the JVM itself.
            }
        } catch (Throwable t) {
            err("HarnessMain: framework bootstrap failed: " + t);
            t.printStackTrace(System.err);
        }

        if (argv.length < 1) {
            err("HarnessMain: usage: <libharness.so> <libkrkr2.so>");
            System.exit(2);
        }
        try {
            System.load(argv[0]);
        } catch (Throwable t) {
            err("HarnessMain: System.load(" + argv[0] + ") failed: " + t);
            t.printStackTrace(System.err);
            System.exit(2);
        }

        String[] nativeArgv = new String[argv.length - 1];
        for (int i = 1; i < argv.length; i++) nativeArgv[i - 1] = argv[i];
        err("HarnessMain: entering native RPC loop");
        int rc = runRpcLoop(nativeArgv);
        err("HarnessMain: native RPC loop returned " + rc);
        System.exit(rc);
    }

    private static void err(String msg) {
        System.err.println(msg);
        System.err.flush();
    }

    public static native int runRpcLoop(String[] argv);
}
