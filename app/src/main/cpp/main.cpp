/*
 * Progressive Android Next — Pure C++/OpenGL ES Matrix/IRC Chat
 * Matches progressive-android-irc UI. Zero JVM UI code.
 * To be merged into progressive-android 0.5.5+
 *
 * Reference: activity_irc_auth.xml + IrcAuthActivity.kt
 *   Title:  "Connect to IRC"
 *   Fields: Server, Port, Nickname, Password
 *   Toggle: Use TLS/SSL (switch)
 *   Button: Connect (full-width)
 *   Status: connection status text
 *
 * Chat: IRC-style [HH:MM] <nick> message, drawer sidebar
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

#define LOG_TAG "PNext"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "glyph_data.hpp"

/* ======== SHADERS ======== */
static const char* kVS=R"glsl(#version 300 es
precision mediump float;
in vec2 aPos;in vec2 aTexCoord;
uniform mat4 uMVP;
out vec2 vTexCoord;
void main(){gl_Position=uMVP*vec4(aPos,0,1);vTexCoord=aTexCoord;}
)glsl";
static const char* kFS=R"glsl(#version 300 es
precision mediump float;
in vec2 vTexCoord;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform float uSmooth;
uniform bool uIsTex;
out vec4 fragColor;
void main(){
if(uIsTex){float d=texture(uTex,vTexCoord).r;float a=smoothstep(0.5-uSmooth,0.5+uSmooth,d);fragColor=vec4(uColor.rgb,uColor.a*a);}
else{fragColor=uColor;}
}
)glsl";

/* ======== COLORS ======== */
#define C_BG       0.08f,0.08f,0.12f,1.0f
#define C_DARKER   0.05f,0.05f,0.08f,1.0f
#define C_TITLE    0.36f,0.77f,0.90f,1.0f
#define C_LABEL    0.60f,0.60f,0.68f,1.0f
#define C_HINT     0.40f,0.40f,0.48f,1.0f
#define C_WHITE    0.90f,0.90f,0.92f,1.0f
#define C_FIELD_BG 0.12f,0.12f,0.17f,1.0f
#define C_BTN_BG   0.15f,0.18f,0.22f,1.0f
#define C_BTN_PR   0.22f,0.26f,0.32f,1.0f
#define C_GREEN    0.20f,0.60f,0.35f,1.0f
#define C_CYAN     0.36f,0.77f,0.90f,1.0f
#define C_PURPLE   0.55f,0.40f,0.75f,1.0f
#define C_BACK     0.35f,0.35f,0.40f,1.0f
#define C_DRAWER   0.10f,0.10f,0.15f,1.0f
#define C_SEL      0.15f,0.18f,0.25f,1.0f
#define C_TS       0.36f,0.36f,0.42f,1.0f
#define C_INPUT    0.12f,0.12f,0.17f,1.0f
#define C_SYSMSG   0.45f,0.50f,0.35f,1.0f
#define C_ON       0.25f,0.70f,0.40f,1.0f
#define C_OFF      0.30f,0.30f,0.35f,1.0f
#define C_KNOB     0.90f,0.90f,0.92f,1.0f

static const float kNicks[16][3]={
{0.90f,0.30f,0.30f},{0.30f,0.80f,0.30f},{0.36f,0.77f,0.90f},
{0.90f,0.70f,0.20f},{0.85f,0.40f,0.85f},{0.30f,0.70f,0.30f},
{0.30f,0.40f,0.90f},{0.90f,0.50f,0.20f},{0.55f,0.40f,0.75f},
{0.20f,0.70f,0.50f},{0.40f,0.50f,0.85f},{0.80f,0.60f,0.20f},
{0.70f,0.30f,0.70f},{0.20f,0.65f,0.45f},{0.35f,0.55f,0.85f},{0.75f,0.45f,0.25f}};

/* ======== TYPES ======== */
struct Vec4{float r,g,b,a;};
struct Rect{float x,y,w,h;};
struct Button{Rect rect;const char* text;Vec4 color;bool pressed;};
struct GlyphVertex{float px,py,tx,ty;};

struct Message{
    const char* nick; /* nullptr=system */
    const char* text; int hour,minute; int nickColorIdx;
};
struct Room{
    const char* name,*topic;
    std::vector<Message> msgs; int unread;
};
enum Screen{SCR_LOGIN,SCR_CHAT};
enum DrawerState{DS_CLOSED,DS_OPEN};

