package chat.progressive.app.next;

import android.app.Activity;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.MotionEvent;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends Activity {
    private GLSurfaceView glView;
    private boolean glReady = false;

    static { System.loadLibrary("progressivenext"); }

    private static native void nativeInit(int w, int h, AssetManager assets);
    private static native void nativeResize(int w, int h);
    private static native void nativeRender();
    private static native void nativeTouchDown(float x, float y);
    private static native void nativeTouchUp(float x, float y);
    private static native void nativeTouchMove(float x, float y);
    private static native void nativeFling(float vx, float vy);

    @Override protected void onCreate(Bundle s) {
        super.onCreate(s);
        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setRenderer(new R());
        glView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        setContentView(glView);
        glView.setOnTouchListener((v, e) -> {
            float x = e.getX(), y = e.getY();
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: nativeTouchDown(x, y); break;
                case MotionEvent.ACTION_UP:   nativeTouchUp(x, y); break;
                case MotionEvent.ACTION_MOVE: nativeTouchMove(x, y); break;
            }
            glView.requestRender(); return true;
        });
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
