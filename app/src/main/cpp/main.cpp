/*
 * Progressive Android Next - Pure C++/OpenGL ES Matrix Chat
 * IRC-styled UI with SDFont text rendering
 * Zero JVM UI code (Java only provides GLSurfaceView bootstrap)
 *
 * Will be merged into progressive-android 0.5.5+
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
#include <unordered_map>

#define LOG_TAG "ProgressiveNext"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "glyph_data.hpp"

/* ======== SHADERS ======== */
static const char* kVS = R"glsl(#version 300 es
precision mediump float;
in vec2 aPos; in vec2 aTexCoord;
uniform mat4 uMVP;
out vec2 vTexCoord;
void main() { gl_Position = uMVP * vec4(aPos,0,1); vTexCoord = aTexCoord; }
)glsl";

static const char* kFS = R"glsl(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform float uSmooth;
uniform bool uIsTex;
out vec4 fragColor;
void main() {
    if (uIsTex) {
        float d = texture(uTex, vTexCoord).r;
        float a = smoothstep(0.5-uSmooth, 0.5+uSmooth, d);
        fragColor = vec4(uColor.rgb, uColor.a*a);
    } else { fragColor = uColor; }
}
)glsl";

/* ======== COLORS ======== */
#define C_BG       0.08f,0.08f,0.12f,1.0f
#define C_DARKER   0.05f,0.05f,0.08f,1.0f
#define C_TITLE    0.36f,0.77f,0.90f,1.0f
#define C_LABEL    0.65f,0.65f,0.70f,1.0f
#define C_WHITE    0.90f,0.90f,0.92f,1.0f
#define C_BTN_BG   0.15f,0.18f,0.22f,1.0f
#define C_BTN_PR   0.22f,0.26f,0.32f,1.0f
#define C_GREEN    0.20f,0.60f,0.35f,1.0f
#define C_CYAN     0.36f,0.77f,0.90f,1.0f
#define C_PURPLE   0.55f,0.40f,0.75f,1.0f
#define C_BACK     0.35f,0.35f,0.40f,1.0f
#define C_DRAWER   0.10f,0.10f,0.15f,1.0f
#define C_ROOM_SEL 0.15f,0.18f,0.25f,1.0f
#define C_TIMESTAMP 0.40f,0.40f,0.45f,1.0f
#define C_INPUT_BG 0.12f,0.12f,0.17f,1.0f
#define C_SEND     0.36f,0.77f,0.90f,1.0f
#define C_TOGGLE_ON 0.36f,0.77f,0.90f,1.0f
#define C_TOGGLE_OFF 0.25f,0.25f,0.30f,1.0f

/* Nickname colors - 16-color IRC-style palette */
static const float kNicks[16][3] = {
    {0.90f,0.30f,0.30f},{0.30f,0.80f,0.30f},{0.36f,0.77f,0.90f},
    {0.90f,0.70f,0.20f},{0.85f,0.40f,0.85f},{0.30f,0.70f,0.30f},
    {0.30f,0.40f,0.90f},{0.90f,0.50f,0.20f},{0.55f,0.40f,0.75f},
    {0.20f,0.70f,0.50f},{0.40f,0.50f,0.85f},{0.80f,0.60f,0.20f},
    {0.70f,0.30f,0.70f},{0.20f,0.65f,0.45f},{0.35f,0.55f,0.85f},
    {0.75f,0.45f,0.25f}
};

/* ======== TYPES ======== */
struct Vec4 { float r,g,b,a; };
struct Rect { float x,y,w,h; };
struct Button { Rect rect; const char* text; Vec4 color; bool pressed; };
struct GlyphVertex { float px,py,tx,ty; };

struct Message {
    const char* nick;     /* nullptr = system message */
    const char* text;
    int hour, minute;
    int nickColorIdx;
};

struct Room {
    const char* name;
    const char* topic;
    std::vector<Message> msgs;
    int unread;
};

enum Screen { SCR_LOGIN, SCR_CHAT };
enum DrawerState { DRAWER_CLOSED, DRAWER_OPEN, DRAWER_ANIM };

/* ======== GLOBAL STATE ======== */
static struct {
    bool init;
    int w, h;
    float dpr;