/* ======== GLOBAL STATE ======== */
static struct{
    bool init; int w,h;
    GLuint prog,tex,vboG,vboR,vaoG,vaoR;
    GLint uMVP,uTex,uColor,uSmooth,uIsTex;

    Screen screen;
    struct{bool tls;const char* status;} login;

    int activeRoom; float scrollY,scrollVel,maxScroll;
    int scrollId; float scrollLast;

    DrawerState drawer; float drawerX,drawerW;

    Button btns[12]; int nBtns,activeBtn;

    float tx,ty; bool touching;

    std::vector<Room> rooms;
    AAssetManager* amgr;
}G;

/* ======== GL HELPERS ======== */
static GLuint mkS(GLenum t,const char*s){
    GLuint sh=glCreateShader(t);glShaderSource(sh,1,&s,nullptr);glCompileShader(sh);
    GLint ok;glGetShaderiv(sh,GL_COMPILE_STATUS,&ok);
    if(!ok){char l[512];glGetShaderInfoLog(sh,512,nullptr,l);LOGE("sh:%s",l);return 0;}
    return sh;
}
static GLuint mkP(const char*vs,const char*fs){
    GLuint v=mkS(GL_VERTEX_SHADER,vs),f=mkS(GL_FRAGMENT_SHADER,fs);
    if(!v||!f)return 0;
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    glDeleteShader(v);glDeleteShader(f);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char l[512];glGetProgramInfoLog(p,512,nullptr,l);LOGE("ln:%s",l);return 0;}
    return p;
}
static void ortho(float*m,float l,float r,float b,float t,float n,float f){
    memset(m,0,64);m[0]=2/(r-l);m[5]=2/(t-b);m[10]=-2/(f-n);
    m[12]=-(r+l)/(r-l);m[13]=-(t+b)/(t-b);m[14]=-(f+n)/(f-n);m[15]=1;
}

/* ======== TEXT RENDERING ======== */
static float msr(const char*t,float s){
    float x=0;
    for(const char*p=t;*p;p++){unsigned char c=*p;if(c<32||c>126)c=32;x+=kGlyphs[c-32].advance*s*1.15f;}
    return x;
}
static void txt(float x,float y,const char*t,float s,Vec4 c,float sp=1.15f){
    if(!t||!*t)return;
    int n=strlen(t);
    std::vector<GlyphVertex>v(n*4);std::vector<GLushort>idx(n*6);
    float cx=x;
    for(int i=0;i<n;i++){
        unsigned char ch=t[i];if(ch<32||ch>126)ch=32;
        const SDFGlyph&g=kGlyphs[ch-32];
        float gx=cx,gy=y-g.bearingY*s,gw=g.width*s,gh=g.height*s;
        float tx=g.texX,ty=g.texY,tw=g.texW,th=g.texH;
        int vi=i*4;float spd=SDF_SPREAD_METRICS*s;
        v[vi+0]={gx-spd,gy-spd,tx,ty+th};
        v[vi+1]={gx+gw+spd,gy-spd,tx+tw,ty+th};
        v[vi+2]={gx+gw+spd,gy+gh+spd,tx+tw,ty};
        v[vi+3]={gx-spd,gy+gh+spd,tx,ty};
        int ii=i*6,b=vi;
        idx[ii+0]=b;idx[ii+1]=b+1;idx[ii+2]=b+2;
        idx[ii+3]=b;idx[ii+4]=b+2;idx[ii+5]=b+3;
        cx+=g.advance*s*sp;
    }
    glUseProgram(G.prog);glUniform1i(G.uIsTex,1);
    glUniform4f(G.uColor,c.r,c.g,c.b,c.a);glUniform1f(G.uSmooth,0.08f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,G.tex);glUniform1i(G.uTex,0);
    float mvp[16];ortho(mvp,0,(float)G.w,(float)G.h,0,-1,1);
    glUniformMatrix4fv(G.uMVP,1,GL_FALSE,mvp);
    glBindBuffer(GL_ARRAY_BUFFER,G.vboG);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(GlyphVertex),v.data(),GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoG);
    GLuint ibo;glGenBuffers(1,&ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLushort),idx.data(),GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,idx.size(),GL_UNSIGNED_SHORT,nullptr);
    glDeleteBuffers(1,&ibo);glBindVertexArray(0);
}

