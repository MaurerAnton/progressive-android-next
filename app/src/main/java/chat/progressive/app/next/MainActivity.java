package chat.progressive.app.next;

import android.app.Activity;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;
import android.widget.FrameLayout;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends Activity {
    private GLSurfaceView glView;
    private EditText overlayEdit;
    private boolean glReady = false;

    static { System.loadLibrary("progressivenext"); }

    private static native void nativeInit(int w, int h, AssetManager assets);
    private static native void nativeResize(int w, int h);
    private static native void nativeRender();
    private static native void nativeTouchDown(float x, float y);
    private static native void nativeTouchUp(float x, float y);
    private static native void nativeTouchMove(float x, float y);
    private static native int  nativeGetFocusField();
    private static native String nativeGetFieldText(int field);
    private static native void nativeSetFieldText(int field, String text);

    @Override protected void onCreate(Bundle s) {
        super.onCreate(s);

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
            /* Chat input field - bottom of screen */
            float ibH = 40 * dp;
            float y = glView.getHeight() - ibH;
            return new float[]{6, y + 6, glView.getWidth() - 74, ibH - 12};
        }
        if (ff == 5) {
            /* Search field - below header */
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