    /* GL */
    GLuint prog, tex, vboG, vboR, vaoG, vaoR;
    GLint uMVP, uTex, uColor, uSmooth, uIsTex;

    /* Screen */
    Screen screen;

    /* Login form */
    struct { bool tls; } loginForm;

    /* Chat */
    int activeRoom;
    float scrollY, scrollVel, maxScroll;
    int scrollTrackId;
    float scrollLastY;

    /* Drawer */
    DrawerState drawer;
    float drawerX; /* 0=closed, drawerW=open */
    float drawerW; /* computed from screen width */

    /* Buttons */
    Button btns[8];
    int nBtns, activeBtn;

    /* Touch */
    float tx, ty;
    bool touching;
    int touchId;

    /* Rooms/data */
    std::vector<Room> rooms;

    AAssetManager* amgr;
} G;

/* ======== HELPERS ======== */
static GLuint mkShader(GLenum t, const char* s) {
    GLuint sh = glCreateShader(t);
    glShaderSource(sh, 1, &s, nullptr);
    glCompileShader(sh);
    GLint ok; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, nullptr, l); LOGE("shader: %s", l); return 0; }
    return sh;
}
static GLuint mkProg(const char* vs, const char* fs) {
    GLuint v = mkShader(GL_VERTEX_SHADER, vs), f = mkShader(GL_FRAGMENT_SHADER, fs);
    if (!v||!f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v); glDeleteShader(f);
    if (!ok) { char l[512]; glGetProgramInfoLog(p, 512, nullptr, l); LOGE("link: %s", l); return 0; }
    return p;
}
static void ortho(float* m, float l, float r, float b, float t, float n, float f) {
    memset(m, 0, 64);
    m[0]=2/(r-l); m[5]=2/(t-b); m[10]=-2/(f-n);
    m[12]=-(r+l)/(r-l); m[13]=-(t+b)/(t-b); m[14]=-(f+n)/(f-n); m[15]=1;
}

/* ======== TEXT ======== */
static float measure(const char* t, float s) {
    float x = 0;
    for (const char* p = t; *p; p++) {
        unsigned char c = *p; if (c<32||c>126) c=32;
        x += kGlyphs[c-32].advance * s * 1.15f;
    }
    return x;
}
static void text(float x, float y, const char* t, float scale, Vec4 c, float spacing=1.15f) {
    if (!t||!*t) return;
    int n = strlen(t);
    std::vector<GlyphVertex> v(n*4);
    std::vector<GLushort> idx(n*6);
    float cx = x;
    for (int i = 0; i < n; i++) {
        unsigned char ch = t[i]; if (ch<32||ch>126) ch=32;
        const SDFGlyph& g = kGlyphs[ch-32];
        float gx = cx, gy = y - g.bearingY * scale, gw = g.width*scale, gh = g.height*scale;
        float tx=g.texX, ty=g.texY, tw=g.texW, th=g.texH;
        int vi = i*4;
        float sp = SDF_SPREAD_METRICS * scale;
        v[vi+0]={gx-sp, gy-sp, tx, ty+th};
        v[vi+1]={gx+gw+sp, gy-sp, tx+tw, ty+th};
        v[vi+2]={gx+gw+sp, gy+gh+sp, tx+tw, ty};
        v[vi+3]={gx-sp, gy+gh+sp, tx, ty};
        int ii=i*6, b=vi;
        idx[ii+0]=b; idx[ii+1]=b+1; idx[ii+2]=b+2;
        idx[ii+3]=b; idx[ii+4]=b+2; idx[ii+5]=b+3;
        cx += g.advance*scale*spacing;
    }
    glUseProgram(G.prog);
    glUniform1i(G.uIsTex, 1);
    glUniform4f(G.uColor, c.r,c.g,c.b,c.a);
    glUniform1f(G.uSmooth, 0.08f);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, G.tex); glUniform1i(G.uTex, 0);
    float mvp[16]; ortho(mvp, 0, (float)G.w, (float)G.h, 0, -1, 1);
    glUniformMatrix4fv(G.uMVP, 1, GL_FALSE, mvp);
    glBindBuffer(GL_ARRAY_BUFFER, G.vboG);
    glBufferData(GL_ARRAY_BUFFER, v.size()*sizeof(GlyphVertex), v.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoG);
    GLuint ibo; glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size()*sizeof(GLushort), idx.data(), GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, idx.size(), GL_UNSIGNED_SHORT, nullptr);
    glDeleteBuffers(1, &ibo); glBindVertexArray(0);
}