/* ======== RECT ======== */
static void rct(float x,float y,float w,float h,Vec4 c){
    float v[]={x,y,0,0,x+w,y,1,0,x+w,y+h,1,1,x,y+h,0,1};
    GLushort idx[]={0,1,2,0,2,3};
    glUseProgram(G.prog);glUniform1i(G.uIsTex,0);
    glUniform4f(G.uColor,c.r,c.g,c.b,c.a);
    float mvp[16];ortho(mvp,0,(float)G.w,(float)G.h,0,-1,1);
    glUniformMatrix4fv(G.uMVP,1,GL_FALSE,mvp);
    glBindBuffer(GL_ARRAY_BUFFER,G.vboR);
    glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoR);
    GLuint ibo;glGenBuffers(1,&ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,nullptr);
    glDeleteBuffers(1,&ibo);glBindVertexArray(0);
}

static void btn(const Button&b,float ts){
    rct(b.rect.x,b.rect.y,b.rect.w,b.rect.h,b.pressed?Vec4{C_BTN_PR}:b.color);
    if(b.text){float tw=msr(b.text,ts);
        txt(b.rect.x+(b.rect.w-tw)*0.5f,b.rect.y+b.rect.h*0.5f+ts*0.35f,b.text,ts,Vec4{C_WHITE});}
}

static Button mkB(float x,float y,float w,float h,const char*t,Vec4 c){
    return{{x,y,w,h},t,c,false};
}

/* ======== TOUCH ======== */
static bool hit(float px,float py,const Rect&r){return px>=r.x&&px<=r.x+r.w&&py>=r.y&&py<=r.y+r.h;}
static int hitB(float x,float y){
    for(int i=0;i<G.nBtns;i++)if(hit(x,y,G.btns[i].rect))return i;
    return -1;
}

/* ======== TOGGLE SWITCH ======== */
static void toggle(float x,float y,bool on){
    float tw=52.0f,th=28.0f,kr=12.0f;
    /* Track - rounded pill */
    rct(x,y,tw,th,on?Vec4{C_ON}:Vec4{C_OFF});
    /* Knob */
    float kx=on?x+tw-kr*2-2:x+2;
    rct(kx+1,y+2,kr*2-2,th-4,Vec4{C_KNOB});
}

/* ======== MOCK DATA ======== */
static void genData(){
    G.rooms.clear();
    const char* rn[]={"#welcome","#general","#random","#dev","#matrix"};
    const char* rt[]={
        "Welcome to Progressive IRC | Please read /topic",
        "General discussion about anything and everything",
        "Random off-topic chat | Keep it civil",
        "Development discussion | C++ / OpenGL / Matrix",
        "Matrix protocol discussion and bridge testing"};
    struct{const char*n,*t;int h,m;}ms[][8]={
        {{nullptr,"--> alice (~alice@matrix) joined #welcome",9,5},
         {"alice","hello everyone!",9,6},
         {nullptr,"--> bob (~bob@matrix) joined #welcome",9,7},
         {"bob","hey alice, how's it going?",9,7},
         {"alice","pretty good! working on the new renderer",9,8},
         {nullptr,"--> charlie (~charlie@irc) joined #welcome",9,9},
         {"charlie","what renderer are you using?",9,10},
         {"alice","pure OpenGL ES with SDF fonts. no JVM UI!",9,12}},
        {{nullptr,"--> dave joined #general",10,14},
         {"dave","anyone here?",10,15},
         {"eve","yep, just lurking",10,16},
         {nullptr,"<-- frank left #general",10,18}},
        {{"frank","lol check this out https://example.com",11,20},
         {"grace","haha that's amazing",11,21},
         {"heidi","i don't get it",11,22},
         {"frank","you had to be there",11,23}},
        {{"ivan","pushed a new commit to the renderer",14,0},
         {nullptr,"--> judy joined #dev",14,1},
         {"judy","nice! SDF fonts looking crisp?",14,2},
         {"ivan","yeah, smoothstep AA is great now",14,3},
         {"karen","can we use this for main app?",14,5},
         {"ivan","yes, merging into 0.5.5+",14,6}},
        {{"larry","testing the matrix bridge",15,30},
         {"mike","seems to be working",15,31},
         {"nancy","which homeserver?",15,32},
         {"larry","matrix.org for now",15,33}}};
    int mc[]={8,4,4,6,4};
    for(int ri=0;ri<5;ri++){
        Room r;r.name=rn[ri];r.topic=rt[ri];r.unread=ri<3?2:0;
        for(int mi=0;mi<mc[ri];mi++){
            Message m;m.nick=ms[ri][mi].n;m.text=ms[ri][mi].t;
            m.hour=ms[ri][mi].h;m.minute=ms[ri][mi].m;
            if(m.nick){unsigned h=0;for(const char*p=m.nick;*p;p++)h=h*31+*p;m.nickColorIdx=h%16;}
            else m.nickColorIdx=0;
            r.msgs.push_back(m);
        }
        G.rooms.push_back(r);
    }
}

