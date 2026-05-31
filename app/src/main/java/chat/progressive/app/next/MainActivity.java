package chat.progressive.app.next;

import android.app.Activity;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import java.io.*;
import java.net.*;
import java.util.concurrent.*;
import javax.net.ssl.*;

public class MainActivity extends Activity {
    private GLSurfaceView glView;
    private EditText overlayEdit;
    private boolean glReady = false;
    private Handler mainHandler = new Handler(Looper.getMainLooper());
    private ExecutorService networkExecutor = Executors.newSingleThreadExecutor();

    static { System.loadLibrary("progressivenext"); }

    private static native void nativeInit(int w, int h, AssetManager assets);
    private static native void nativeSetActivity(MainActivity act);
    private static native void nativeResize(int w, int h);
    private static native void nativeRender();
    private static native void nativeTouchDown(float x, float y);
    private static native void nativeTouchUp(float x, float y);
    private static native void nativeTouchMove(float x, float y);
    private static native int  nativeGetFocusField();
    private static native String nativeGetFieldText(int field);
    private static native void nativeSetFieldText(int field, String text);
    /* Matrix callbacks */
    private static native void nativeOnMatrixResult(String json);
    private static native void nativeOnMatrixError(String error);

    private String accessToken = null;
    private String userId = null;
    private String deviceId = null;
    private String homeserverUrl = "https://progressive.chat";
    private String syncSince = null;
    private boolean syncing = false;