/* ======== RECTANGLES ======== */
static void rect(float x, float y, float w, float h, Vec4 c) {
    float v[] = {x,y,0,0, x+w,y,1,0, x+w,y+h,1,1, x,y+h,0,1};
    GLushort idx[] = {0,1,2,0,2,3};
    glUseProgram(G.prog);
    glUniform1i(G.uIsTex, 0);
    glUniform4f(G.uColor, c.r,c.g,c.b,c.a);
    float mvp[16]; ortho(mvp, 0, (float)G.w, (float)G.h, 0, -1, 1);
    glUniformMatrix4fv(G.uMVP, 1, GL_FALSE, mvp);
    glBindBuffer(GL_ARRAY_BUFFER, G.vboR);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoR);
    GLuint ibo; glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
    glDeleteBuffers(1, &ibo); glBindVertexArray(0);
}

static void btn(const Button& b, float ts) {
    rect(b.rect.x, b.rect.y, b.rect.w, b.rect.h, b.pressed ? Vec4{C_BTN_PR} : b.color);
    if (b.text) {
        float tw = measure(b.text, ts);
        text(b.rect.x+(b.rect.w-tw)*0.5f, b.rect.y+b.rect.h*0.5f+ts*0.35f, b.text, ts, Vec4{C_WHITE});
    }
}

static Button mkBtn(float x, float y, float w, float h, const char* t, Vec4 c) {
    return {{x,y,w,h}, t, c, false};
}

/* ======== TOUCH ======== */
static bool hitRect(float px, float py, const Rect& r) {
    return px>=r.x && px<=r.x+r.w && py>=r.y && py<=r.y+r.h;
}
static int hitBtn(float x, float y) {
    for (int i = 0; i < G.nBtns; i++)
        if (hitRect(x, y, G.btns[i].rect)) return i;
    return -1;
}

/* ======== MOCK DATA ======== */
static void genMockData() {
    G.rooms.clear();
    const char* rnames[] = {"#welcome", "#general", "#random", "#dev", "#matrix"};
    const char* rtopics[] = {
        "Welcome to Progressive IRC | Please read the rules",
        "General discussion about anything and everything",
        "Random off-topic chat | Keep it civil",
        "Development discussion | C++ / OpenGL / Matrix",
        "Matrix protocol discussion and bridge testing"
    };
    struct { const char* n; const char* t; int h, m; } msgs[][8] = {
        {{"ServerBot","Welcome to #welcome! Please read /topic.",9,5},
         {nullptr,"--> alice (~alice@matrix) has joined #welcome",9,6},
         {"alice","hello everyone!",9,6},
         {nullptr,"--> bob (~bob@matrix) has joined #welcome",9,7},
         {"bob","hey alice, how's it going?",9,7},
         {nullptr,"--> charlie (~charlie@irc) has joined #welcome",9,9},
         {"alice","pretty good! working on the new renderer",9,8},
         {"charlie","what renderer are you using?",9,10}},
        {{"ServerBot","Channel #general created",10,0},
         {nullptr,"--> dave (~dave@matrix) has joined #general",10,14},
         {"dave","anyone here?",10,15},
         {"eve","yep, just lurking",10,16},
         {nullptr,"<-- frank (~frank@irc) has left #general",10,18}},
        {{"frank","lol check this out https://example.com",11,20},
         {"grace","haha that's amazing",11,21},
         {"heidi","i don't get it",11,22},
         {"frank","you had to be there",11,23}},
        {{"ivan","pushed a new commit to the renderer",14,0},
         {nullptr,"--> judy (~judy@dev) has joined #dev",14,1},
         {"judy","nice! SDF fonts looking crisp?",14,2},
         {"ivan","yeah, the smoothstep AA is working great now",14,3},
         {"karen","can we use this for the main app?",14,5},
         {"ivan","that's the plan. merging into 0.5.5+",14,6}},
        {{"larry","testing the matrix bridge",15,30},
         {"mike","seems to be working",15,31},
         {"nancy","which homeserver?",15,32},
         {"larry","matrix.org for now",15,33}},
    };
    int ni = 0;
    for (int ri = 0; ri < 5; ri++) {
        Room r;
        r.name = rnames[ri];
        r.topic = rtopics[ri];
        r.unread = (ri < 3) ? 2 : 0;
        int msgCount = (ri == 0) ? 8 : (ri == 1) ? 5 : (ri == 3) ? 6 : 4;
        for (int mi = 0; mi < msgCount; mi++) {
            Message m;
            m.nick = msgs[ri][mi].n;
            m.text = msgs[ri][mi].t;
            m.hour = msgs[ri][mi].h;
            m.minute = msgs[ri][mi].m;
            /* Hash nickname to color index */
            if (m.nick) {
                unsigned h = 0;
                for (const char* p = m.nick; *p; p++) h = h*31 + *p;
                m.nickColorIdx = h % 16;
            } else {
                m.nickColorIdx = 0;
            }
            r.msgs.push_back(m);
        }
        G.rooms.push_back(r);
    }
}