/* ======== LAYOUT ======== */
static void layoutUI(){
    G.nBtns=0;
    switch(G.screen){
        case SCR_LOGIN:{
            float cx=G.w*0.12f,cw=G.w*0.76f;
            /* Buttons placed by renderLogin */
            G.nBtns=2; /* TLS toggle, Connect */
            break;}
        case SCR_CHAT:{
            G.btns[G.nBtns++]=mkB(8,8,44,44,"#",Vec4{C_BTN_BG});
            float dy=60.0f;
            for(size_t i=0;i<G.rooms.size();i++){
                G.btns[G.nBtns++]=mkB(0,dy,G.drawerW,44,G.rooms[i].name,
                    i==(size_t)G.activeRoom?Vec4{C_SEL}:Vec4{0,0,0,0});
                dy+=48.0f;}
            G.btns[G.nBtns++]=mkB(G.w-60.0f,G.h-48.0f,52,40,"Send",Vec4{C_CYAN});
            break;}
    }
}

/* ======== RENDER LOGIN ======== */
static void renderLogin(){
    float pad=G.w*0.12f,fw=G.w*0.76f;
    float y=G.h*0.10f;

    txt((G.w-msr("Connect to IRC",32.0f))*0.5f,y,"Connect to IRC",32.0f,Vec4{C_TITLE});
    y+=56.0f;

    /* Fields: label + underline + hint/value */
    auto field=[&](const char*label,const char*val,const char*hint){
        txt(pad,y,label,16.0f,Vec4{C_CYAN},1.1f);
        float uy=y+22.0f;
        rct(pad,uy,fw,1.5f,Vec4{0.25f,0.28f,0.33f,1.0f});
        if(val&&*val)txt(pad+4.0f,y+16.0f,val,16.0f,Vec4{C_WHITE},1.05f);
        else txt(pad+4.0f,y+16.0f,hint,16.0f,Vec4{C_HINT},1.05f);
        y+=50.0f;
    };

    field("Server hostname (e.g. irc.libera.chat)","irc.libera.chat","irc.libera.chat");
    y+=4.0f;
    field("Port","6667","6667");
    y+=4.0f;
    field("Nickname","progressive_user","");
    y+=4.0f;
    field("Server password (optional)","","********");

    y+=8.0f;
    /* TLS toggle */
    txt(pad,y,"Use TLS/SSL",18.0f,Vec4{C_WHITE});
    G.btns[0].rect={pad+fw-60.0f,y-2.0f,60.0f,32.0f};
    G.btns[0].color = G.login.tls ? Vec4{C_ON} : Vec4{C_OFF};
    toggle(pad+fw-56.0f,y+1.0f,G.login.tls);
    y+=44.0f;

    /* Status text */
    if(G.login.status){
        txt(pad,y,G.login.status,14.0f,Vec4{C_LABEL});y+=24.0f;
    }

    /* Spacer to push Connect button toward center */
    y = G.h * 0.55f;
    float btnH = 48.0f;

    /* Connect button */
    G.btns[1].rect={pad,y,fw,btnH};
    G.btns[1].text = "Connect";
    G.btns[1].color = Vec4{C_CYAN};
    btn(G.btns[1],20.0f);
}

/* ======== RENDER CHAT ======== */
static void renderDrawer(){
    if(G.drawer==DS_CLOSED&&G.drawerX<1.0f)return;
    float dx=-G.drawerW+G.drawerX;
    rct(dx,0,G.drawerW,(float)G.h,Vec4{C_DRAWER});
    txt(dx+12,40,"Rooms",20.0f,Vec4{C_TITLE});
    rct(dx,54,G.drawerW,1.0f,Vec4{0.2f,0.22f,0.28f,1.0f});
    float y=64.0f;
    for(size_t i=0;i<G.rooms.size();i++){
        bool sel=(int)i==G.activeRoom;
        if(sel)rct(dx,y,G.drawerW,44.0f,Vec4{C_SEL});
        txt(dx+12,y+28,G.rooms[i].name,18.0f,sel?Vec4{C_WHITE}:Vec4{C_LABEL});
        if(G.rooms[i].unread>0){
            char ub[8];snprintf(ub,8,"%d",G.rooms[i].unread);
            float uw=msr(ub,14.0f);
            rct(dx+G.drawerW-uw-24.0f,y+14,uw+16.0f,20.0f,Vec4{C_CYAN});
            txt(dx+G.drawerW-uw-16.0f,y+28,ub,14.0f,Vec4{C_WHITE});
        }
        y+=48.0f;
    }
}