    @Override protected void onCreate(Bundle s) {
        super.onCreate(s);
        nativeSetActivity(this);

        FrameLayout root = new FrameLayout(this);
        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setRenderer(new R());
        glView.setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        root.addView(glView);

        overlayEdit = new EditText(this);
        overlayEdit.setBackgroundColor(0x00000000);
        overlayEdit.setTextColor(0xFFFFFFFF);
        overlayEdit.setTextSize(16);
        overlayEdit.setCursorVisible(true);
        overlayEdit.setHighlightColor(0x335bc0de);
        overlayEdit.setPadding(14, 0, 14, 0);
        overlayEdit.setImeOptions(EditorInfo.IME_ACTION_DONE);
        overlayEdit.setVisibility(View.GONE);
        overlayEdit.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                commitText();
                overlayEdit.setVisibility(View.GONE);
                glView.requestFocus();
                return true;
            }
            return false;
        });
        root.addView(overlayEdit);
        setContentView(root);

        glView.setOnTouchListener((v, e) -> {
            float x = e.getX(), y = e.getY();
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: nativeTouchDown(x, y); break;
                case MotionEvent.ACTION_UP:   nativeTouchUp(x, y); updateOverlay(); break;
                case MotionEvent.ACTION_MOVE: nativeTouchMove(x, y); break;
            }
            glView.requestRender(); return true;
        });
    }

    /* ===== Matrix HTTP API ===== */

    public void matrixLogin(String user, String password) {
        networkExecutor.execute(() -> {
            try {
                String json = "{\"type\":\"m.login.password\",\"identifier\":{\"type\":\"m.id.user\",\"user\":\"" + user + "\"},\"password\":\"" + escapeJson(password) + "\"}";
                String result = httpPost(homeserverUrl + "/_matrix/client/v3/login", json);
                accessToken = extractJsonString(result, "access_token");
                userId = extractJsonString(result, "user_id");
                deviceId = extractJsonString(result, "device_id");
                String finalResult = "{\"type\":\"login\",\"ok\":true,\"user_id\":\"" + (userId != null ? escapeJson(userId) : "") + "\",\"access_token\":\"" + (accessToken != null ? escapeJson(accessToken) : "") + "\"}";
                mainHandler.post(() -> nativeOnMatrixResult(finalResult));
                if (accessToken != null) startSync();
            } catch (Exception e) {
                mainHandler.post(() -> nativeOnMatrixError("Login failed: " + e.getMessage()));
            }
        });
    }

    public void matrixSync() {
        if (accessToken == null) return;
        syncing = true;
        networkExecutor.execute(() -> {
            try {
                String url = homeserverUrl + "/_matrix/client/v3/sync?timeout=30000&filter={\"room\":{\"timeline\":{\"limit\":50}}}";
                if (syncSince != null) url += "&since=" + syncSince;
                String result = httpGet(url, accessToken);
                syncSince = extractJsonString(result, "next_batch");
                mainHandler.post(() -> nativeOnMatrixResult(result));
            } catch (Exception e) {
                mainHandler.post(() -> nativeOnMatrixError("Sync failed: " + e.getMessage()));
            }
            syncing = false;
        });
    }

    public void matrixSendMessage(String roomId, String text) {
        if (accessToken == null) return;
        networkExecutor.execute(() -> {
            try {
                String txnId = String.valueOf(System.currentTimeMillis());
                String url = homeserverUrl + "/_matrix/client/v3/rooms/" + roomId + "/send/m.room.message/" + txnId;
                String json = "{\"msgtype\":\"m.text\",\"body\":\"" + escapeJson(text) + "\"}";
                String result = httpPut(url, json, accessToken);
                mainHandler.post(() -> nativeOnMatrixResult("{\"type\":\"send\",\"ok\":true,\"event_id\":\"" + escapeJson(extractJsonString(result, "event_id")) + "\"}"));
            } catch (Exception e) {
                mainHandler.post(() -> nativeOnMatrixError("Send failed: " + e.getMessage()));
            }
        });
    }

    private void startSync() {
        if (syncing) return;
        matrixSync();
        // Poll every 10 seconds
        mainHandler.postDelayed(() -> { matrixSync(); if (accessToken != null) mainHandler.postDelayed(() -> startSync(), 10000); }, 10000);
    }

    /* ===== HTTP helpers ===== */

    private String httpGet(String url, String token) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setRequestMethod("GET");
        conn.setRequestProperty("Authorization", "Bearer " + token);
        conn.setConnectTimeout(15000);
        conn.setReadTimeout(35000);
        return readResponse(conn);
    }

    private String httpPost(String url, String body) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setRequestMethod("POST");
        conn.setRequestProperty("Content-Type", "application/json");
        conn.setDoOutput(true);
        conn.setConnectTimeout(15000);
        conn.setReadTimeout(15000);
        OutputStream os = conn.getOutputStream();
        os.write(body.getBytes("UTF-8"));
        os.close();
        return readResponse(conn);
    }

    private String httpPut(String url, String body, String token) throws Exception {
        HttpURLConnection conn = (HttpURLConnection) new URL(url).openConnection();
        conn.setRequestMethod("PUT");
        conn.setRequestProperty("Content-Type", "application/json");
        conn.setRequestProperty("Authorization", "Bearer " + token);
        conn.setDoOutput(true);
        conn.setConnectTimeout(15000);
        conn.setReadTimeout(15000);
        OutputStream os = conn.getOutputStream();
        os.write(body.getBytes("UTF-8"));
        os.close();
        return readResponse(conn);
    }

    private String readResponse(HttpURLConnection conn) throws Exception {
        int code = conn.getResponseCode();
        InputStream is = (code >= 200 && code < 300) ? conn.getInputStream() : conn.getErrorStream();
        if (is == null) throw new Exception("HTTP " + code);
        BufferedReader br = new BufferedReader(new InputStreamReader(is, "UTF-8"));
        StringBuilder sb = new StringBuilder();
        String line;
        while ((line = br.readLine()) != null) sb.append(line);
        br.close();
        conn.disconnect();
        return sb.toString();
    }

    private String extractJsonString(String json, String key) {
        String search = "\"" + key + "\":\"";
        int i = json.indexOf(search);
        if (i < 0) { search = "\"" + key + "\": \""; i = json.indexOf(search); }
        if (i < 0) return null;
        i += search.length();
        int j = i;
        while (j < json.length() && json.charAt(j) != '"') {
            if (json.charAt(j) == '\\') j++;
            j++;
        }
        return json.substring(i, j).replace("\\\"", "\"").replace("\\\\", "\\");
    }

    private String escapeJson(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n");
    }

    /* ===== JNI bridge for C++ to call Java ===== */

    public void jniLogin(String user, String pass) { matrixLogin(user, pass); }
    public void jniSync() { matrixSync(); }
    public void jniSendMessage(String roomId, String text) { matrixSendMessage(roomId, text); }
    public String jniGetToken() { return accessToken; }
    public String jniGetUserId() { return userId; }
    public String jniGetHomeserver() { return homeserverUrl; }

    /* ===== Existing overlay handling ===== */

    private void commitText() {
        int ff = nativeGetFocusField();
        if (ff > 0) {
            String text = overlayEdit.getText().toString();
            nativeSetFieldText(ff, text);
        }
    }

    private float[] getFieldPosition(int ff) {
        float dp = glView.getWidth() / 411.0f;
        if (ff == 4) {
            float ibH = 40 * dp;
            float y = glView.getHeight() - ibH;
            return new float[]{6, y + 6, glView.getWidth() - 74, ibH - 12};
        }
        if (ff == 5) {
            float hdrH = 84 * dp;
            float y = hdrH + 4;
            return new float[]{36, y + 4, glView.getWidth() - 80, 30 * dp};
        }
        float cardH = 40*dp + 3*(52*dp + 24*dp) + 2*24*dp + 48*dp + 20*dp;
        float cardY = (glView.getHeight() - cardH) * 0.20f;
        if (cardY < glView.getHeight() * 0.02f) cardY = glView.getHeight() * 0.02f;
        float pad = glView.getWidth() * 0.06f;
        float fw = glView.getWidth() * 0.88f;
        float fieldTop = cardY + 24*dp + 40*dp + 8*dp + 16*dp;
        float fieldH = 52 * dp;
        float fieldGap = 24 * dp;
        float y = fieldTop + (ff - 1) * (fieldH + fieldGap) + 18;
        return new float[]{pad, y, fw, fieldH};
    }

    private void updateOverlay() {
        int ff = nativeGetFocusField();
        if (ff > 0) {
            String text = nativeGetFieldText(ff);
            overlayEdit.setText(text);
            overlayEdit.setSelection(text.length());
            float[] pos = getFieldPosition(ff);
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                (int)pos[2], (int)pos[3]);
            lp.leftMargin = (int)pos[0];
            lp.topMargin = (int)pos[1];
            overlayEdit.setLayoutParams(lp);
            if (ff == 3) overlayEdit.setInputType(
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            else overlayEdit.setInputType(
                InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
            overlayEdit.setVisibility(View.VISIBLE);
            overlayEdit.requestFocus();
        } else {
            commitText();
            overlayEdit.setVisibility(View.GONE);
            glView.requestFocus();
        }
    }

    @Override protected void onPause()  { super.onPause(); if (glView != null) glView.onPause(); }
    @Override protected void onResume() { super.onResume(); if (glView != null) glView.onResume(); }

    class R implements GLSurfaceView.Renderer {
        public void onSurfaceCreated(GL10 gl, EGLConfig c) {}
        public void onSurfaceChanged(GL10 gl, int w, int h) {
            nativeInit(w, h, getAssets()); glReady = true; glView.requestRender();
        }
        public void onDrawFrame(GL10 gl) { if (glReady) nativeRender(); }
    }
}
