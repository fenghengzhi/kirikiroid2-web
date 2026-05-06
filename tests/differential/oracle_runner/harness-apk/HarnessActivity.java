package org.github.krkr2;

import android.os.Bundle;
import android.util.Log;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import org.tvp.kirikiri2.KR2Activity;

/**
 * Differential-oracle harness launched via:
 *
 *   am start -W -n org.github.krkr2/.HarnessActivity
 *
 * Extends {@link KR2Activity} so cocos2d's full init chain runs. The RPC
 * socket is started from Activity creation instead of waiting for a later
 * window-focus callback: headless Redroid runs can display the Activity while
 * omitting or delaying focus/resume callbacks enough for the host startup
 * probe to time out. Native commands that need TVPMainScene still retry until
 * cocos2d finishes its GL-thread bootstrap.
 *
 * A background thread binds 127.0.0.1:{@value #RPC_PORT} and hands each
 * accepted connection's raw file descriptor to {@link #runRpcServeFd},
 * implemented in libharness.so. The RPC protocol is the same line-based
 * dialect the standalone bionic ELF harness speaks (see
 * harness/README.md).
 */
public final class HarnessActivity extends KR2Activity {
    private static final String TAG = "HarnessRpc";
    private static final int RPC_PORT = 5039;

    static {
        System.loadLibrary("harness");
    }

    public static native int runRpcServeFd(int fd);

    private volatile boolean started = false;
    private Thread serverThread;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        startRpcServer("onCreate");
    }

    @Override
    protected void onResume() {
        super.onResume();
        startRpcServer("onResume");
    }

    private synchronized void startRpcServer(String source) {
        if (started) {
            Log.i(TAG, "server already started before " + source);
            return;
        }
        started = true;
        serverThread = new Thread(this::serveLoop, "harness-rpc");
        serverThread.setDaemon(true);
        serverThread.start();
        Log.i(TAG, "server thread started from " + source);
    }

    private void serveLoop() {
        try (ServerSocket server = new ServerSocket(RPC_PORT, 1,
                InetAddress.getByName("127.0.0.1"))) {
            Log.i(TAG, "listening on 127.0.0.1:" + RPC_PORT);
            while (!Thread.interrupted()) {
                Socket s = server.accept();
                s.setTcpNoDelay(true);
                int fd;
                try {
                    fd = getFdFromSocket(s);
                } catch (Throwable t) {
                    Log.e(TAG, "getFdFromSocket failed", t);
                    try { s.close(); } catch (Exception ignored) {}
                    continue;
                }
                Log.i(TAG, "accepted connection fd=" + fd);
                int rc = runRpcServeFd(fd);
                Log.i(TAG, "runRpcServeFd returned " + rc);
                try { s.close(); } catch (Exception ignored) {}
            }
        } catch (Exception e) {
            Log.e(TAG, "serveLoop died", e);
        }
    }

    /**
     * Extract the integer file descriptor from a {@link Socket}. Uses the
     * hidden {@code Socket.getFileDescriptor$()} method that has existed
     * unchanged since Android 4.0; if a future OS removes it we fall back
     * to a pure-Java I/O bridge (not implemented here).
     */
    private static int getFdFromSocket(Socket s) throws Exception {
        Method m = Socket.class.getDeclaredMethod("getFileDescriptor$");
        m.setAccessible(true);
        Object fdObj = m.invoke(s);
        Field descriptor = fdObj.getClass().getDeclaredField("descriptor");
        descriptor.setAccessible(true);
        return descriptor.getInt(fdObj);
    }
}