static void renderChat(){
    if(G.rooms.empty())return;
    Room&r=G.rooms[G.activeRoom];
    float hdrH=56.0f;

    rct(0,0,(float)G.w,hdrH,Vec4{C_DARKER});
    txt(52,36,r.name,22.0f,Vec4{C_WHITE});
    if(r.topic)txt(52,50,r.topic,14.0f,Vec4{C_LABEL});
    rct(0,hdrH,(float)G.w,1.0f,Vec4{0.2f,0.22f,0.28f,1.0f});

    float msgTop=hdrH+4.0f,msgBot=G.h-52.0f,area=msgBot-msgTop;
    float lineH=20.0f,totalH=r.msgs.size()*lineH+8.0f;
    G.maxScroll=totalH-area;if(G.maxScroll<0)G.maxScroll=0;
    if(G.scrollY<0)G.scrollY=0;if(G.scrollY>G.maxScroll)G.scrollY=G.maxScroll;

    glEnable(GL_SCISSOR_TEST);
    glScissor(0,(GLint)(G.h-msgBot),G.w,(GLsizei)area);
    rct(0,msgTop,(float)G.w,area,Vec4{C_BG});

    float my=msgTop+8.0f-G.scrollY;
    for(auto&m:r.msgs){
        char ts[16];snprintf(ts,16,"[%02d:%02d]",m.hour,m.minute);
        txt(8,my+15,ts,14.0f,Vec4{C_TS},1.0f);
        float tx=8+msr(ts,14.0f)+6.0f;
        if(!m.nick){
            txt(tx,my+15,m.text,14.0f,Vec4{C_SYSMSG},1.0f);
        }else{
            Vec4 nc={kNicks[m.nickColorIdx][0],kNicks[m.nickColorIdx][1],kNicks[m.nickColorIdx][2],1.0f};
            txt(tx,my+15,m.nick,15.0f,nc,1.05f);
            tx+=msr(m.nick,15.0f)+6.0f;
            txt(tx,my+15,m.text,15.0f,Vec4{C_WHITE},1.05f);
        }
        my+=lineH;
    }
    glDisable(GL_SCISSOR_TEST);

    /* Bottom bar */
    rct(0,msgBot,(float)G.w,52.0f,Vec4{C_INPUT});
    rct(0,msgBot,(float)G.w,1.0f,Vec4{0.2f,0.22f,0.28f,1.0f});
    rct(8,msgBot+8,G.w-80.0f,36.0f,Vec4{C_DARKER});
    txt(16,msgBot+30,"Type a message...",16.0f,Vec4{C_HINT});
    if(G.nBtns>0)btn(G.btns[G.nBtns-1],20.0f);

    /* Drawer overlay */
    if(G.drawer!=DS_CLOSED){
        float a=G.drawerX/G.drawerW*0.5f;
        rct(0,0,(float)G.w,(float)G.h,Vec4{0,0,0,a});
        renderDrawer();
    }
}

/* ======== FRAME ======== */
static void frame(){
    glClearColor(C_BG);glClear(GL_COLOR_BUFFER_BIT);
    switch(G.screen){case SCR_LOGIN:renderLogin();break;case SCR_CHAT:renderChat();break;}
}