/* ======== LAYOUT ======== */
static void layoutUI() {
    G.nBtns = 0;
    switch (G.screen) {
        case SCR_LOGIN: {
            float cx = G.w * 0.12f, cw = G.w * 0.76f, y = G.h * 0.08f;
            /* Title */
            /* Fields: labels are part of rendering, buttons handle interaction */
            float fh = 48.0f, gap = 12.0f;
            y += 80.0f;
            /* Server */
            y += fh + gap;
            /* Port */
            y += fh + gap;
            /* Nick */
            y += fh + gap;
            /* Password */
            y += fh + gap + 8.0f;
            /* TLS toggle */
            G.btns[G.nBtns++] = mkBtn(cx, y, 60.0f, 32.0f, nullptr, G.loginForm.tls ? Vec4{C_TOGGLE_ON} : Vec4{C_TOGGLE_OFF});
            y += 48.0f;
            /* Connect button */
            G.btns[G.nBtns++] = mkBtn(cx, y, cw, 52.0f, "Connect", Vec4{C_CYAN});
            break;
        }
        case SCR_CHAT: {
            /* Drawer toggle (hamburger) */
            G.btns[G.nBtns++] = mkBtn(8, 8, 44, 44, "#", Vec4{C_BTN_BG});
            /* Room items in drawer */
            float dy = 60.0f;
            for (size_t i = 0; i < G.rooms.size(); i++) {
                G.btns[G.nBtns++] = mkBtn(0, dy, G.drawerW, 44, G.rooms[i].name,
                    i == (size_t)G.activeRoom ? Vec4{C_ROOM_SEL} : Vec4{0,0,0,0});
                dy += 48.0f;
            }
            /* Send button (bottom bar) */
            G.btns[G.nBtns++] = mkBtn(G.w - 60.0f, G.h - 48.0f, 52, 40, "Send", Vec4{C_SEND});
            break;
        }
    }
}

/* ======== RENDER: LOGIN ======== */
static void renderLogin() {
    float cx = G.w * 0.12f, cw = G.w * 0.76f, y = G.h * 0.08f;

    text((G.w - measure("Progressive IRC", 36.0f))*0.5f, y, "Progressive IRC", 36.0f, Vec4{C_TITLE});
    y += 52.0f;
    text((G.w - measure("Connect to IRC Server", 18.0f))*0.5f, y,
        "Connect to IRC Server", 18.0f, Vec4{C_LABEL});

    float fh = 48.0f, gap = 12.0f;
    y = G.h * 0.22f;

    auto field = [&](const char* label, const char* val, const char* hint) {
        text(cx, y, label, 20.0f, Vec4{C_CYAN});
        rect(cx, y + 26.0f, cw, 2.0f, Vec4{0.25f,0.28f,0.32f,1.0f});
        text(cx + 4.0f, y + 18.0f, val, 18.0f, Vec4{C_LABEL});
        y += fh + gap;
    };

    field("Server:", "irc.libera.chat", "irc.libera.chat");
    field("Port:", "6667", "6667");
    field("Nickname:", "progressive_user", "");
    field("Password:", "********", "");

    /* TLS toggle */
    G.btns[G.nBtns-2].rect.y = y;
    G.btns[G.nBtns-2].rect.x = cx;
    text(cx, y, "TLS", 20.0f, Vec4{C_WHITE});
    text(cx + 70.0f, y + 4.0f, G.loginForm.tls ? "[ON]" : "[OFF]", 16.0f,
        G.loginForm.tls ? Vec4{C_TOGGLE_ON} : Vec4{C_LABEL});
    rect(cx, y, 60.0f, 32.0f, G.btns[G.nBtns-2].pressed ? Vec4{C_BTN_PR} : G.btns[G.nBtns-2].color);
    y += 48.0f;

    /* Connect button */
    G.btns[G.nBtns-1].rect.y = y;
    G.btns[G.nBtns-1].rect.x = cx;
    G.btns[G.nBtns-1].rect.w = cw;
    G.btns[G.nBtns-1].rect.h = 52.0f;
    btn(G.btns[G.nBtns-1], 22.0f);
}

