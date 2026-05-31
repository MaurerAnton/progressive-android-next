/*
 * Progressive Android Next — Pure C++/OpenGL ES IRC Chat
 * Zero JVM UI. Merging into progressive-android 0.5.5+
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
uniform bool uIsRGBA;
out vec4 fragColor;
void main(){
if(!uIsTex){fragColor=uColor;return;}
if(uIsRGBA){fragColor=texture(uTex,vTexCoord)*uColor;return;}
float d=texture(uTex,vTexCoord).r;
float a=smoothstep(0.5-uSmooth,0.5+uSmooth,d);
fragColor=vec4(uColor.rgb,uColor.a*a);
}
)glsl";

#define C_BG      0.07f,0.07f,0.11f,1.0f
#define C_DARK    0.04f,0.04f,0.07f,1.0f
#define C_TITLE   0.36f,0.77f,0.90f,1.0f
#define C_LABEL   0.55f,0.55f,0.63f,1.0f
#define C_HINT    0.35f,0.35f,0.42f,1.0f
#define C_WHITE   0.88f,0.88f,0.90f,1.0f
#define C_CYAN    0.36f,0.77f,0.90f,1.0f
#define C_GREEN   0.20f,0.62f,0.35f,1.0f
#define C_PURPLE  0.55f,0.40f,0.75f,1.0f
#define C_DRAWER  0.09f,0.09f,0.14f,1.0f
#define C_SEL     0.14f,0.17f,0.24f,1.0f
#define C_TS      0.32f,0.32f,0.38f,1.0f
#define C_INPUT   0.11f,0.11f,0.16f,1.0f
#define C_SYSMSG  0.42f,0.48f,0.32f,1.0f
#define C_TOGGLE_TRACK_ON  0.20f,0.62f,0.35f,1.0f
#define C_TOGGLE_TRACK_OFF 0.22f,0.22f,0.28f,1.0f
#define C_TOGGLE_KNOB 0.95f,0.95f,0.96f,1.0f
#define C_BTN_HOVER 0.30f,0.68f,0.82f,1.0f
#define C_BTN_PRESS 0.18f,0.52f,0.65f,1.0f
#define C_DIVIDER  0.16f,0.18f,0.22f,1.0f

static const float kNicks[16][3]={
{0.95f,0.35f,0.35f},{0.35f,0.85f,0.35f},{0.36f,0.77f,0.90f},
{0.95f,0.75f,0.25f},{0.90f,0.45f,0.90f},{0.35f,0.75f,0.35f},
{0.35f,0.45f,0.95f},{0.95f,0.55f,0.25f},{0.60f,0.45f,0.80f},
{0.25f,0.75f,0.55f},{0.45f,0.55f,0.90f},{0.85f,0.65f,0.25f},
{0.75f,0.35f,0.75f},{0.25f,0.70f,0.50f},{0.40f,0.60f,0.90f},
{0.80f,0.50f,0.30f}};

struct Vec4{float r,g,b,a;};
struct Rect{float x,y,w,h;};
struct Button{Rect rect;const char* text;Vec4 color;bool pressed;};
struct GlyphVertex{float px,py,tx,ty;};
struct Message{const char*nick,*text;int h,m;int ci;};
struct Room{const char*name,*topic;std::vector<Message>msgs;int unread;};
enum Screen{SCR_SERVER,SCR_MATRIX,SCR_IRC,SCR_SIGNUP,SCR_PROFILE,SCR_SETTINGS,SCR_CHAT};
enum DrawerState{DS_CLOSED,DS_OPEN};

static struct{
    bool init;int w,h;float dp; /* density scale: 1sp = dp px */
    int logoW=0,logoH=0;
    GLuint prog,tex,vboG,vboR,vaoG,vaoR,texLogo,texCar[4];
    GLint uMVP,uTex,uColor,uSmooth,uIsTex,uIsRGBA;
    Screen screen;
    struct{bool tls;int cat;int carouselPage;int frameCount;int focusField;char hsUrl[64];char user[64];char pass[64];int hsLen,userLen,passLen;char profileNick[32];int profileNickLen;}login;
    int activeRoom;float sy,sv,ms;
    int sid;float sl;
    DrawerState ds;float dx,dw;
    Button btns[12];int nb,ab;
    float tx,ty;bool touching;
    std::vector<Room> rooms;
    AAssetManager* amgr;
}G;

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

static float msr(const char*t,float s){
    float x=0;for(const char*p=t;*p;p++){unsigned char c=*p;if(c<32||c>126)c=32;x+=kGlyphs[c-32].advance*s*1.15f;}return x;
}
static void txt(float x,float y,const char*t,float s,Vec4 c,float sp=1.15f){
    if(!t||!*t)return;int n=strlen(t);
    std::vector<GlyphVertex>v(n*4);std::vector<GLushort>idx(n*6);float cx=x;
    for(int i=0;i<n;i++){
        unsigned char ch=t[i];if(ch<32||ch>126)ch=32;
        const SDFGlyph&g=kGlyphs[ch-32];
        float gx=cx,gy=y-g.bearingY*s,gw=g.width*s,gh=g.height*s;
        float tx=g.texX,ty=g.texY,tw=g.texW,th=g.texH;
        int vi=i*4;float spd=SDF_SPREAD_METRICS*s;
        v[vi+0]={gx-spd,gy-spd,tx,ty+th};v[vi+1]={gx+gw+spd,gy-spd,tx+tw,ty+th};
        v[vi+2]={gx+gw+spd,gy+gh+spd,tx+tw,ty};v[vi+3]={gx-spd,gy+gh+spd,tx,ty};
        int ii=i*6,b=vi;idx[ii+0]=b;idx[ii+1]=b+1;idx[ii+2]=b+2;idx[ii+3]=b;idx[ii+4]=b+2;idx[ii+5]=b+3;
        cx+=g.advance*s*sp;
    }
    glUseProgram(G.prog);glUniform1i(G.uIsTex,1);glUniform1i(G.uIsRGBA,0);
    glUniform4f(G.uColor,c.r,c.g,c.b,c.a);glUniform1f(G.uSmooth,0.08f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,G.tex);glUniform1i(G.uTex,0);
    float mvp[16];ortho(mvp,0,(float)G.w,(float)G.h,0,-1,1);glUniformMatrix4fv(G.uMVP,1,GL_FALSE,mvp);
    glBindBuffer(GL_ARRAY_BUFFER,G.vboG);
    glBufferData(GL_ARRAY_BUFFER,v.size()*sizeof(GlyphVertex),v.data(),GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoG);
    GLuint ibo;glGenBuffers(1,&ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,idx.size()*sizeof(GLushort),idx.data(),GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,idx.size(),GL_UNSIGNED_SHORT,nullptr);
    glDeleteBuffers(1,&ibo);glBindVertexArray(0);
}

static void rct(float x,float y,float w,float h,Vec4 c){
    float v[]={x,y,0,0,x+w,y,1,0,x+w,y+h,1,1,x,y+h,0,1};
    GLushort idx[]={0,1,2,0,2,3};
    glUseProgram(G.prog);glUniform1i(G.uIsTex,0);glUniform1i(G.uIsRGBA,0);glUniform4f(G.uColor,c.r,c.g,c.b,c.a);
    float mvp[16];ortho(mvp,0,(float)G.w,(float)G.h,0,-1,1);glUniformMatrix4fv(G.uMVP,1,GL_FALSE,mvp);
    glBindBuffer(GL_ARRAY_BUFFER,G.vboR);glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoR);
    GLuint ibo;glGenBuffers(1,&ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,nullptr);
    glDeleteBuffers(1,&ibo);glBindVertexArray(0);
}