/* ======== TOUCH ======== */
static void td(float x,float y){
    G.tx=x;G.ty=y;G.touching=true;
    if(G.screen==SCR_CHAT&&G.drawer==DS_OPEN&&x<G.drawerW){
        int h=hitB(x,y);
        if(h==0){G.drawer=DS_CLOSED;G.drawerX=0;}
        else if(h>0&&h<=(int)G.rooms.size()){G.activeRoom=h-1;G.scrollY=0;G.drawer=DS_CLOSED;G.drawerX=0;layoutUI();}
        return;
    }
    int h=hitB(x,y);
    if(h>=0){G.btns[h].pressed=true;G.activeBtn=h;return;}
    if(G.screen==SCR_CHAT&&x>G.drawerW){G.scrollId=1;G.scrollLast=y;G.scrollVel=0;}
}
static void tu(float x,float y){
    G.touching=false;G.scrollId=0;
    for(int i=0;i<G.nBtns;i++){
        if(G.btns[i].pressed){G.btns[i].pressed=false;
            if(hit(x,y,G.btns[i].rect)){
                if(G.screen==SCR_LOGIN){
                    if(i==0){G.login.tls=!G.login.tls;G.btns[0].color=G.login.tls?Vec4{C_ON}:Vec4{C_OFF};}
                    else if(i==1){LOGI("Connect");G.screen=SCR_CHAT;G.drawer=DS_CLOSED;G.drawerX=0;G.scrollY=0;layoutUI();}
                }else if(G.screen==SCR_CHAT){
                    if(i==0){G.drawer=G.drawer==DS_CLOSED?DS_OPEN:DS_CLOSED;G.drawerX=G.drawer==DS_OPEN?G.drawerW:0;}
                }
            }
        }
    }
    G.activeBtn=-1;
}
static void tm(float x,float y){
    G.tx=x;G.ty=y;
    if(G.scrollId==1){G.scrollY+=y-G.scrollLast;G.scrollVel=y-G.scrollLast;G.scrollLast=y;}
    for(int i=0;i<G.nBtns;i++)if(i==G.activeBtn)G.btns[i].pressed=hit(x,y,G.btns[i].rect);
}

/* ======== INIT ======== */
static bool initGL(JNIEnv*env,jobject am){
    LOGI("init %dx%d",G.w,G.h);
    G.amgr=AAssetManager_fromJava(env,am);if(!G.amgr)return false;
    G.prog=mkP(kVS,kFS);if(!G.prog)return false;
    G.uMVP=glGetUniformLocation(G.prog,"uMVP");
    G.uTex=glGetUniformLocation(G.prog,"uTex");
    G.uColor=glGetUniformLocation(G.prog,"uColor");
    G.uSmooth=glGetUniformLocation(G.prog,"uSmooth");
    G.uIsTex=glGetUniformLocation(G.prog,"uIsTex");

    AAsset*a=AAssetManager_open(G.amgr,"noto_sans_sdf.png",AASSET_MODE_BUFFER);
    if(!a)return false;
    const void*d=AAsset_getBuffer(a);off_t len=AAsset_getLength(a);
    stbi_set_flip_vertically_on_load(1);
    int tw,th,ch;unsigned char*px=stbi_load_from_memory((const stbi_uc*)d,len,&tw,&th,&ch,1);
    AAsset_close(a);if(!px)return false;
    glGenTextures(1,&G.tex);glBindTexture(GL_TEXTURE_2D,G.tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8,tw,th,0,GL_RED,GL_UNSIGNED_BYTE,px);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    stbi_image_free(px);

    glGenVertexArrays(1,&G.vaoG);glBindVertexArray(G.vaoG);
    glGenBuffers(1,&G.vboG);glBindBuffer(GL_ARRAY_BUFFER,G.vboG);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(GlyphVertex),(void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(GlyphVertex),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glGenVertexArrays(1,&G.vaoR);glBindVertexArray(G.vaoR);
    glGenBuffers(1,&G.vboR);glBindBuffer(GL_ARRAY_BUFFER,G.vboR);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0,0,G.w,G.h);
    G.drawerW=G.w*0.75f;if(G.drawerW>320.0f)G.drawerW=320.0f;
    genData();G.screen=SCR_LOGIN;G.drawer=DS_CLOSED;layoutUI();G.init=true;
    LOGI("init ok");return true;
}

/* ======== JNI ======== */
extern "C"{
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeInit(JNIEnv*e,jclass,jint w,jint h,jobject am){
    G.w=w;G.h=h;if(!G.init)initGL(e,am);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeResize(JNIEnv*,jclass,jint w,jint h){
    G.w=w;G.h=h;G.drawerW=w*0.75f;if(G.drawerW>320.0f)G.drawerW=320.0f;
    if(G.init){glViewport(0,0,w,h);layoutUI();}
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeRender(JNIEnv*,jclass){
    if(G.init){
        if(!G.touching&&fabsf(G.scrollVel)>0.5f){G.scrollY+=G.scrollVel;G.scrollVel*=0.92f;}
        frame();
    }
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchDown(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)td(x,y);}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchUp(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)tu(x,y);}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchMove(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)tm(x,y);}
}
