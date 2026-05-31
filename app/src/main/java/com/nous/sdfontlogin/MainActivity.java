package com.nous.sdfontlogin;

import android.app.Activity;
import android.content.res.AssetManager;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.MotionEvent;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends Activity {
    private GLSurfaceView glView;
    private boolean glInitialized = false;

    static {
        System.loadLibrary("sdfontlogin");
    }

    private static native void nativeInitWithAssets(int width, int height, AssetManager assets);
    private static native void nativeResize(int width, int height);
    private static native void nativeRender();
    private static native void nativeTouchDown(float x, float y);
    private static native void nativeTouchUp(float x, float y);
    private static native void nativeTouchMove(float x, float y);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        glView = new GLSurfaceView(this);
        glView.setEGLContextClientVersion(3);
        glView.setRenderer(new Renderer());
        glView.setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
        setContentView(glView);

        glView.setOnTouchListener((v, event) -> {
            float x = event.getX();
            float y = event.getY();
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    nativeTouchDown(x, y);
                    break;
                case MotionEvent.ACTION_UP:
                    nativeTouchUp(x, y);
                    break;
                case MotionEvent.ACTION_MOVE:
                    nativeTouchMove(x, y);
                    break;
            }
            glView.requestRender();
            return true;
        });
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glView != null) glView.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glView != null) glView.onResume();
    }

    private class Renderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            if (!glInitialized) {
                nativeInitWithAssets(width, height, getAssets());
                glInitialized = true;
            } else {
                nativeResize(width, height);
            }
            glView.requestRender();
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            if (glInitialized) {
                nativeRender();
            }
        }
    }
}