/* Rounded rect: leaves corners empty (relies on solid bg for round look) */
static void rrct(float x,float y,float w,float h,float r,Vec4 c){
    if(r<1.0f||r*2>w||r*2>h){rct(x,y,w,h,c);return;}
    /* Three horizontal strips */
    rct(x+r, y,      w-2*r, r,   c); /* top middle */
    rct(x,   y+r,    w,     h-2*r,c); /* full middle */
    rct(x+r, y+h-r,  w-2*r, r,   c); /* bottom middle */
    /* Two vertical strips (corners stay empty = rounded look) */
    rct(x,     y+r, r, h-2*r, c); /* left edge */
    rct(x+w-r, y+r, r, h-2*r, c); /* right edge */
}

/* Sprite: render a texture at (x,y) with given width/height */
static void sprite(float x,float y,float w,float h,GLuint texture){
    float v[]={x,y,0,0, x+w,y,1,0, x+w,y+h,1,1, x,y+h,0,1};
    GLushort idx[]={0,1,2,0,2,3};
    glUseProgram(G.prog);glUniform1i(G.uIsTex,1);
    glUniform1i(G.uIsRGBA,1);
    glUniform4f(G.uColor,1,1,1,1);glUniform1f(G.uSmooth,0.0f);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(G.uTex,0);
    float mvp[16];ortho(mvp,0,(float)G.w,(float)G.h,0,-1,1);glUniformMatrix4fv(G.uMVP,1,GL_FALSE,mvp);
    glBindBuffer(GL_ARRAY_BUFFER,G.vboR);glBufferData(GL_ARRAY_BUFFER,sizeof(v),v,GL_DYNAMIC_DRAW);
    glBindVertexArray(G.vaoR);
    GLuint ibo;glGenBuffers(1,&ibo);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,nullptr);
    glDeleteBuffers(1,&ibo);glBindVertexArray(0);
}

static void btn(const Button&b,float ts){
    Vec4 bg=b.pressed?Vec4{C_BTN_PRESS}:b.color;
    rct(b.rect.x,b.rect.y,b.rect.w,b.rect.h,bg);
    if(b.text){float tw=msr(b.text,ts);
        txt(b.rect.x+(b.rect.w-tw)*0.5f,b.rect.y+b.rect.h*0.5f+ts*0.35f,b.text,ts,Vec4{C_WHITE});}
}
static Button mkB(float x,float y,float w,float h,const char*t,Vec4 c){
    return{{x,y,w,h},t,c,false};}
static bool hit(float px,float py,const Rect&r){return px>=r.x&&px<=r.x+r.w&&py>=r.y&&py<=r.y+r.h;}
static int hitB(float x,float y){for(int i=0;i<G.nb;i++)if(hit(x,y,G.btns[i].rect))return i;return -1;}

/* Improved toggle switch: wide track + circle knob */
static void toggleSwitch(float x,float y,bool on){
    float tw=60.0f,th=32.0f,pad=4.0f,knobD=th-2*pad;
    /* Track - bright green ON, bright grey OFF */
    Vec4 tc=on?Vec4{0.15f,0.75f,0.40f,1.0f}:Vec4{0.45f,0.45f,0.50f,1.0f};
    rrct(x,y,tw,th,th/2,tc);
    /* Knob - white circle */
    float kx=on?x+tw-knobD-pad:x+pad;
    rrct(kx,y+pad,knobD,knobD,knobD/2,Vec4{1.0f,1.0f,1.0f,1.0f});
}

static void genData(){
    G.rooms.clear();
    const char*rn[]={"#welcome","#general","#random","#dev","#matrix"};
    const char*rt[]={
        "Welcome to Progressive IRC | Please read /topic",
        "General discussion about anything and everything",
        "Random off-topic chat | Keep it civil",
        "Development | C++ / OpenGL / Matrix bridge",
        "Matrix protocol discussion and testing"};
    struct{const char*n,*t;int h,m;}ms[][8]={
        {{nullptr,"--> alice (~alice) joined #welcome",9,5},
         {"alice","hello everyone!",9,6},{nullptr,"--> bob (~bob) joined #welcome",9,7},
         {"bob","hey alice, how's it going?",9,7},{"alice","pretty good! working on the renderer",9,8},
         {nullptr,"--> charlie (~charlie) joined #welcome",9,9},
         {"charlie","what renderer are you using?",9,10},
         {"alice","pure OpenGL ES with signed-distance-field fonts",9,12}},
        {{nullptr,"--> dave joined #general",10,14},{"dave","anyone here?",10,15},
         {"eve","yep, just lurking",10,16},{nullptr,"<-- frank left #general",10,18}},
        {{"frank","lol check this out https://example.com",11,20},
         {"grace","haha that's amazing",11,21},{"heidi","i don't get it",11,22},
         {"frank","you had to be there",11,23}},
        {{"ivan","pushed a new commit to the renderer",14,0},
         {nullptr,"--> judy joined #dev",14,1},{"judy","nice! SDF fonts looking crisp?",14,2},
         {"ivan","yeah, smoothstep AA is working great",14,3},
         {"karen","can we use this for the main app?",14,5},
         {"ivan","that's the plan! merging into 0.5.5+",14,6}},
        {{"larry","testing the matrix bridge",15,30},{"mike","seems to be working",15,31},
         {"nancy","which homeserver are you on?",15,32},{"larry","matrix.org for now",15,33}}};
    int mc[]={8,4,4,6,4};
    for(int ri=0;ri<5;ri++){
        Room r;r.name=rn[ri];r.topic=rt[ri];r.unread=ri<3?2:0;
        for(int mi=0;mi<mc[ri];mi++){
            Message m;m.nick=ms[ri][mi].n;m.text=ms[ri][mi].t;
            m.h=ms[ri][mi].h;m.m=ms[ri][mi].m;
            if(m.nick){unsigned h=0;for(const char*p=m.nick;*p;p++)h=h*31+*p;m.ci=h%16;}else m.ci=0;
            r.msgs.push_back(m);
        }G.rooms.push_back(r);
    }
}

static void layoutUI(){
    G.nb=0;
    switch(G.screen){
        case SCR_SERVER: G.nb=12; break; /* 3 buttons + settings + 2 chips + 6 cards */
        case SCR_MATRIX: G.nb=6; break; /* back + sign in + create + 3 fields */
        case SCR_SIGNUP: G.nb=4; break; /* back + 3 fields (user/pass/confirm) */
        case SCR_IRC: G.nb=3; break; /* back + TLS + Connect */
        case SCR_PROFILE: G.nb=5; break; /* back + 4 action buttons */
        case SCR_SETTINGS: G.nb=2; break; /* back + about */
        case SCR_CHAT:{
            G.btns[G.nb++]=mkB(6,6,42,42,"<",Vec4{C_DARK}); /* back */
            G.btns[G.nb++]=mkB(52,6,42,42,"#",Vec4{C_DARK}); /* drawer toggle */
            float dy=60.0f;
            for(size_t i=0;i<G.rooms.size();i++){
                G.btns[G.nb++]=mkB(0,dy,G.dw,44,G.rooms[i].name,
                    i==(size_t)G.activeRoom?Vec4{C_SEL}:Vec4{0,0,0,0});dy+=48.0f;}
            G.btns[G.nb++]=mkB(G.w-60.0f,G.h-48.0f,52,40,"Send",Vec4{C_CYAN});
            break;}
    }
}