/* ======== RENDER: CHAT ======== */
static void renderDrawer() {
    if (G.drawer == DRAWER_CLOSED && G.drawerX < 1.0f) return;

    float dx = -G.drawerW + G.drawerX;
    /* Drawer background */
    rect(dx, 0, G.drawerW, (float)G.h, Vec4{C_DRAWER});
    /* Header */
    text(dx + 12, 40, "Rooms", 22.0f, Vec4{C_TITLE});
    rect(dx, 54, G.drawerW, 1.0f, Vec4{0.2f,0.22f,0.28f,1.0f});

    /* Room list */
    float y = 64.0f;
    for (size_t i = 0; i < G.rooms.size(); i++) {
        bool sel = (int)i == G.activeRoom;
        if (sel) rect(dx, y, G.drawerW, 44.0f, Vec4{C_ROOM_SEL});
        text(dx + 12, y + 28, G.rooms[i].name, 18.0f, sel ? Vec4{C_WHITE} : Vec4{C_LABEL});
        if (G.rooms[i].unread > 0) {
            char ub[8]; snprintf(ub, 8, "%d", G.rooms[i].unread);
            float uw = measure(ub, 14.0f);
            rect(dx + G.drawerW - uw - 24.0f, y + 14, uw + 16.0f, 20.0f, Vec4{C_CYAN});
            text(dx + G.drawerW - uw - 16.0f, y + 28, ub, 14.0f, Vec4{C_WHITE});
        }
        y += 48.0f;
    }
}

static void renderChat() {
    if (G.rooms.empty()) return;

    Room& room = G.rooms[G.activeRoom];
    float y0 = 56.0f;

    /* Header bar */
    rect(0, 0, (float)G.w, y0, Vec4{C_DARKER});
    text(52, 36, room.name, 22.0f, Vec4{C_WHITE});
    if (room.topic) {
        float tw = measure(room.topic, 14.0f);
        float maxW = G.w - 60.0f;
        if (tw > maxW) {
            /* Truncate topic */
            char buf[128]; int len = strlen(room.topic);
            for (int i = 0; i < len && measure(buf, 14.0f) < maxW - 30.0f; i++)
                buf[i] = room.topic[i];
            text(52, 50, room.topic, 14.0f, Vec4{C_LABEL});
        } else {
            text(52, 50, room.topic, 14.0f, Vec4{C_LABEL});
        }
    }
    rect(0, y0, (float)G.w, 1.0f, Vec4{0.2f,0.22f,0.28f,1.0f});

    /* Message area: y0 to bottom bar */
    float msgTop = y0 + 4.0f;
    float msgBot = G.h - 52.0f;
    float msgArea = msgBot - msgTop;

    /* Clamp scroll */
    float lineH = 20.0f;
    float totalH = room.msgs.size() * lineH + 8.0f;
    G.maxScroll = totalH - msgArea;
    if (G.maxScroll < 0) G.maxScroll = 0;
    if (G.scrollY < 0) G.scrollY = 0;
    if (G.scrollY > G.maxScroll) G.scrollY = G.maxScroll;

    /* Scissor for message area */
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, (GLint)(G.h - msgBot), G.w, (GLsizei)msgArea);

    rect(0, msgTop, (float)G.w, msgArea, Vec4{C_BG});

    float my = msgTop + 8.0f - G.scrollY;
    for (auto& m : room.msgs) {
        char ts[16]; snprintf(ts, 16, "[%02d:%02d]", m.hour, m.minute);
        text(8, my + 15, ts, 14.0f, Vec4{C_TIMESTAMP}, 1.0f);
        float tx = 8 + measure(ts, 14.0f) + 6.0f;

        if (!m.nick) {
            /* System message */
            text(tx, my + 15, m.text, 14.0f, Vec4{0.45f,0.50f,0.35f,1.0f}, 1.0f);
        } else {
            /* Normal message */
            Vec4 nc = {kNicks[m.nickColorIdx][0], kNicks[m.nickColorIdx][1],
                       kNicks[m.nickColorIdx][2], 1.0f};
            text(tx, my + 15, m.nick, 15.0f, nc, 1.05f);
            tx += measure(m.nick, 15.0f) + 6.0f;
            text(tx, my + 15, m.text, 15.0f, Vec4{C_WHITE}, 1.05f);
        }
        my += lineH;
    }
    glDisable(GL_SCISSOR_TEST);

    /* Bottom input bar */
    rect(0, msgBot, (float)G.w, 52.0f, Vec4{C_INPUT_BG});
    rect(0, msgBot, (float)G.w, 1.0f, Vec4{0.2f,0.22f,0.28f,1.0f});
    rect(8, msgBot + 8, G.w - 80.0f, 36.0f, Vec4{C_DARKER});
    text(16, msgBot + 30, "Type a message...", 16.0f, Vec4{C_LABEL});

    /* Send button (the last button) */
    if (G.nBtns > 0) btn(G.btns[G.nBtns - 1], 20.0f);

    /* Drawer overlay */
    if (G.drawer != DRAWER_CLOSED) {
        float alpha = G.drawerX / G.drawerW * 0.5f;
        rect(0, 0, (float)G.w, (float)G.h, Vec4{0,0,0,alpha});
        renderDrawer();
    }
}

