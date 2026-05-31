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
        overlayEdit.setTextColor(0x00000000);
        overlayEdit.setCursorVisible(false);
        overlayEdit.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
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

    private void updateOverlay() {
        int ff = nativeGetFocusField();
        if (ff > 0) {
            String text = nativeGetFieldText(ff);
            overlayEdit.setText(text);
            overlayEdit.setSelection(text.length());
            /* Position EditText at field location - coordinates from native */
            float[] pos = getFieldPosition(ff);
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                (int)(glView.getWidth() * 0.80f), 100);
            lp.leftMargin = (int)pos[0];
            lp.topMargin = (int)pos[1];
            overlayEdit.setLayoutParams(lp);
            overlayEdit.setVisibility(View.VISIBLE);
            overlayEdit.requestFocus();
        } else {
            commitText();
            overlayEdit.setVisibility(View.GONE);
            glView.requestFocus();
        }
    }

    private float[] getFieldPosition(int ff) {
        /* Approximate field Y positions based on card layout */
        float cardY = (float)(glView.getHeight() * 0.20);
        float pad = glView.getWidth() * 0.06f;
        float fieldH = 52.0f * (glView.getWidth() / 411.0f);
        float fieldGap = 24.0f * (glView.getWidth() / 411.0f);
        float y = cardY + 24.0f * (glView.getWidth() / 411.0f) + 40.0f * (glView.getWidth() / 411.0f)
                + 8.0f * (glView.getWidth() / 411.0f) + 16.0f * (glView.getWidth() / 411.0f);
        y += (ff - 1) * (fieldH + fieldGap) + 18.0f;
        return new float[]{pad, y};
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
