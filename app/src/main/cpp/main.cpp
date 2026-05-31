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
out vec4 fragColor;
void main(){
if(uIsTex){float d=texture(uTex,vTexCoord).r;float a=smoothstep(0.5-uSmooth,0.5+uSmooth,d);fragColor=vec4(uColor.rgb,uColor.a*a);}
else{fragColor=uColor;}
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
enum Screen{SCR_LOGIN,SCR_CHAT};
enum DrawerState{DS_CLOSED,DS_OPEN};

static struct{
    bool init;int w,h;
    GLuint prog,tex,vboG,vboR,vaoG,vaoR;
    GLint uMVP,uTex,uColor,uSmooth,uIsTex;
    Screen screen;
    struct{bool tls;}login;
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
    glUseProgram(G.prog);glUniform1i(G.uIsTex,1);
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
    glUseProgram(G.prog);glUniform1i(G.uIsTex,0);glUniform4f(G.uColor,c.r,c.g,c.b,c.a);
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

static void btn(const Button&b,float ts){
    Vec4 bg=b.pressed?Vec4{C_BTN_PRESS}:b.color;
    rrct(b.rect.x,b.rect.y,b.rect.w,b.rect.h,8.0f,bg);
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
        case SCR_LOGIN:G.nb=2;break; /* 0=TLS,1=Connect */
        case SCR_CHAT:{
            G.btns[G.nb++]=mkB(6,6,42,42,"#",Vec4{C_DARK});
            float dy=60.0f;
            for(size_t i=0;i<G.rooms.size();i++){
                G.btns[G.nb++]=mkB(0,dy,G.dw,44,G.rooms[i].name,
                    i==(size_t)G.activeRoom?Vec4{C_SEL}:Vec4{0,0,0,0});dy+=48.0f;}
            G.btns[G.nb++]=mkB(G.w-60.0f,G.h-48.0f,52,40,"Send",Vec4{C_CYAN});
            break;}
    }
}

/* ====== LOGIN SCREEN ====== */
static void renderLogin(){
    float pad=G.w*0.08f,fw=G.w*0.84f;
    float fieldH=48.0f,fieldGap=24.0f;
    int nFields=4;
    float fieldsTotal=nFields*(fieldH+fieldGap);
    float tlsH=42.0f,btnH=48.0f;
    float titleH=44.0f,footerH=24.0f;
    float cardPad=20.0f;
    float cardH=titleH+fieldsTotal+tlsH+cardPad*2+btnH+24.0f;
    /* Center card vertically */
    float cardY=(G.h-cardH-footerH)*0.35f;
    if(cardY<G.h*0.03f)cardY=G.h*0.03f;

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
        else txt(pad+12.0f,cy+16.0f+fieldH*0.5f+5.0f,hint,16.0f,Vec4{C_HINT},1.03f);
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
    txt(pad+8.0f,cy+8.0f,"Use TLS/SSL",17.0f,Vec4{C_WHITE});
    G.btns[0].rect={pad+fw-70.0f,cy,70.0f,34.0f};
    G.btns[0].color=G.login.tls?Vec4{0.18f,0.70f,0.40f,1.0f}:Vec4{0.35f,0.35f,0.40f,1.0f};
    toggleSwitch(pad+fw-62.0f,cy+2.0f,G.login.tls);
    cy+=tlsH+12.0f;

    /* Divider before button */
    rct(pad,cy,fw,1.0f,Vec4{C_DIVIDER});
    cy+=14.0f;

    /* Connect button inside card */
    G.btns[1].rect={pad,cy,fw,btnH};
    G.btns[1].text="Connect";
    G.btns[1].color=Vec4{C_CYAN};
    btn(G.btns[1],20.0f);

    /* Footer */
    txt((G.w-msr("Progressive IRC  v0.5.5-pre",12.0f))*0.5f,G.h-28.0f,
        "Progressive IRC  v0.5.5-pre",12.0f,Vec4{C_HINT});
}

/* ====== CHAT SCREEN ====== */
static void renderDrawer(){
    if(G.ds==DS_CLOSED&&G.dx<1.0f)return;
    float dx=-G.dw+G.dx;
    rct(dx,0,G.dw,(float)G.h,Vec4{C_DRAWER});
    txt(dx+12,40,"Rooms",20.0f,Vec4{C_TITLE});
    rct(dx,54,G.dw,1.0f,Vec4{C_DIVIDER});
    float y=64.0f;
    for(size_t i=0;i<G.rooms.size();i++){
        bool sel=(int)i==G.activeRoom;
        if(sel)rct(dx+4.0f,y+2.0f,G.dw-8.0f,40.0f,Vec4{C_SEL});
        txt(dx+12,y+28,G.rooms[i].name,18.0f,sel?Vec4{C_WHITE}:Vec4{C_LABEL});
        if(G.rooms[i].unread>0){
            char ub[8];snprintf(ub,8,"%d",G.rooms[i].unread);
            float uw=msr(ub,13.0f);
            rrct(dx+G.dw-uw-20.0f,y+15.0f,uw+12.0f,18.0f,9.0f,Vec4{C_CYAN});
            txt(dx+G.dw-uw-14.0f,y+28,ub,13.0f,Vec4{C_WHITE});
        }y+=48.0f;
    }
}

static void renderChat(){
    if(G.rooms.empty())return;
    Room&r=G.rooms[G.activeRoom];
    float hdrH=52.0f;

    rct(0,0,(float)G.w,hdrH,Vec4{C_DARK});
    txt(50,34,r.name,22.0f,Vec4{C_WHITE});
    if(r.topic)txt(50,48,r.topic,13.0f,Vec4{C_LABEL});
    rct(0,hdrH,(float)G.w,1.0f,Vec4{C_DIVIDER});

    float msgTop=hdrH+4.0f,msgBot=G.h-50.0f,area=msgBot-msgTop;
    float lh=20.0f,total=r.msgs.size()*lh+8.0f;
    G.ms=total-area;if(G.ms<0)G.ms=0;
    if(G.sy<0)G.sy=0;if(G.sy>G.ms)G.sy=G.ms;

    glEnable(GL_SCISSOR_TEST);
    glScissor(0,(GLint)(G.h-msgBot),G.w,(GLsizei)area);
    rct(0,msgTop,(float)G.w,area,Vec4{C_BG});

    float my=msgTop+8.0f-G.sy;
    for(auto&m:r.msgs){
        char ts[16];snprintf(ts,16,"[%02d:%02d]",m.h,m.m);
        txt(6,my+15,ts,13.0f,Vec4{C_TS},1.0f);
        float tx=6+msr(ts,13.0f)+5.0f;
        if(!m.nick)txt(tx,my+15,m.text,13.0f,Vec4{C_SYSMSG},1.0f);
        else{
            Vec4 nc={kNicks[m.ci][0],kNicks[m.ci][1],kNicks[m.ci][2],1.0f};
            txt(tx,my+15,m.nick,14.0f,nc,1.05f);
            tx+=msr(m.nick,14.0f)+5.0f;
            txt(tx,my+15,m.text,14.0f,Vec4{C_WHITE},1.05f);
        }my+=lh;
    }
    glDisable(GL_SCISSOR_TEST);

    /* Bottom input */
    rct(0,msgBot,(float)G.w,50.0f,Vec4{C_INPUT});
    rct(0,msgBot,(float)G.w,1.0f,Vec4{C_DIVIDER});
    rrct(6,msgBot+7,G.w-74.0f,36.0f,6.0f,Vec4{C_DARK});
    char hintBuf[128];
    snprintf(hintBuf,128,"Message #%s",r.name);
    txt(14,msgBot+29,hintBuf,15.0f,Vec4{C_HINT});
    if(G.nb>0)btn(G.btns[G.nb-1],19.0f);

    /* Drawer overlay */
    if(G.ds!=DS_CLOSED){
        rct(0,0,(float)G.w,(float)G.h,Vec4{0,0,0,G.dx/G.dw*0.45f});
        renderDrawer();
    }
}

static void frame(){
    glClearColor(C_BG);glClear(GL_COLOR_BUFFER_BIT);
    switch(G.screen){case SCR_LOGIN:renderLogin();break;case SCR_CHAT:renderChat();break;}
}

/* ====== TOUCH ====== */
static void td(float x,float y){
    G.tx=x;G.ty=y;G.touching=true;
    if(G.screen==SCR_CHAT&&G.ds==DS_OPEN&&x<G.dw){
        int h=hitB(x,y);
        if(h==0){G.ds=DS_CLOSED;G.dx=0;}
        else if(h>0&&h<=(int)G.rooms.size()){G.activeRoom=h-1;G.sy=0;G.ds=DS_CLOSED;G.dx=0;layoutUI();}
        return;
    }
    int h=hitB(x,y);
    if(h>=0){G.btns[h].pressed=true;G.ab=h;return;}
    if(G.screen==SCR_CHAT&&x>G.dw){G.sid=1;G.sl=y;G.sv=0;}
}
static void tu(float x,float y){
    G.touching=false;G.sid=0;
    for(int i=0;i<G.nb;i++)if(G.btns[i].pressed){G.btns[i].pressed=false;
        if(hit(x,y,G.btns[i].rect)){
            if(G.screen==SCR_LOGIN){
                if(i==0){G.login.tls=!G.login.tls;G.btns[0].color=G.login.tls?Vec4{C_TOGGLE_TRACK_ON}:Vec4{C_TOGGLE_TRACK_OFF};}
                else if(i==1){LOGI("Connect");G.screen=SCR_CHAT;G.ds=DS_CLOSED;G.dx=0;G.sy=0;layoutUI();}
            }else if(G.screen==SCR_CHAT){
                if(i==0){G.ds=G.ds==DS_CLOSED?DS_OPEN:DS_CLOSED;G.dx=G.ds==DS_OPEN?G.dw:0;}
            }
        }
    }G.ab=-1;
}
static void tm(float x,float y){
    G.tx=x;G.ty=y;
    if(G.sid==1){G.sy+=y-G.sl;G.sv=y-G.sl;G.sl=y;}
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
    G.dw=G.w*0.75f;if(G.dw>320.0f)G.dw=320.0f;
    genData();G.screen=SCR_LOGIN;G.ds=DS_CLOSED;layoutUI();G.init=true;
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
}
