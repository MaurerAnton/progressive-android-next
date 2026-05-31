/*
 * SDFont Android Login Demo
 * Matrix Chat login UI using Signed Distance Field fonts
 * JNI bridge with GLSurfaceView
 * Multi-screen: Login, Test (guest), Account, Sign Up
 */

#include <jni.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define LOG_TAG "SDFontLogin"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "glyph_data.hpp"

/* ---------- GLSL Shaders ---------- */
static const char* kVertexShader = R"glsl(#version 300 es
precision mediump float;
in vec2 aPos;
in vec2 aTexCoord;
uniform mat4 uMVP;
out vec2 vTexCoord;
void main() {
    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)glsl";

static const char* kFragmentShader = R"glsl(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTexture;
uniform vec4 uColor;
uniform float uSmoothing;
uniform bool uIsTexture;
out vec4 fragColor;
void main() {
    if (uIsTexture) {
        float dist = texture(uTexture, vTexCoord).r;
        float alpha = smoothstep(0.5 - uSmoothing, 0.5 + uSmoothing, dist);
        fragColor = vec4(uColor.rgb, uColor.a * alpha);
    } else {
        fragColor = uColor;
    }
}
)glsl";

/* ---------- Colors ---------- */
#define COLOR_BG          0.10f, 0.10f, 0.14f, 1.0f
#define COLOR_TITLE       0.47f, 0.80f, 0.93f, 1.0f
#define COLOR_LABEL       0.70f, 0.70f, 0.75f, 1.0f
#define COLOR_BTN_TEXT    0.90f, 0.90f, 0.92f, 1.0f
#define COLOR_BTN_PRESS   0.25f, 0.30f, 0.38f, 1.0f
#define COLOR_BTN_TEST    0.25f, 0.50f, 0.35f, 1.0f
#define COLOR_BTN_LOGIN   0.47f, 0.80f, 0.93f, 1.0f
#define COLOR_BTN_SIGNUP  0.60f, 0.45f, 0.80f, 1.0f
#define COLOR_BTN_BACK    0.40f, 0.40f, 0.45f, 1.0f
#define COLOR_ACCENT      0.47f, 0.80f, 0.93f, 1.0f

/* ---------- Types ---------- */
struct Vec4 { float r, g, b, a; };
struct Rect { float x, y, w, h; };

struct Button {
    Rect   rect;
    const char* text;
    Vec4   color;
    bool   pressed;
};

struct GlyphVertex {
    float px, py;
    float tx, ty;
};

enum Screen { SCR_LOGIN = 0, SCR_TEST = 1, SCR_ACCOUNT = 2, SCR_SIGNUP = 3 };

/* ---------- Global State ---------- */
static struct {
    bool initialized;
    int  width, height;

    GLuint program;
    GLuint fontTexture;
    GLuint vboGlyph;
    GLuint vboButton;
    GLuint vaoGlyph;
    GLuint vaoButton;
    GLint  uMVP, uTexture, uColor, uSmoothing, uIsTexture;

    Screen screen;
    Button btns[4];     /* up to 4 buttons per screen */
    int    numBtns;
    int    activeButton;
    float  touchX, touchY;
    bool   touching;

    AAssetManager* assetMgr;
} G;

/* ---------- Shader helpers ---------- */
static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("Shader compile error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint createProgram(const char* vs, const char* fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOGE("Program link error: %s", log);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return prog;
}

static void ortho(float* m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
}

/* ---------- Text rendering ---------- */
static float measureText(const char* text, float scale) {
    float x = 0.0f;
    for (const char* p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || c > 126) c = 32;
        x += kGlyphs[c - 32].advance * scale * 1.15f;
    }
    return x;
}