/* ====== ONBOARDING CAROUSEL ====== */
static void renderOnboard(){
    int page=G.login.carouselPage;
    struct{const char*title;const char*body;}pages[]={
        {"Own your conversations.","Secure and independent communication, same privacy as a face-to-face conversation in your own home."},
        {"You're in control.","Choose where your conversations are kept. Connected via Matrix, giving you full control and independence."},
        {"Secure messaging.","End-to-end encrypted, no phone number required. No ads, no data mining, no tracking."},
        {"Messaging for your team.","Great for the workplace too. Trusted by the world's most secure organisations."},
    };
    int nPages=4;

    /* Page indicator dots - centered */
    float dotR=4.0f*G.dp,dotGap=12.0f*G.dp,dotW=nPages*(dotR*2+dotGap)-dotGap;
    float dotX=(G.w-dotW)*0.5f,dotY=G.h*0.58f;
    for(int i=0;i<nPages;i++){
        rrct(dotX+i*(dotR*2+dotGap),dotY,dotR*2,dotR*2,dotR,
            i==page?Vec4{C_TITLE}:Vec4{0.22f,0.22f,0.28f,1.0f});
    }

    /* Icon placeholder per page */
    Vec4 iconC[]={Vec4{C_CYAN},Vec4{C_GREEN},Vec4{C_PURPLE},Vec4{0.80f,0.65f,0.30f,1.0f}};
    float is=G.w*0.26f,iy=G.h*0.12f;
    rrct((G.w-is)*0.5f,iy,is,is,is*0.22f,iconC[page]);

    /* Title */
    float ty=iy+is+20.0f*G.dp;
    float tsz=18.0f*G.dp;
    txt((G.w-msr(pages[page].title,tsz))*0.5f,ty,pages[page].title,tsz,Vec4{C_WHITE});

    /* Body - multi-line word wrap */
    float bx=G.w*0.12f,bw=G.w*0.76f,by=ty+24.0f*G.dp,bsz=13.0f*G.dp;
    const char*b=pages[page].body;
    char word[64];int wi=0;
    float cx=bx;
    for(const char*p=b;;p++){
        if(*p==' '||*p=='\0'){
            if(wi>0){word[wi]=0;wi=0;
                float ww=msr(word,bsz);
                if(cx+ww>bx+bw&&cx>bx+4.0f){cx=bx;by+=bsz*1.5f;}
                txt(cx,by,word,bsz,Vec4{C_LABEL});
                cx+=ww+msr(" ",bsz);
            }
            if(*p=='\0')break;
        }else{word[wi++]=(unsigned char)*p;if(wi>=63)wi=63;}
    }

    /* Buttons at bottom */
    float btnW=G.w*0.76f,btnH=44.0f*G.dp,btnX=(G.w-btnW)*0.5f;
    float btnY=G.h*0.80f;

    /* Sign In - text button (no fill) */
    txt((G.w-msr("Sign In",14.0f*G.dp))*0.5f,btnY+8.0f,"Sign In",14.0f*G.dp,Vec4{C_CYAN});
    G.btns[0].rect={btnX-8.0f,btnY-4.0f,btnW+16.0f,btnH*0.7f};
    G.btns[0].text=nullptr;G.btns[0].color=Vec4{0,0,0,0};
    btnY+=btnH*0.7f+8.0f*G.dp;

    /* Create account - filled button */
    G.btns[1].rect={btnX,btnY,btnW,btnH};
    G.btns[1].text="Create account";
    G.btns[1].color=Vec4{C_CYAN};
    btn(G.btns[1],14.0f*G.dp);

    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-24.0f,
        "Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== PROTOCOL SELECTION SCREEN ====== */
static void renderServerSelect(){
    float pad=G.w*0.05f,fw=G.w*0.90f,cardH=52.0f*G.dp,cardGap=12.0f*G.dp;
    float chipH=36.0f*G.dp;
    int page=G.login.carouselPage;
    float y=G.h*0.02f;

    /* === LOGO + PROTOCOL SELECTION (top) === */
    if(G.texLogo&&G.logoW>0){
        float is=G.w*0.13f;
        sprite((G.w-is)*0.5f,y,is,is,G.texLogo);
        y+=is+8.0f*G.dp;
    }
    txt((G.w-msr("Progressive Chat",20.0f*G.dp))*0.5f,y,"Progressive Chat",20.0f*G.dp,Vec4{C_TITLE});
    y+=30.0f*G.dp;
    txt((G.w-msr("Choose your protocol",14.0f*G.dp))*0.5f,y,"Choose your protocol",14.0f*G.dp,Vec4{C_LABEL});
    y+=30.0f*G.dp;

    /* Category chips */
    float chipW=fw*0.5f-5.0f;
    bool openSrc=(G.login.cat==0);
    rct(pad,y,chipW,chipH,openSrc?Vec4{0.25f,0.25f,0.33f,1.0f}:Vec4{0.12f,0.12f,0.17f,1.0f});
    txt(pad+chipW*0.15f,y+chipH*0.32f+5.0f,"Open source",12.0f*G.dp,openSrc?Vec4{C_WHITE}:Vec4{C_LABEL});
    G.btns[4].rect={pad,y,chipW,chipH};
    rct(pad+chipW+10.0f,y,chipW,chipH,!openSrc?Vec4{0.25f,0.25f,0.33f,1.0f}:Vec4{0.12f,0.12f,0.17f,1.0f});
    txt(pad+chipW+10.0f+chipW*0.2f,y+chipH*0.32f+5.0f,"Proprietary",12.0f*G.dp,!openSrc?Vec4{C_WHITE}:Vec4{C_LABEL});
    G.btns[5].rect={pad+chipW+10.0f,y,chipW,chipH};
    y+=chipH+16.0f*G.dp;

    txt(pad+4.0f,y,openSrc?"Decentralized, fully open and free.":"Popular platforms with closed-source servers.",11.0f*G.dp,Vec4{C_HINT});
    y+=24.0f*G.dp;

    /* Protocol cards */
    struct Card{const char*title,*desc;Vec4 accent;bool dim;};
    Card openCards[]={
        {"Matrix","Modern messenger with E2E encryption",Vec4{C_CYAN},false},
        {"IRC","Lightweight classic for old-school hackers",Vec4{C_GREEN},false},
        {"XMPP","Federated instant messaging since 1999",Vec4{C_PURPLE},true},
        {"Delta Chat","Chat over email - no new account needed",Vec4{0.40f,0.55f,0.80f,1.0f},true},
        {"Lemmy","Decentralized link aggregator",Vec4{0.80f,0.65f,0.30f,1.0f},true},
        {"Mastodon","Decentralized microblogging",Vec4{0.50f,0.45f,0.85f,1.0f},true},
    };
    Card propCards[]={
        {"Telegram","Popular messenger with closed-source server",Vec4{0.30f,0.65f,0.90f,1.0f},true},
        {"Reddit","The front page of the internet",Vec4{0.88f,0.45f,0.30f,1.0f},true},
        {"WhatsApp","End-to-end encrypted messaging",Vec4{0.25f,0.70f,0.45f,1.0f},true},
        {"Discord","Community chat platform",Vec4{0.45f,0.50f,0.85f,1.0f},true},
        {"Slack","Team collaboration hub",Vec4{0.65f,0.30f,0.70f,1.0f},true},
        {"Signal","Private messenger, open protocol",Vec4{0.35f,0.55f,0.85f,1.0f},true},
    };
    int nCards=6;
    Card* cards=openSrc?openCards:propCards;
    for(int i=0;i<nCards;i++){
        float alpha=cards[i].dim?0.55f:1.0f;
        rct(pad,y,fw,cardH,Vec4{0.22f*alpha,0.22f*alpha,0.30f*alpha,1.0f});
        rct(pad,y,3.0f,cardH,Vec4{cards[i].accent.r*alpha,cards[i].accent.g*alpha,cards[i].accent.b*alpha,1.0f});
        rrct(pad+10.0f*G.dp,y+6.0f*G.dp,38.0f*G.dp,38.0f*G.dp,10.0f*G.dp,Vec4{cards[i].accent.r*0.45f*alpha,cards[i].accent.g*0.45f*alpha,cards[i].accent.b*0.45f*alpha,1.0f});
        txt(pad+56.0f*G.dp,y+14.0f*G.dp,cards[i].title,12.0f*G.dp,cards[i].dim?Vec4{C_LABEL}:Vec4{C_WHITE});
        txt(pad+56.0f*G.dp,y+32.0f*G.dp,cards[i].desc,10.0f*G.dp,cards[i].dim?Vec4{C_HINT}:Vec4{C_LABEL});
        if(cards[i].dim)txt(pad+fw-70.0f,y+14.0f*G.dp,"Soon",9.0f*G.dp,Vec4{C_LABEL});
        G.btns[6+i].rect={pad,y,fw,cardH};
        G.btns[6+i].color=cards[i].accent;
        y+=cardH+cardGap;
    }

    /* === BOTTOM BUTTONS === */
    y+=16.0f*G.dp;
    rct(pad,y,fw,1.0f,Vec4{C_DIVIDER});
    y+=16.0f*G.dp;

    float btnW=fw*0.48f,btnH=36.0f*G.dp;
    G.btns[0].rect={pad,y,btnW,btnH};G.btns[0].text="Sign In";G.btns[0].color=Vec4{C_DARK};btn(G.btns[0],12.0f*G.dp);
    G.btns[1].rect={pad+btnW+fw*0.04f,y,btnW,btnH};G.btns[1].text="Create account";G.btns[1].color=Vec4{C_CYAN};btn(G.btns[1],12.0f*G.dp);
    y+=btnH+8.0f*G.dp;
    G.btns[2].rect={pad,y,fw,btnH};G.btns[2].text="Test without account";G.btns[2].color=Vec4{C_GREEN};btn(G.btns[2],12.0f*G.dp);
    /* Settings gear at bottom-right */
    G.btns[3].rect={G.w-50.0f,G.h-50.0f,40.0f,40.0f};G.btns[3].text="#";G.btns[3].color=Vec4{C_DARK};btn(G.btns[3],16.0f*G.dp);

    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-22.0f,"Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== IRC AUTH SCREEN ====== */
static void renderIrcAuth(){
    /* Back button */
    G.btns[0].rect={8.0f,8.0f,50.0f,40.0f};
    G.btns[0].text="<";
    G.btns[0].color=Vec4{C_DARK};
    btn(G.btns[0],22.0f);

    float pad=G.w*0.08f,fw=G.w*0.84f;
    float fieldH=48.0f*G.dp,fieldGap=24.0f*G.dp;
    int nFields=4;
    float fieldsTotal=nFields*(fieldH+fieldGap);
    float tlsH=42.0f*G.dp,btnH=48.0f*G.dp;
    float titleH=44.0f*G.dp,footerH=24.0f*G.dp;
    float cardPad=20.0f*G.dp;
    float cardH=titleH+fieldsTotal+tlsH+cardPad*2+btnH+16.0f*G.dp;
    /* Center card vertically */
    float cardY=(G.h-cardH)*0.22f;
    if(cardY<G.h*0.02f)cardY=G.h*0.02f;

    float cy=cardY+cardPad;

    /* Card background - clearly distinct from page bg */
    rrct(pad-8.0f,cardY,fw+16.0f,cardH,16.0f,Vec4{0.15f,0.15f,0.22f,1.0f});
    /* Card subtle border */
    rrct(pad-8.0f+1.0f,cardY+1.0f,fw+16.0f-2.0f,cardH-2.0f,15.0f,Vec4{0.17f,0.17f,0.23f,1.0f});

    /* Title inside card */
    txt(pad+fw*0.04f,cy+10.0f,"Connect to IRC",26.0f,Vec4{C_TITLE});
    cy+=titleH+8.0f;

    /* Divider */
    rct(pad,cy,fw,1.0f,Vec4{C_DIVIDER});
    cy+=16.0f;

    /* Fields */
    auto field=[&](const char*label,const char*val,const char*hint){
        txt(pad+4.0f,cy,label,13.0f,Vec4{C_TITLE},1.05f);
        rrct(pad,cy+16.0f,fw,fieldH,10.0f,Vec4{0.15f,0.15f,0.20f,1.0f});
        if(val&&*val)txt(pad+12.0f,cy+16.0f+fieldH*0.5f+5.0f,val,16.0f,Vec4{C_WHITE},1.03f);
        else txt(pad+12.0f,cy+16.0f+fieldH*0.5f+5.0f,hint,16.0f*G.dp,Vec4{C_HINT},1.03f);
        cy+=fieldH+fieldGap;
    };

    field("Server","irc.libera.chat","irc.libera.chat");
    field("Port","6667","6667");
    field("Nickname","progressive_user","your nickname");
    field("Password","","********");

    /* TLS row inside card - bright, undeniable */
    cy+=4.0f;
    /* Background highlight for TLS row */
    rrct(pad,cy-2.0f,fw,tlsH+4.0f,8.0f,Vec4{0.11f,0.11f,0.16f,1.0f});
    txt(pad+8.0f,cy+8.0f,"Use TLS/SSL",14.0f*G.dp,Vec4{C_WHITE});
    G.btns[1].rect={pad+fw-70.0f,cy,70.0f,34.0f};
    G.btns[1].color=G.login.tls?Vec4{0.18f,0.70f,0.40f,1.0f}:Vec4{0.35f,0.35f,0.40f,1.0f};
    toggleSwitch(pad+fw-62.0f,cy+2.0f,G.login.tls);
    cy+=tlsH+12.0f;

    /* Divider before button */
    rct(pad,cy,fw,1.0f,Vec4{C_DIVIDER});
    cy+=14.0f;

    /* Connect button inside card */
    G.btns[2].rect={pad,cy,fw,btnH};
    G.btns[2].text="Connect";
    G.btns[2].color=Vec4{C_CYAN};
    btn(G.btns[2],14.0f*G.dp);

    /* Footer */
    txt((G.w-msr("Progressive Chat v0.5.5-pre",12.0f))*0.5f,G.h-28.0f,
        "Progressive Chat v0.5.5-pre",12.0f,Vec4{C_HINT});
}

/* ====== MATRIX LOGIN SCREEN ====== */
static void renderMatrixLogin(){
    /* Init field buffers on first render */
    if(G.login.hsLen==0){strcpy(G.login.hsUrl,"matrix.org");G.login.hsLen=10;}
    if(G.login.userLen==0){strcpy(G.login.user,"@user:matrix.org");G.login.userLen=16;}
    if(G.login.passLen==0){strcpy(G.login.pass,"");G.login.passLen=0;}

    /* Back button */
    G.btns[0].rect={8.0f,8.0f,50.0f,40.0f};
    G.btns[0].text="<";G.btns[0].color=Vec4{C_DARK};
    btn(G.btns[0],14.0f*G.dp);

    float pad=G.w*0.06f,fw=G.w*0.88f;
    float fieldH=52.0f*G.dp,fieldGap=24.0f*G.dp,btnH=48.0f*G.dp;
    float titleH=40.0f*G.dp,cardPad=24.0f*G.dp;
    float cardH=titleH+3*(fieldH+fieldGap)+cardPad*2+btnH+20.0f*G.dp;
    float cardY=(G.h-cardH)*0.20f;
    if(cardY<G.h*0.02f)cardY=G.h*0.02f;

    rrct(pad-6.0f,cardY,fw+12.0f,cardH,16.0f,Vec4{0.15f,0.15f,0.22f,1.0f});

    float cy=cardY+cardPad;
    txt(pad,cy+8.0f,"Sign in to matrix.org",18.0f*G.dp,Vec4{C_TITLE});
    cy+=titleH+8.0f*G.dp;
    rct(pad,cy,fw,1.0f,Vec4{C_DIVIDER});
    cy+=16.0f*G.dp;

    /* Field 1: Homeserver URL */
    int ff=G.login.focusField;
    txt(pad+4.0f,cy,"Homeserver URL",13.0f*G.dp,Vec4{C_TITLE},1.05f);
    rrct(pad,cy+18.0f,fw,fieldH,10.0f,ff==1?Vec4{0.18f,0.18f,0.26f,1.0f}:Vec4{0.12f,0.12f,0.17f,1.0f});
    rct(pad,cy+18.0f+fieldH-2.0f,fw,2.0f,ff==1?Vec4{C_CYAN}:Vec4{0.22f,0.25f,0.32f,1.0f});
    G.login.hsUrl[G.login.hsLen]=0;
    txt(pad+14.0f,cy+18.0f+fieldH*0.5f+5.0f,G.login.hsUrl,16.0f*G.dp,ff==1?Vec4{0,0,0,0}:Vec4{C_WHITE},1.03f);
    /* Cursor blink for focused field */
    if(ff==1&&(G.login.frameCount/30)%2){
        float cx=pad+14.0f+msr(G.login.hsUrl,16.0f*G.dp);
        rct(cx,cy+18.0f+6.0f,2.0f,fieldH-12.0f,Vec4{C_CYAN});
    }
    G.btns[0]={G.btns[0]}; /* keep back button */
    /* Field 1 touch area */
    G.btns[3].rect={pad,cy-24.0f*G.dp,fw,fieldH+18.0f};
    G.btns[3].color=Vec4{0,0,0,0};G.btns[3].text=nullptr;
    cy+=fieldH+fieldGap;

    /* Field 2: Username */
    txt(pad+4.0f,cy,"Username",13.0f*G.dp,Vec4{C_TITLE},1.05f);
    rrct(pad,cy+18.0f,fw,fieldH,10.0f,ff==2?Vec4{0.18f,0.18f,0.26f,1.0f}:Vec4{0.12f,0.12f,0.17f,1.0f});
    rct(pad,cy+18.0f+fieldH-2.0f,fw,2.0f,ff==2?Vec4{C_CYAN}:Vec4{0.22f,0.25f,0.32f,1.0f});
    G.login.user[G.login.userLen]=0;
    txt(pad+14.0f,cy+18.0f+fieldH*0.5f+5.0f,G.login.user,16.0f*G.dp,ff==2?Vec4{0,0,0,0}:Vec4{C_WHITE},1.03f);
    if(ff==2&&(G.login.frameCount/30)%2){
        float cx=pad+14.0f+msr(G.login.user,16.0f*G.dp);
        rct(cx,cy+18.0f+6.0f,2.0f,fieldH-12.0f,Vec4{C_CYAN});
    }
    G.btns[4].rect={pad,cy-24.0f*G.dp,fw,fieldH+18.0f};
    G.btns[4].color=Vec4{0,0,0,0};G.btns[4].text=nullptr;
    cy+=fieldH+fieldGap;

    /* Field 3: Password */
    txt(pad+4.0f,cy,"Password",13.0f*G.dp,Vec4{C_TITLE},1.05f);
    rrct(pad,cy+18.0f,fw,fieldH,10.0f,ff==3?Vec4{0.18f,0.18f,0.26f,1.0f}:Vec4{0.12f,0.12f,0.17f,1.0f});
    rct(pad,cy+18.0f+fieldH-2.0f,fw,2.0f,ff==3?Vec4{C_CYAN}:Vec4{0.22f,0.25f,0.32f,1.0f});
    char masked[64];int ml=G.login.passLen;if(ml>60)ml=60;
    for(int i=0;i<ml;i++)masked[i]='*';masked[ml]=0;
    txt(pad+14.0f,cy+18.0f+fieldH*0.5f+5.0f,ml>0?masked:"",16.0f*G.dp,ff==3?Vec4{0,0,0,0}:Vec4{C_WHITE},1.03f);
    if(ff==3&&(G.login.frameCount/30)%2){
        float cx=pad+14.0f+(ml>0?msr(masked,16.0f*G.dp):0);
        rct(cx,cy+18.0f+6.0f,2.0f,fieldH-12.0f,Vec4{C_CYAN});
    }
    G.btns[5].rect={pad,cy-24.0f*G.dp,fw,fieldH+18.0f};
    G.btns[5].color=Vec4{0,0,0,0};G.btns[5].text=nullptr;
    cy+=fieldH+fieldGap+8.0f*G.dp;

    /* Sign in button */
    G.btns[1].rect={pad,cy,fw,btnH};
    G.btns[1].text="Sign in";G.btns[1].color=Vec4{C_CYAN};
    btn(G.btns[1],14.0f*G.dp);
    cy+=btnH+12.0f*G.dp;

    /* Create account link */
    txt((G.w-msr("Create account",14.0f*G.dp))*0.5f,cy+4.0f,"Create account",14.0f*G.dp,Vec4{C_CYAN});
    G.btns[2].rect={(G.w-msr("Create account",14.0f*G.dp))*0.5f-8.0f,cy,msr("Create account",14.0f*G.dp)+16.0f,28.0f};
    G.btns[2].text=nullptr;G.btns[2].color=Vec4{0,0,0,0};

    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-24.0f,
        "Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== SIGN UP SCREEN ====== */
static void renderSignup(){
    G.btns[0].rect={8.0f,8.0f,50.0f,40.0f};
    G.btns[0].text="<";G.btns[0].color=Vec4{C_DARK};
    btn(G.btns[0],14.0f*G.dp);

    float pad=G.w*0.06f,fw=G.w*0.88f;
    float fieldH=52.0f*G.dp,fieldGap=24.0f*G.dp;
    float titleH=40.0f*G.dp,cardPad=24.0f*G.dp;
    float cardH=titleH+3*(fieldH+fieldGap)+cardPad*2+48.0f*G.dp+20.0f*G.dp;
    float cardY=(G.h-cardH)*0.20f;
    if(cardY<G.h*0.02f)cardY=G.h*0.02f;
    rrct(pad-6.0f,cardY,fw+12.0f,cardH,16.0f,Vec4{0.15f,0.15f,0.22f,1.0f});

    float cy=cardY+cardPad;
    txt(pad,cy+8.0f,"Create account",18.0f*G.dp,Vec4{C_TITLE});
    cy+=titleH+8.0f*G.dp;
    rct(pad,cy,fw,1.0f,Vec4{C_DIVIDER});
    cy+=16.0f*G.dp;

    const char* labels[]={"Username","Password","Confirm password"};
    for(int i=0;i<3;i++){
        txt(pad+4.0f,cy,labels[i],13.0f*G.dp,Vec4{C_TITLE},1.05f);
        rrct(pad,cy+18.0f,fw,fieldH,10.0f,Vec4{0.12f,0.12f,0.17f,1.0f});
        rct(pad,cy+18.0f+fieldH-2.0f,fw,2.0f,Vec4{0.22f,0.25f,0.32f,1.0f});
        txt(pad+14.0f,cy+18.0f+fieldH*0.5f+5.0f,i==0?"@user:matrix.org":(i>=1?"********":""),16.0f*G.dp,Vec4{C_WHITE},1.03f);
        G.btns[1+i].rect={pad,cy-24.0f*G.dp,fw,fieldH+18.0f};
        G.btns[1+i].color=Vec4{0,0,0,0};G.btns[1+i].text=nullptr;
        cy+=fieldH+fieldGap;
    }
    cy+=8.0f*G.dp;

    /* Register button */
    Button reg={{pad,cy,fw,48.0f*G.dp},"Register",Vec4{C_CYAN},false};
    btn(reg,14.0f*G.dp);

    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-24.0f,
        "Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== PROFILE SCREEN ====== */
static void renderProfile(){
    G.btns[0].rect={8,8,50,40};G.btns[0].text="<";G.btns[0].color=Vec4{C_DARK};btn(G.btns[0],14.0f*G.dp);
    float pad=G.w*0.08f,fw=G.w*0.84f,y=G.h*0.08f;
    /* Avatar */
    float ar=G.w*0.12f;rrct((G.w-ar)*0.5f,y,ar,ar,ar*0.5f,Vec4{C_CYAN});
    char init[2]={G.login.profileNick[0]?G.login.profileNick[0]:'?',0};
    txt((G.w-msr(init,ar*0.4f))*0.5f,y+ar*0.6f,init,ar*0.4f,Vec4{C_WHITE},1.0f);
    y+=ar+16.0f*G.dp;
    txt((G.w-msr(G.login.profileNick,18.0f*G.dp))*0.5f,y,G.login.profileNick,18.0f*G.dp,Vec4{C_WHITE});
    y+=28.0f*G.dp;
    txt((G.w-msr("@user:matrix.org",13.0f*G.dp))*0.5f,y,"@user:matrix.org",13.0f*G.dp,Vec4{C_LABEL});
    y+=40.0f*G.dp;
    const char* actions[]={"Send message","View details","Mention","Block user"};
    for(int i=0;i<4;i++){
        rrct(pad,y,fw,44.0f*G.dp,10.0f,Vec4{0.15f,0.15f,0.22f,1.0f});
        txt(pad+16.0f,y+28.0f*G.dp,actions[i],14.0f*G.dp,Vec4{C_WHITE});
        G.btns[1+i].rect={pad,y,fw,44.0f*G.dp};
        G.btns[1+i].color=Vec4{0,0,0,0};G.btns[1+i].text=nullptr;
        y+=48.0f*G.dp;
    }
    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-22.0f,"Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== SETTINGS SCREEN ====== */
static void renderSettings(){
    G.btns[0].rect={8,8,50,40};G.btns[0].text="<";G.btns[0].color=Vec4{C_DARK};btn(G.btns[0],14.0f*G.dp);
    float pad=G.w*0.08f,fw=G.w*0.84f,y=G.h*0.10f;
    txt(pad,y,"Settings",22.0f*G.dp,Vec4{C_TITLE});
    y+=40.0f*G.dp;rct(pad,y,fw,1,Vec4{C_DIVIDER});y+=16.0f*G.dp;
    const char* items[]={"Notifications","Appearance","Privacy","About"};
    for(int i=0;i<4;i++){
        rrct(pad,y,fw,44.0f*G.dp,8.0f,Vec4{0.15f,0.15f,0.22f,1.0f});
        txt(pad+16.0f,y+28.0f*G.dp,items[i],14.0f*G.dp,Vec4{C_WHITE});
        y+=48.0f*G.dp;
    }
    G.btns[1].rect={pad,y,fw,40.0f*G.dp};G.btns[1].text="Progressive Chat v0.5.5-pre";G.btns[1].color=Vec4{0,0,0,0};
    txt((G.w-msr("Progressive Chat v0.5.5-pre",10.0f*G.dp))*0.5f,G.h-22.0f,"Progressive Chat v0.5.5-pre",10.0f*G.dp,Vec4{C_HINT});
}

/* ====== CHAT SCREEN ====== */
static void renderDrawer(){
    if(G.ds==DS_CLOSED&&G.dx<1.0f)return;
    float dx=-G.dw+G.dx;
    rct(dx,0,G.dw,(float)G.h,Vec4{C_DRAWER});
    txt(dx+12.0f*G.dp,32.0f*G.dp,"Rooms",14.0f*G.dp,Vec4{C_TITLE});
    rct(dx,44.0f*G.dp,G.dw,1.0f,Vec4{C_DIVIDER});
    float y=52.0f*G.dp;
    for(size_t i=0;i<G.rooms.size();i++){
        bool sel=(int)i==G.activeRoom;
        if(sel)rct(dx+4.0f,y+2.0f,G.dw-8.0f,36.0f*G.dp,Vec4{C_SEL});
        txt(dx+12.0f*G.dp,y+22.0f*G.dp,G.rooms[i].name,14.0f*G.dp,sel?Vec4{C_WHITE}:Vec4{C_LABEL});
        if(G.rooms[i].unread>0){
            char ub[8];snprintf(ub,8,"%d",G.rooms[i].unread);
            float uw=msr(ub,13.0f);
            rct(dx+G.dw-uw-20.0f,y+8.0f,uw+12.0f,18.0f,Vec4{C_CYAN});
            txt(dx+G.dw-uw-14.0f,y+22.0f*G.dp,ub,11.0f*G.dp,Vec4{C_WHITE});
        }y+=40.0f*G.dp;
    }
}

static void renderChat(){
    if(G.rooms.empty())return;
    Room&r=G.rooms[G.activeRoom];
    float hdrH=40.0f*G.dp;

    rct(0,0,(float)G.w,hdrH,Vec4{C_DARK});
    txt(12.0f*G.dp,hdrH*0.65f,r.name,16.0f*G.dp,Vec4{C_WHITE});
    if(r.topic)txt(12.0f*G.dp,hdrH-4.0f,r.topic,10.0f*G.dp,Vec4{C_LABEL});
    rct(0,hdrH,(float)G.w,1.0f,Vec4{C_DIVIDER});

    float msgTop=hdrH+4.0f,msgBot=G.h-44.0f*G.dp,area=msgBot-msgTop;
    float lh=18.0f*G.dp,total=r.msgs.size()*lh+8.0f;
    G.ms=total-area;if(G.ms<0)G.ms=0;
    if(G.sy<0)G.sy=0;if(G.sy>G.ms)G.sy=G.ms;

    glEnable(GL_SCISSOR_TEST);
    glScissor(0,(GLint)(G.h-msgBot),G.w,(GLsizei)area);
    rct(0,msgTop,(float)G.w,area,Vec4{C_BG});

    float my=msgTop+8.0f-G.sy;
    for(auto&m:r.msgs){
        char ts[16];snprintf(ts,16,"[%02d:%02d]",m.h,m.m);
        txt(6.0f*G.dp,my+lh*0.75f,ts,12.0f*G.dp,Vec4{C_TS},1.0f);
        float tx=6.0f*G.dp+msr(ts,12.0f*G.dp)+4.0f;
        if(!m.nick)txt(tx,my+lh*0.75f,m.text,12.0f*G.dp,Vec4{C_SYSMSG},1.0f);
        else{
            Vec4 nc={kNicks[m.ci][0],kNicks[m.ci][1],kNicks[m.ci][2],1.0f};
            /* Avatar circle */
            float ax=tx-2.0f,ay=my+lh*0.3f,ar=lh*0.35f;
            rrct(ax,ay-ar,ar*2,ar*2,ar,nc);
            /* Initial letter */
            char init[2]={(char)m.nick[0],0};
            txt(ax+ar-msr(init,10.0f*G.dp)*0.5f,ay+4.0f,init,10.0f*G.dp,Vec4{C_WHITE},1.0f);
            tx=ax+ar*2+6.0f;
            txt(tx,my+lh*0.75f,m.nick,13.0f*G.dp,nc,1.05f);
            tx+=msr(m.nick,13.0f*G.dp)+4.0f;
            txt(tx,my+lh*0.75f,m.text,13.0f*G.dp,Vec4{C_WHITE},1.05f);
        }my+=lh;
    }
    glDisable(GL_SCISSOR_TEST);

    /* Bottom input */
    float ibH=40.0f*G.dp;
    rct(0,msgBot,(float)G.w,ibH,Vec4{C_INPUT});
    rct(0,msgBot,(float)G.w,1.0f,Vec4{C_DIVIDER});
    rct(6.0f,msgBot+6.0f,G.w-74.0f,ibH-12.0f,Vec4{C_DARK});
    char hintBuf[128];
    snprintf(hintBuf,128,"Message #%s",r.name);
    txt(14.0f,msgBot+ibH*0.65f,hintBuf,13.0f*G.dp,Vec4{C_HINT});
    if(G.nb>0)btn(G.btns[G.nb-1],13.0f*G.dp);

    /* Drawer overlay */
    if(G.ds!=DS_CLOSED){
        rct(0,0,(float)G.w,(float)G.h,Vec4{0,0,0,G.dx/G.dw*0.45f});
        renderDrawer();
    }
}

static void frame(){
    glClearColor(C_BG);glClear(GL_COLOR_BUFFER_BIT);
    switch(G.screen){case SCR_SERVER:renderServerSelect();break;case SCR_MATRIX:renderMatrixLogin();break;case SCR_IRC:renderIrcAuth();break;case SCR_SIGNUP:renderSignup();break;case SCR_PROFILE:renderProfile();break;case SCR_SETTINGS:renderSettings();break;case SCR_CHAT:renderChat();break;}
}

/* ====== TOUCH ====== */
static void td(float x,float y){
    G.tx=x;G.ty=y;G.touching=true;
    if(G.screen==SCR_CHAT&&G.ds==DS_OPEN&&x<G.dw){
        int h=hitB(x,y);
        if(h==1){G.ds=DS_CLOSED;G.dx=0;}
        else if(h>=2&&h<=1+(int)G.rooms.size()){G.activeRoom=h-2;G.sy=0;G.ds=DS_CLOSED;G.dx=0;layoutUI();}
        return;
    }
    /* Message tap in chat: open profile for tapped user */
    if(G.screen==SCR_CHAT&&x>20.0f*G.dp&&x<G.w-80.0f){
        float hdrH=40.0f*G.dp,msgTop=hdrH+4.0f,lh=18.0f*G.dp;
        float relY=y-msgTop+G.sy;
        int msgIdx=(int)(relY/lh);
        Room&r=G.rooms[G.activeRoom];
        if(msgIdx>=0&&msgIdx<(int)r.msgs.size()&&r.msgs[msgIdx].nick){
            strncpy(G.login.profileNick,r.msgs[msgIdx].nick,31);
            G.login.profileNick[31]=0;
            G.login.profileNickLen=strlen(G.login.profileNick);
            G.screen=SCR_PROFILE;layoutUI();return;
        }
    }
    int h=hitB(x,y);
    if(h>=0){G.btns[h].pressed=true;G.ab=h;return;}
    if(G.screen==SCR_CHAT&&x>G.dw){G.sid=1;G.sl=y;G.sv=0;}
}
static void tu(float x,float y){
    G.touching=false;G.sid=0;
    for(int i=0;i<G.nb;i++)if(G.btns[i].pressed){G.btns[i].pressed=false;
        if(hit(x,y,G.btns[i].rect)){
            if(G.screen==SCR_SERVER){
                if(i==0){LOGI("Sign In");}
                else if(i==1){LOGI("Create account");}
                else if(i==2){LOGI("Test");G.screen=SCR_CHAT;G.ds=DS_CLOSED;G.dx=0;G.sy=0;layoutUI();}
                else if(i==3){LOGI("Settings");G.screen=SCR_SETTINGS;layoutUI();}
                else if(i==4){G.login.cat=0;}
                else if(i==5){G.login.cat=1;}
                else if(i==6&&G.login.cat==0){LOGI("Matrix");G.screen=SCR_MATRIX;layoutUI();}
                else if(i==7&&G.login.cat==0){LOGI("IRC");G.screen=SCR_IRC;layoutUI();}
                /* 7-10 are dimmed coming-soon cards, no action */
            }
            else if(G.screen==SCR_MATRIX){
                if(i==0){LOGI("Back");G.screen=SCR_SERVER;G.login.focusField=0;layoutUI();}
                else if(i==1){LOGI("Sign in");G.screen=SCR_CHAT;G.ds=DS_CLOSED;G.dx=0;G.sy=0;layoutUI();}
                else if(i==2){LOGI("Create account");G.screen=SCR_SIGNUP;layoutUI();}
                /* i==3,4,5 are field touch areas */
                else if(i==3)G.login.focusField=1;
                else if(i==4)G.login.focusField=2;
                else if(i==5)G.login.focusField=3;
            }
            else if(G.screen==SCR_SIGNUP){
                if(i==0){LOGI("Back");G.screen=SCR_MATRIX;layoutUI();}
            }
            else if(G.screen==SCR_PROFILE){
                if(i==0){LOGI("Back");G.screen=SCR_CHAT;layoutUI();}
                else if(i==1){LOGI("Send message");G.screen=SCR_CHAT;layoutUI();}
                else if(i==2){LOGI("View details");}
                else if(i==3){LOGI("Mention");G.screen=SCR_CHAT;layoutUI();}
                else if(i==4){LOGI("Block user");G.screen=SCR_CHAT;layoutUI();}
            }
            else if(G.screen==SCR_SETTINGS){
                if(i==0){LOGI("Back");G.screen=SCR_SERVER;layoutUI();}
            }
            else if(G.screen==SCR_IRC){
                if(i==0){LOGI("Back");G.screen=SCR_SERVER;layoutUI();}
                else if(i==1){G.login.tls=!G.login.tls;G.btns[1].color=G.login.tls?Vec4{0.18f,0.70f,0.40f,1.0f}:Vec4{0.35f,0.35f,0.40f,1.0f};}
                else if(i==2){LOGI("Connect");G.screen=SCR_CHAT;G.ds=DS_CLOSED;G.dx=0;G.sy=0;layoutUI();}
            }else if(G.screen==SCR_CHAT){
                if(i==0){LOGI("Back");G.screen=SCR_SERVER;layoutUI();}
                else if(i==1){G.ds=G.ds==DS_CLOSED?DS_OPEN:DS_CLOSED;G.dx=G.ds==DS_OPEN?G.dw:0;}
            }
        }
    }G.ab=-1;
}
static void tm(float x,float y){
    G.tx=x;G.ty=y;
    if(G.sid==1){G.sy+=y-G.sl;G.sv=y-G.sl;G.sl=y;}
    else if(G.sid==2){
        float dx=x-G.sl;
        if(fabsf(dx)>120.0f){G.login.carouselPage+=(dx>0?-1:1);if(G.login.carouselPage<0)G.login.carouselPage=0;if(G.login.carouselPage>3)G.login.carouselPage=3;G.sl=x;G.login.frameCount=0;}
    }
    for(int i=0;i<G.nb;i++)if(i==G.ab)G.btns[i].pressed=hit(x,y,G.btns[i].rect);
}

/* ====== INIT ====== */
static bool initGL(JNIEnv*env,jobject am){
    LOGI("init %dx%d",G.w,G.h);
    G.amgr=AAssetManager_fromJava(env,am);if(!G.amgr)return false;
    G.prog=mkP(kVS,kFS);if(!G.prog)return false;
    G.uMVP=glGetUniformLocation(G.prog,"uMVP");
    G.uTex=glGetUniformLocation(G.prog,"uTex");
    G.uColor=glGetUniformLocation(G.prog,"uColor");
    G.uSmooth=glGetUniformLocation(G.prog,"uSmooth");
    G.uIsTex=glGetUniformLocation(G.prog,"uIsTex");
    G.uIsRGBA=glGetUniformLocation(G.prog,"uIsRGBA");
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

    /* Load launcher icon */
    AAsset* la=AAssetManager_open(G.amgr,"ic_launcher.png",AASSET_MODE_BUFFER);
    if(la){
        const void*ld=AAsset_getBuffer(la);off_t llen=AAsset_getLength(la);
        int lw,lh,lc;
        stbi_set_flip_vertically_on_load(0); /* logotype is already top-down */
        unsigned char*lpx=stbi_load_from_memory((const stbi_uc*)ld,llen,&lw,&lh,&lc,4);
        stbi_set_flip_vertically_on_load(1); /* restore for font */
        AAsset_close(la);
        if(lpx){
            glGenTextures(1,&G.texLogo);glBindTexture(GL_TEXTURE_2D,G.texLogo);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,lw,lh,0,GL_RGBA,GL_UNSIGNED_BYTE,lpx);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            stbi_image_free(lpx);
            G.logoW=lw;G.logoH=lh;
            LOGI("Launcher icon: %dx%d",lw,lh);
        }
    }

    /* Load carousel images */
    for(int ci=0;ci<4;ci++){
        char cname[32];snprintf(cname,32,"carousel_%d.png",ci);
        AAsset* ca=AAssetManager_open(G.amgr,cname,AASSET_MODE_BUFFER);
        if(ca){
            const void*cd=AAsset_getBuffer(ca);off_t clen=AAsset_getLength(ca);
            int cw,ch,cc;
            stbi_set_flip_vertically_on_load(0);
            unsigned char*cpx=stbi_load_from_memory((const stbi_uc*)cd,clen,&cw,&ch,&cc,4);
            stbi_set_flip_vertically_on_load(1);
            AAsset_close(ca);
            if(cpx){
                glGenTextures(1,&G.texCar[ci]);glBindTexture(GL_TEXTURE_2D,G.texCar[ci]);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,cw,ch,0,GL_RGBA,GL_UNSIGNED_BYTE,cpx);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
                stbi_image_free(cpx);
            }
        }
    }

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
    G.dp=(float)G.w/411.0f; /* density scale: 1080px/411dp = 2.63 */
    G.dw=G.w*0.75f;if(G.dw>320.0f)G.dw=320.0f;
    genData();G.screen=SCR_SERVER;G.ds=DS_CLOSED;layoutUI();G.init=true;
    LOGI("init ok");return true;
}

extern "C"{
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeInit(JNIEnv*e,jclass,jint w,jint h,jobject am){
    G.w=w;G.h=h;if(!G.init)initGL(e,am);
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeResize(JNIEnv*,jclass,jint w,jint h){
    G.w=w;G.h=h;G.dw=w*0.75f;if(G.dw>320.0f)G.dw=320.0f;
    if(G.init){glViewport(0,0,w,h);layoutUI();}
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeRender(JNIEnv*,jclass){
    if(G.init){
        /* Auto-switch carousel every ~5s (300 frames at 60fps) */
        if(G.screen==SCR_SERVER&&++G.login.frameCount>300){
            G.login.carouselPage=(G.login.carouselPage+1)%4;
            G.login.frameCount=0;
        }
        if(!G.touching&&fabsf(G.sv)>0.5f){G.sy+=G.sv;G.sv*=0.92f;}
        frame();
    }
}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchDown(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)td(x,y);}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchUp(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)tu(x,y);}
JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeTouchMove(JNIEnv*,jclass,jfloat x,jfloat y){if(G.init)tm(x,y);}

JNIEXPORT jint JNICALL
Java_chat_progressive_app_next_MainActivity_nativeGetFocusField(JNIEnv*,jclass){return G.login.focusField;}

JNIEXPORT jstring JNICALL
Java_chat_progressive_app_next_MainActivity_nativeGetFieldText(JNIEnv*env,jclass,jint field){
    const char* t="";
    if(field==1){G.login.hsUrl[G.login.hsLen]=0;t=G.login.hsUrl;}
    else if(field==2){G.login.user[G.login.userLen]=0;t=G.login.user;}
    else if(field==3){G.login.pass[G.login.passLen]=0;t=G.login.pass;}
    return env->NewStringUTF(t);
}

JNIEXPORT void JNICALL
Java_chat_progressive_app_next_MainActivity_nativeSetFieldText(JNIEnv*env,jclass,jint field,jstring jtext){
    const char* s=env->GetStringUTFChars(jtext,nullptr);
    int len=strlen(s);if(len>60)len=60;
    if(field==1){memcpy(G.login.hsUrl,s,len);G.login.hsLen=len;}
    else if(field==2){memcpy(G.login.user,s,len);G.login.userLen=len;}
    else if(field==3){memcpy(G.login.pass,s,len);G.login.passLen=len;}
    env->ReleaseStringUTFChars(jtext,s);
}
}