/* ======== FRAME ======== */
static void renderFrame() {
    glClearColor(C_BG); glClear(GL_COLOR_BUFFER_BIT);
    switch (G.screen) {
        case SCR_LOGIN: renderLogin(); break;
        case SCR_CHAT:  renderChat();  break;
    }
}

/* ======== TOUCH HANDLING ======== */
static void doDown(float x, float y) {
    G.tx = x; G.ty = y; G.touching = true;
    /* Check drawer area first */
    if (G.screen == SCR_CHAT && G.drawer == DRAWER_OPEN && x < G.drawerW) {
        int hit = hitBtn(x, y);
        if (hit == 0) { G.drawer = DRAWER_ANIM; } /* hamburger */
        else if (hit > 0 && hit <= (int)G.rooms.size()) {
            G.activeRoom = hit - 1;
            G.scrollY = 0;
            G.drawer = DRAWER_ANIM;
            layoutUI();
        }
        return;
    }
    /* Check buttons */
    int hit = hitBtn(x, y);
    if (hit >= 0) {
        G.btns[hit].pressed = true;
        G.activeBtn = hit;
        return;
    }
    /* Start scroll tracking */
    if (G.screen == SCR_CHAT && x > G.drawerW) {
        G.scrollTrackId = 1;
        G.scrollLastY = y;
        G.scrollVel = 0;
    }
}

static void doUp(float x, float y) {
    G.touching = false;
    G.scrollTrackId = 0;
    for (int i = 0; i < G.nBtns; i++) {
        if (G.btns[i].pressed) {
            G.btns[i].pressed = false;
            if (hitRect(x, y, G.btns[i].rect)) {
                /* Handle button clicks */
                if (G.screen == SCR_LOGIN) {
                    if (i == G.nBtns - 2) { /* TLS toggle */
                        G.loginForm.tls = !G.loginForm.tls;
                        G.btns[i].color = G.loginForm.tls ? Vec4{C_TOGGLE_ON} : Vec4{C_TOGGLE_OFF};
                    } else if (i == G.nBtns - 1) { /* Connect */
                        LOGI("Connect pressed -> chat");
                        G.screen = SCR_CHAT;
                        G.drawer = DRAWER_CLOSED;
                        G.drawerX = 0;
                        G.scrollY = 0;
                        G.scrollVel = 0;
                        layoutUI();
                    }
                } else if (G.screen == SCR_CHAT) {
                    if (i == 0) { /* Hamburger */
                        G.drawer = (G.drawer == DRAWER_CLOSED) ? DRAWER_OPEN : DRAWER_CLOSED;
                        G.drawerX = (G.drawer == DRAWER_OPEN) ? G.drawerW : 0;
                    } else if (i == G.nBtns - 1) { /* Send */
                        LOGI("Send pressed");
                    }
                }
            }
        }
    }
    G.activeBtn = -1;
}