static void renderText(float x, float y, const char* text, float scale, Vec4 color, float spacing) {
    if (!text || !*text) return;

    int len = (int)strlen(text);
    std::vector<GlyphVertex> verts(len * 4);
    std::vector<GLushort> indices(len * 6);

    float cx = x;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c > 126) c = 32;
        const SDFGlyph& g = kGlyphs[c - 32];

        float gx = cx;
        float gy = y - g.bearingY * scale;
        float gw = g.width * scale;
        float gh = g.height * scale;
        float tx = g.texX, ty = g.texY, tw = g.texW, th = g.texH;

        int vi = i * 4;
        float spread = SDF_SPREAD_METRICS * scale;
        verts[vi + 0] = {gx - spread,          gy - spread,          tx, ty + th};
        verts[vi + 1] = {gx + gw + spread,     gy - spread,          tx + tw, ty + th};
        verts[vi + 2] = {gx + gw + spread,     gy + gh + spread,     tx + tw, ty};
        verts[vi + 3] = {gx - spread,          gy + gh + spread,     tx, ty};

        int ii = i * 6;
        int base = vi;
        indices[ii + 0] = (GLushort)(base + 0);
        indices[ii + 1] = (GLushort)(base + 1);
        indices[ii + 2] = (GLushort)(base + 2);
        indices[ii + 3] = (GLushort)(base + 0);
        indices[ii + 4] = (GLushort)(base + 2);
        indices[ii + 5] = (GLushort)(base + 3);

        cx += g.advance * scale * spacing;
    }

    glUseProgram(G.program);
    glUniform1i(G.uIsTexture, 1);
    glUniform4f(G.uColor, color.r, color.g, color.b, color.a);
    glUniform1f(G.uSmoothing, 0.08f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, G.fontTexture);
    glUniform1i(G.uTexture, 0);

    float mvp[16];
    ortho(mvp, 0.0f, (float)G.width, (float)G.height, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(G.uMVP, 1, GL_FALSE, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, G.vboGlyph);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(GlyphVertex), verts.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoGlyph);

    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort), indices.data(), GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_SHORT, nullptr);
    glDeleteBuffers(1, &ibo);
    glBindVertexArray(0);
}

static void renderTextC(float x, float y, const char* text, float scale, Vec4 color) {
    renderText(x, y, text, scale, color, 1.15f);
}

/* ---------- Button rendering ---------- */
static void renderButton(const Button& btn, float textScale) {
    float x = btn.rect.x, y = btn.rect.y, w = btn.rect.w, h = btn.rect.h;
    Vec4 bg = btn.pressed ? Vec4{COLOR_BTN_PRESS} : btn.color;

    float verts[] = {
        x, y,     0.0f, 0.0f,
        x + w, y, 1.0f, 0.0f,
        x + w, y + h, 1.0f, 1.0f,
        x, y + h, 0.0f, 1.0f,
    };
    GLushort idx[] = {0, 1, 2, 0, 2, 3};

    glUseProgram(G.program);
    glUniform1i(G.uIsTexture, 0);
    glUniform4f(G.uColor, bg.r, bg.g, bg.b, bg.a);

    float mvp[16];
    ortho(mvp, 0.0f, (float)G.width, (float)G.height, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(G.uMVP, 1, GL_FALSE, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, G.vboButton);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoButton);

    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    glDeleteBuffers(1, &ibo);
    glBindVertexArray(0);

    if (btn.text) {
        float textW = measureText(btn.text, textScale);
        float tx = x + (w - textW) * 0.5f;
        float ty = y + h * 0.5f + textScale * 0.35f;
        renderTextC(tx, ty, btn.text, textScale, Vec4{COLOR_BTN_TEXT});
    }
}

static Button makeButton(float x, float y, float w, float h, const char* text, Vec4 color) {
    return {{x, y, w, h}, text, color, false};
}

/* ---------- Touch ---------- */
static bool pointInRect(float px, float py, const Rect& r) {
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

static int hitButton(float x, float y) {
    for (int i = 0; i < G.numBtns; i++) {
        if (pointInRect(x, y, G.btns[i].rect)) return i;
    }
    return -1;
}

/* ========== Screen Layouts ========== */

static void layoutUI();

static void doTouchDown(float x, float y) {
    G.touchX = x; G.touchY = y; G.touching = true;
    int hit = hitButton(x, y);
    if (hit >= 0) {
        G.btns[hit].pressed = true;
        G.activeButton = hit;
    }
}

static void doTouchUp(float x, float y) {
    G.touching = false;
    for (int i = 0; i < G.numBtns; i++) {
        if (G.btns[i].pressed) {
            G.btns[i].pressed = false;
            if (pointInRect(x, y, G.btns[i].rect)) {
                /* Button clicked - determine action by screen + index */
                switch (G.screen) {
                    case SCR_LOGIN:
                        if (i == 0) { LOGI("-> Test screen"); G.screen = SCR_TEST; }
                        else if (i == 1) { LOGI("-> Account screen"); G.screen = SCR_ACCOUNT; }
                        else if (i == 2) { LOGI("-> Sign Up screen"); G.screen = SCR_SIGNUP; }
                        break;
                    case SCR_TEST:
                    case SCR_ACCOUNT:
                    case SCR_SIGNUP:
                        if (i == 0) { LOGI("<- Back to login"); G.screen = SCR_LOGIN; }
                        break;
                }
                layoutUI();
            }
        }
    }
    G.activeButton = -1;
}

static void doTouchMove(float x, float y) {
    G.touchX = x; G.touchY = y;
    for (int i = 0; i < G.numBtns; i++) {
        if (i == G.activeButton) {
            G.btns[i].pressed = pointInRect(x, y, G.btns[i].rect);
        }
    }
}

/* ========== Screen Layouts ========== */

static void layoutUI() {
    int w = G.width, h = G.height;
    float btnW = w * 0.75f, btnH = 56.0f;
    float btnX = (w - btnW) * 0.5f;

    switch (G.screen) {
        case SCR_LOGIN: {
            float startY = h * 0.50f;
            float gap = 16.0f;
            G.btns[0] = makeButton(btnX, startY, btnW, btnH,
                "Test without account", Vec4{COLOR_BTN_TEST});
            G.btns[1] = makeButton(btnX, startY + btnH + gap, btnW, btnH,
                "Log in", Vec4{COLOR_BTN_LOGIN});
            G.btns[2] = makeButton(btnX, startY + 2*(btnH + gap), btnW, btnH,
                "Sign up", Vec4{COLOR_BTN_SIGNUP});
            G.numBtns = 3;
            break;
        }
        case SCR_TEST: {
            G.btns[0] = makeButton(btnX, h - 100.0f, btnW, btnH,
                "< Back", Vec4{COLOR_BTN_BACK});
            G.numBtns = 1;
            break;
        }
        case SCR_ACCOUNT: {
            G.btns[0] = makeButton(btnX, h - 100.0f, btnW, btnH,
                "< Back", Vec4{COLOR_BTN_BACK});
            G.numBtns = 1;
            break;
        }
        case SCR_SIGNUP: {
            G.btns[0] = makeButton(btnX, h - 100.0f, btnW, btnH,
                "< Back", Vec4{COLOR_BTN_BACK});
            G.numBtns = 1;
            break;
        }
    }
}

/* ========== Screen Rendering ========== */

static void renderScreenLogin() {
    int w = G.width, h = G.height;

    renderTextC((w - measureText("Matrix Chat", 38.0f)) * 0.5f, h * 0.18f,
        "Matrix Chat", 38.0f, Vec4{COLOR_TITLE});

    renderTextC((w - measureText("Connect to your Matrix homeserver", 18.0f)) * 0.5f, h * 0.24f,
        "Connect to your Matrix homeserver", 18.0f, Vec4{COLOR_LABEL});

    float lx = w * 0.125f, ly = h * 0.32f;
    renderTextC(lx, ly, "Server:  matrix.org", 18.0f, Vec4{COLOR_LABEL});
    renderTextC(lx, ly + 36.0f, "Username:", 18.0f, Vec4{COLOR_LABEL});
    renderTextC(lx, ly + 72.0f, "Password:", 18.0f, Vec4{COLOR_LABEL});

    for (int i = 0; i < G.numBtns; i++)
        renderButton(G.btns[i], 24.0f);

    const char* foot = "Powered by SDFont + OpenGL ES 3.0";
    renderTextC((w - measureText(foot, 14.0f)) * 0.5f, h - 40.0f, foot, 14.0f, Vec4{COLOR_LABEL});
}

static void renderScreenTest() {
    int w = G.width, h = G.height;

    renderTextC((w - measureText("Guest Access", 34.0f)) * 0.5f, h * 0.15f,
        "Guest Access", 34.0f, Vec4{COLOR_TITLE});

    const char* lines[] = {
        "You are browsing as a guest.",
        "No account required.",
        "",
        "Available public rooms:",
        "  #welcome:matrix.org",
        "  #general:matrix.org",
        "  #help:matrix.org",
        "",
        "Rooms would be listed here",
        "after connecting to the server.",
        nullptr
    };
    float y = h * 0.28f;
    for (int i = 0; lines[i]; i++) {
        renderTextC(w * 0.1f, y, lines[i], 16.0f, Vec4{COLOR_LABEL});
        y += 28.0f;
    }

    for (int i = 0; i < G.numBtns; i++)
        renderButton(G.btns[i], 22.0f);
}

static void renderScreenAccount() {
    int w = G.width, h = G.height;

    renderTextC((w - measureText("Log In", 34.0f)) * 0.5f, h * 0.12f,
        "Log In", 34.0f, Vec4{COLOR_TITLE});

    float lx = w * 0.15f;
    float ly = h * 0.28f;
    float gap = 50.0f;

    renderTextC(lx, ly, "Server:", 20.0f, Vec4{COLOR_ACCENT});
    /* Draw a line/underline for the input field */
    float fieldW = w * 0.7f;
    float uy = ly + 10.0f;
    Button fieldLine = {{lx, uy, fieldW, 2.0f}, nullptr, Vec4{0.3f,0.35f,0.4f,1.0f}, false};
    renderButton(fieldLine, 0);
    renderTextC(lx + 6.0f, ly - 6.0f, "matrix.org", 18.0f, Vec4{COLOR_LABEL});

    ly += gap;
    renderTextC(lx, ly, "Username:", 20.0f, Vec4{COLOR_ACCENT});
    fieldLine = {{lx, ly + 10.0f, fieldW, 2.0f}, nullptr, Vec4{0.3f,0.35f,0.4f,1.0f}, false};
    renderButton(fieldLine, 0);
    renderTextC(lx + 6.0f, ly - 6.0f, "@user:matrix.org", 18.0f, Vec4{COLOR_LABEL});

    ly += gap;
    renderTextC(lx, ly, "Password:", 20.0f, Vec4{COLOR_ACCENT});
    fieldLine = {{lx, ly + 10.0f, fieldW, 2.0f}, nullptr, Vec4{0.3f,0.35f,0.4f,1.0f}, false};
    renderButton(fieldLine, 0);
    renderTextC(lx + 6.0f, ly - 6.0f, "********", 18.0f, Vec4{COLOR_LABEL});

    /* Log in button */
    float btnW = w * 0.5f, btnH = 50.0f;
    Button loginBtn = {{(w - btnW) * 0.5f, ly + gap, btnW, btnH},
        "Connect", Vec4{COLOR_BTN_LOGIN}, false};
    renderButton(loginBtn, 22.0f);

    for (int i = 0; i < G.numBtns; i++)
        renderButton(G.btns[i], 22.0f);
}

static void renderScreenSignup() {
    int w = G.width, h = G.height;

    renderTextC((w - measureText("Create Account", 34.0f)) * 0.5f, h * 0.12f,
        "Create Account", 34.0f, Vec4{COLOR_TITLE});

    float lx = w * 0.15f;
    float ly = h * 0.28f;
    float gap = 50.0f;
    float fieldW = w * 0.7f;

    const char* fields[] = {"Server:", "Username:", "Password:", "Confirm:", nullptr};
    const char* values[] = {"matrix.org", "newuser", "********", "********", nullptr};
    for (int i = 0; fields[i]; i++) {
        renderTextC(lx, ly, fields[i], 20.0f, Vec4{COLOR_ACCENT});
        Button line = {{lx, ly + 10.0f, fieldW, 2.0f}, nullptr, Vec4{0.3f,0.35f,0.4f,1.0f}, false};
        renderButton(line, 0);
        renderTextC(lx + 6.0f, ly - 6.0f, values[i], 18.0f, Vec4{COLOR_LABEL});
        ly += gap;
    }

    float btnW = w * 0.5f, btnH = 50.0f;
    Button signBtn = {{(w - btnW) * 0.5f, ly + 20.0f, btnW, btnH},
        "Register", Vec4{COLOR_BTN_SIGNUP}, false};
    renderButton(signBtn, 22.0f);

    for (int i = 0; i < G.numBtns; i++)
        renderButton(G.btns[i], 22.0f);
}

static void renderFrame() {
    glClearColor(COLOR_BG);
    glClear(GL_COLOR_BUFFER_BIT);

    switch (G.screen) {
        case SCR_LOGIN:   renderScreenLogin();   break;
        case SCR_TEST:    renderScreenTest();    break;
        case SCR_ACCOUNT: renderScreenAccount(); break;
        case SCR_SIGNUP:  renderScreenSignup();  break;
    }
}

/* ---------- Initialization ---------- */
static bool initGL(JNIEnv* env, jobject assetManager) {
    LOGI("initGL start, screen %dx%d", G.width, G.height);
    G.assetMgr = AAssetManager_fromJava(env, assetManager);
    if (!G.assetMgr) { LOGE("AAssetManager_fromJava failed"); return false; }

    G.program = createProgram(kVertexShader, kFragmentShader);
    if (!G.program) { LOGE("Shader fail"); return false; }

    G.uMVP       = glGetUniformLocation(G.program, "uMVP");
    G.uTexture   = glGetUniformLocation(G.program, "uTexture");
    G.uColor     = glGetUniformLocation(G.program, "uColor");
    G.uSmoothing = glGetUniformLocation(G.program, "uSmoothing");
    G.uIsTexture = glGetUniformLocation(G.program, "uIsTexture");

    AAsset* asset = AAssetManager_open(G.assetMgr, "noto_sans_sdf.png", AASSET_MODE_BUFFER);
    if (!asset) { LOGE("Asset open fail"); return false; }

    const void* data = AAsset_getBuffer(asset);
    off_t len = AAsset_getLength(asset);
    int texW, texH, ch;
    stbi_set_flip_vertically_on_load(1);
    unsigned char* pixels = stbi_load_from_memory((const stbi_uc*)data, (int)len, &texW, &texH, &ch, 1);
    AAsset_close(asset);

    if (!pixels) { LOGE("PNG decode fail: %s", stbi_failure_reason()); return false; }
    LOGI("Texture: %dx%d ch=%d", texW, texH, ch);

    glGenTextures(1, &G.fontTexture);
    glBindTexture(GL_TEXTURE_2D, G.fontTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, texW, texH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);

    glGenVertexArrays(1, &G.vaoGlyph);
    glBindVertexArray(G.vaoGlyph);
    glGenBuffers(1, &G.vboGlyph);
    glBindBuffer(GL_ARRAY_BUFFER, G.vboGlyph);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex), (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glGenVertexArrays(1, &G.vaoButton);
    glBindVertexArray(G.vaoButton);
    glGenBuffers(1, &G.vboButton);
    glBindBuffer(GL_ARRAY_BUFFER, G.vboButton);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, G.width, G.height);

    G.screen = SCR_LOGIN;
    layoutUI();
    G.initialized = true;
    LOGI("initGL done");
    return true;
}

/* ---------- JNI Exports ---------- */
extern "C" {

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeInitWithAssets(
    JNIEnv* env, jclass, jint width, jint height, jobject assetManager) {
    G.width = width; G.height = height;
    if (!G.initialized) initGL(env, assetManager);
}

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeResize(JNIEnv*, jclass, jint w, jint h) {
    G.width = w; G.height = h;
    if (G.initialized) { glViewport(0, 0, w, h); layoutUI(); }
}

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeRender(JNIEnv*, jclass) {
    if (G.initialized) renderFrame();
}

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeTouchDown(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.initialized) doTouchDown(x, y);
}

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeTouchUp(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.initialized) doTouchUp(x, y);
}

JNIEXPORT void JNICALL
Java_com_nous_sdfontlogin_MainActivity_nativeTouchMove(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.initialized) doTouchMove(x, y);
}

} /* extern "C" */