static void doMove(float x, float y) {
    G.tx = x; G.ty = y;
    if (G.scrollTrackId == 1) {
        float dy = y - G.scrollLastY;
        G.scrollY += dy;
        G.scrollVel = dy;
        G.scrollLastY = y;
    }
    for (int i = 0; i < G.nBtns; i++) {
        if (i == G.activeBtn) G.btns[i].pressed = hitRect(x, y, G.btns[i].rect);
    }
}

static void doFling(float vx, float vy) {
    G.scrollVel = vy;
}

/* ======== INIT ======== */
static bool initGL(JNIEnv* env, jobject am) {
    LOGI("initGL %dx%d", G.w, G.h);
    G.amgr = AAssetManager_fromJava(env, am);
    if (!G.amgr) return false;

    G.prog = mkProg(kVS, kFS);
    if (!G.prog) return false;
    G.uMVP = glGetUniformLocation(G.prog, "uMVP");
    G.uTex = glGetUniformLocation(G.prog, "uTex");
    G.uColor = glGetUniformLocation(G.prog, "uColor");
    G.uSmooth = glGetUniformLocation(G.prog, "uSmooth");
    G.uIsTex = glGetUniformLocation(G.prog, "uIsTex");

    AAsset* a = AAssetManager_open(G.amgr, "noto_sans_sdf.png", AASSET_MODE_BUFFER);
    if (!a) return false;
    const void* d = AAsset_getBuffer(a);
    off_t len = AAsset_getLength(a);
    stbi_set_flip_vertically_on_load(1);
    int tw, th, ch;
    unsigned char* px = stbi_load_from_memory((const stbi_uc*)d, len, &tw, &th, &ch, 1);
    AAsset_close(a);
    if (!px) return false;
    glGenTextures(1, &G.tex);
    glBindTexture(GL_TEXTURE_2D, G.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, tw, th, 0, GL_RED, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);

    glGenVertexArrays(1, &G.vaoG); glBindVertexArray(G.vaoG);
    glGenBuffers(1, &G.vboG); glBindBuffer(GL_ARRAY_BUFFER, G.vboG);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(GlyphVertex),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(GlyphVertex),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glGenVertexArrays(1, &G.vaoR); glBindVertexArray(G.vaoR);
    glGenBuffers(1, &G.vboR); glBindBuffer(GL_ARRAY_BUFFER, G.vboR);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, G.w, G.h);
    G.drawerW = G.w * 0.75f;
    if (G.drawerW > 320.0f) G.drawerW = 320.0f; /* max drawer width */

    genMockData();
    G.screen = SCR_LOGIN;
    G.drawer = DRAWER_CLOSED;
    layoutUI();
    G.init = true;
    LOGI("initGL done");
    return true;
}

/* ======== JNI ======== */
extern "C" {

JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeInit(
    JNIEnv* e, jclass, jint w, jint h, jobject am) {
    G.w = w; G.h = h; if (!G.init) initGL(e, am);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeResize(JNIEnv*, jclass, jint w, jint h) {
    G.w = w; G.h = h;
    G.drawerW = w * 0.75f; if (G.drawerW > 320.0f) G.drawerW = 320.0f;
    if (G.init) { glViewport(0,0,w,h); layoutUI(); }
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeRender(JNIEnv*, jclass) {
    if (G.init) {
        /* Apply fling decay */
        if (!G.touching && fabsf(G.scrollVel) > 0.5f) {
            G.scrollY += G.scrollVel;
            G.scrollVel *= 0.92f;
        }
        renderFrame();
    }
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchDown(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.init) doDown(x, y);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchUp(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.init) doUp(x, y);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchMove(JNIEnv*, jclass, jfloat x, jfloat y) {
    if (G.init) doMove(x, y);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeFling(JNIEnv*, jclass, jfloat vx, jfloat vy) {
    if (G.init) doFling(vx, vy);
}

}
