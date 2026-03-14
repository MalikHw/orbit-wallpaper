#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_image.h>
#include <box2d/box2d.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <timeapi.h>
#include <SDL2/SDL_syswm.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define BG_BLACK     0
#define BG_COLOR     1
#define BG_IMAGE     2

#define FIT_STRETCH 0
#define FIT_ZOOM    1
#define FIT_TILE    2

#define NUM_ORBS    11
#define PPM         40.0f
#define PLAYER_SIZE 80

// Shared with settings via INI
struct Settings {
    int   speed;
    int   fps;
    int   fps_battery;
    int   bg_mode;
    float bg_color[3];
    char  bg_image[512];
    int   bg_fit;
    char  cube_path[512];
    bool  no_ground;
    float orb_scale;
    int   orb_count;
    int   cube_chance;
    bool  pause_fullscreen;
    bool  start_with_windows;
    int   monitor_index; // -1 = all
};

static Settings g_settings = {
    10, 60, 30,
    BG_BLACK, {0.12f,0.12f,0.12f}, "", FIT_STRETCH, "",
    false, 1.0f, 120, 50,
    true, false, -1
};

static std::string getExeDir() {
    char buf[MAX_PATH]; GetModuleFileNameA(NULL,buf,MAX_PATH);
    std::string s(buf); return s.substr(0,s.rfind('\\'));
}
static std::string getCfgPath() { return getExeDir()+"\\settings.ini"; }

static void loadCfg() {
    FILE* f=fopen(getCfgPath().c_str(),"r"); if(!f)return;
    char line[640];
    while(fgets(line,sizeof(line),f)){
        int iv; float fv,fv2,fv3; char sv[512];
        if(sscanf(line,"speed=%d",&iv)==1)             g_settings.speed=iv;
        if(sscanf(line,"fps=%d",&iv)==1)               g_settings.fps=iv;
        if(sscanf(line,"fps_battery=%d",&iv)==1)       g_settings.fps_battery=iv;
        if(sscanf(line,"bg_mode=%d",&iv)==1)           g_settings.bg_mode=iv;
        if(sscanf(line,"bg_color=%f,%f,%f",&fv,&fv2,&fv3)==3){g_settings.bg_color[0]=fv;g_settings.bg_color[1]=fv2;g_settings.bg_color[2]=fv3;}
        if(sscanf(line,"bg_fit=%d",&iv)==1)            g_settings.bg_fit=iv;
        if(sscanf(line,"no_ground=%d",&iv)==1)         g_settings.no_ground=(iv!=0);
        if(sscanf(line,"orb_scale=%f",&fv)==1)         g_settings.orb_scale=fv;
        if(sscanf(line,"orb_count=%d",&iv)==1)         g_settings.orb_count=iv;
        if(sscanf(line,"cube_chance=%d",&iv)==1)       g_settings.cube_chance=iv;
        if(sscanf(line,"pause_fullscreen=%d",&iv)==1)  g_settings.pause_fullscreen=(iv!=0);
        if(sscanf(line,"start_with_windows=%d",&iv)==1)g_settings.start_with_windows=(iv!=0);
        if(sscanf(line,"monitor_index=%d",&iv)==1)     g_settings.monitor_index=iv;
        if(sscanf(line,"bg_image=%511[^\n]",sv)==1)    strncpy(g_settings.bg_image,sv,511);
        if(sscanf(line,"cube_path=%511[^\n]",sv)==1)   strncpy(g_settings.cube_path,sv,511);
    }
    fclose(f);
}

// ---------------------------------------------------------------------------
// Desktop worker window trick
// ---------------------------------------------------------------------------
static HWND g_workerW = nullptr;

static BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam) {
    HWND p = FindWindowExA(hwnd, nullptr, "SHELLDLL_DefView", nullptr);
    if (p) {
        g_workerW = FindWindowExA(nullptr, hwnd, "WorkerW", nullptr);
    }
    return TRUE;
}

static HWND getDesktopWorkerW() {
    HWND progman = FindWindowA("Progman", nullptr);
    // spawn the WorkerW behind icons
    SendMessageTimeoutA(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    g_workerW = nullptr;
    EnumWindows(enumWindowsProc, 0);
    return g_workerW;
}

// ---------------------------------------------------------------------------
// Battery check
// ---------------------------------------------------------------------------
static bool isOnBattery() {
    SYSTEM_POWER_STATUS sps;
    if (!GetSystemPowerStatus(&sps)) return false;
    return sps.ACLineStatus == 0;
}

// ---------------------------------------------------------------------------
// Fullscreen detection
// ---------------------------------------------------------------------------
static bool isFullscreenAppRunning() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    RECT fgRect;
    GetWindowRect(fg, &fgRect);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    return (fgRect.left <= 0 && fgRect.top <= 0 &&
            fgRect.right >= sw && fgRect.bottom >= sh);
}

// ---------------------------------------------------------------------------
// Monitor enumeration
// ---------------------------------------------------------------------------
struct MonitorInfo { HMONITOR hmon; RECT rc; };
static std::vector<MonitorInfo> g_monitors;
static BOOL CALLBACK monitorEnumProc(HMONITOR hmon, HDC, LPRECT rc, LPARAM) {
    g_monitors.push_back({hmon, *rc});
    return TRUE;
}
static void enumMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, 0);
}

// ---------------------------------------------------------------------------
// Texture helpers
// ---------------------------------------------------------------------------
struct Texture { GLuint id; int w,h; bool ok; };

static Texture loadTexture(const char* path) {
    Texture t={0,0,0,false};
    SDL_Surface* surf=IMG_Load(path); if(!surf)return t;
    SDL_Surface* conv=SDL_ConvertSurfaceFormat(surf,SDL_PIXELFORMAT_RGBA32,0);
    SDL_FreeSurface(surf); if(!conv)return t;
    glGenTextures(1,&t.id);
    glBindTexture(GL_TEXTURE_2D,t.id);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,conv->w,conv->h,0,GL_RGBA,GL_UNSIGNED_BYTE,conv->pixels);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    t.w=conv->w;t.h=conv->h;t.ok=true;SDL_FreeSurface(conv);return t;
}

static void drawTexturedQuad(GLuint texId,float cx,float cy,float w,float h,float angleDeg){
    glEnable(GL_TEXTURE_2D);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D,texId);
    glPushMatrix();glTranslatef(cx,cy,0);glRotatef(angleDeg,0,0,1);
    float hw=w/2,hh=h/2;
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-hw,-hh);
    glTexCoord2f(1,0);glVertex2f(hw,-hh);
    glTexCoord2f(1,1);glVertex2f(hw,hh);
    glTexCoord2f(0,1);glVertex2f(-hw,hh);
    glEnd();
    glPopMatrix();glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
}

static void drawBgTex(Texture& bg, int W, int H) {
    if(!bg.ok)return;
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,bg.id);glColor4f(1,1,1,1);
    if(g_settings.bg_fit==FIT_ZOOM){
        float sx=(float)W/bg.w,sy=(float)H/bg.h,sc=fmaxf(sx,sy);
        float dw=bg.w*sc,dh=bg.h*sc,ox=(W-dw)/2,oy=(H-dh)/2;
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(ox,oy);glTexCoord2f(1,0);glVertex2f(ox+dw,oy);glTexCoord2f(1,1);glVertex2f(ox+dw,oy+dh);glTexCoord2f(0,1);glVertex2f(ox,oy+dh);glEnd();
    } else if(g_settings.bg_fit==FIT_TILE){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        float tx=(float)W/bg.w,ty=(float)H/bg.h;
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(tx,0);glVertex2f(W,0);glTexCoord2f(tx,ty);glVertex2f(W,H);glTexCoord2f(0,ty);glVertex2f(0,H);glEnd();
    } else {
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(1,0);glVertex2f(W,0);glTexCoord2f(1,1);glVertex2f(W,H);glTexCoord2f(0,1);glVertex2f(0,H);glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}

static void drawCircleFallback(float cx,float cy,float r){
    glColor3f(0.39f,0.39f,0.78f);
    glBegin(GL_TRIANGLE_FAN);glVertex2f(cx,cy);
    for(int i=0;i<=32;i++){float a=i*2*(float)M_PI/32;glVertex2f(cx+cosf(a)*r,cy+sinf(a)*r);}
    glEnd();glColor3f(1,1,1);
}

// ---------------------------------------------------------------------------
// Ball
// ---------------------------------------------------------------------------
struct Ball { b2Body* body; float radius; int orbIdx; bool isPlayer; };

// ---------------------------------------------------------------------------
// Run wallpaper on one monitor rect
// ---------------------------------------------------------------------------
static void runWallpaperOnRect(HWND workerW, RECT monRect) {
    int X = monRect.left;
    int Y = monRect.top;
    int W = monRect.right  - monRect.left;
    int H = monRect.bottom - monRect.top;

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)<0) return;
    IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);

    SDL_Window* win = SDL_CreateWindow("orbit-wallpaper",
        X, Y, W, H,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_SKIP_TASKBAR);
    if(!win){ IMG_Quit(); SDL_Quit(); return; }

    // Reparent into WorkerW
    SDL_SysWMinfo wmi; SDL_VERSION(&wmi.version);
    if(SDL_GetWindowWMInfo(win, &wmi)){
        HWND sdlHwnd = wmi.info.win.window;
        SetParent(sdlHwnd, workerW);
        // remove window decorations just in case
        LONG style = GetWindowLong(sdlHwnd, GWL_STYLE);
        style &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
        SetWindowLong(sdlHwnd, GWL_STYLE, style);
        SetWindowPos(sdlHwnd, HWND_BOTTOM, X, Y, W, H,
            SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }

    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,H,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);

    std::string assetDir = getExeDir();
    Texture orbTex[NUM_ORBS];
    for(int i=0;i<NUM_ORBS;i++){
        char p[600];snprintf(p,sizeof(p),"%s/orb%d.png",assetDir.c_str(),i+1);
        orbTex[i]=loadTexture(p);
    }
    Texture cubeTex={0,0,0,false};
    {const char* cs=g_settings.cube_path[0]?g_settings.cube_path:nullptr;
     if(!cs){char p[600];snprintf(p,sizeof(p),"%s/cube.png",assetDir.c_str());cubeTex=loadTexture(p);}
     else cubeTex=loadTexture(cs);}
    Texture bgTex={0,0,0,false};
    if(g_settings.bg_mode==BG_IMAGE&&g_settings.bg_image[0])
        bgTex=loadTexture(g_settings.bg_image);

    srand((unsigned)time(nullptr));
    bool running = true;

    while(running) {
        // Reload settings each cycle so live-changes apply
        loadCfg();

        int fps = g_settings.fps;
        if(fps<1)fps=1;if(fps>500)fps=500;
        if(isOnBattery()){
            int bf=g_settings.fps_battery;
            if(bf<1)bf=1;if(bf>fps)bf=fps;
            fps=bf;
        }

        float speedMult = g_settings.speed/10.0f;
        int numBalls = g_settings.orb_count; if(numBalls<1)numBalls=1;
        int dropTime = (int)(20.0f/speedMult); if(dropTime<1)dropTime=1;

        b2Vec2 gravity(0.0f, 9.8f*speedMult*3.0f);
        b2World world(gravity);

        auto makeWall=[&](float x1,float y1,float x2,float y2){
            b2BodyDef bd;bd.type=b2_staticBody;b2Body* b=world.CreateBody(&bd);
            b2EdgeShape es;es.SetTwoSided(b2Vec2(x1/PPM,y1/PPM),b2Vec2(x2/PPM,y2/PPM));
            b2FixtureDef fd;fd.shape=&es;fd.restitution=0.5f;fd.friction=0.7f;
            b->CreateFixture(&fd);return b;
        };
        makeWall(0,0,0,H);makeWall(W,0,W,H);
        b2Body* wallBottom = nullptr;
        if(!g_settings.no_ground) wallBottom=makeWall(0,H,W,H);

        std::vector<Ball> balls;
        int globalTime=0,nextSpawn=0;
        bool fillingDone=false,draining=false,playerSpawned=false;
        Uint32 allSpawnedAt=0;
        Uint32 lastTick=SDL_GetTicks();
        float physAccum=0;
        const float physStep=1.0f/fps;
        bool simRunning=true;
        bool paused=false;

        while(simRunning&&running){
            globalTime++;

            // pause/resume fullscreen check
            if(g_settings.pause_fullscreen){
                bool fs=isFullscreenAppRunning();
                if(fs&&!paused){
                    paused=true;
                    SDL_HideWindow(win);
                } else if(!fs&&paused){
                    paused=false;
                    SDL_ShowWindow(win);
                }
            }

            SDL_Event ev;
            while(SDL_PollEvent(&ev)){
                if(ev.type==SDL_QUIT){running=false;simRunning=false;}
            }

            if(paused){ SDL_Delay(200); lastTick=SDL_GetTicks(); continue; }

            // spawn
            while(nextSpawn < numBalls && globalTime >= dropTime*nextSpawn){
                float radius=(40+rand()%20)*g_settings.orb_scale;
                int chosenOrb=rand()%NUM_ORBS;
                b2BodyDef bd;bd.type=b2_dynamicBody;
                bd.position.Set(((float)W*0.8f/numBalls*(1+rand()%(numBalls*2)))/PPM,-250.0f/PPM);
                bd.angle=(float)(rand()%360)*((float)M_PI/180.0f);
                b2Body* body=world.CreateBody(&bd);
                b2FixtureDef fd;fd.density=1.0f;fd.restitution=0.5f;fd.friction=1.0f;
                b2CircleShape cs;b2PolygonShape ps;
                if(chosenOrb==10){ps.SetAsBox(radius/PPM,radius/PPM);fd.shape=&ps;}
                else{cs.m_radius=radius/PPM;fd.shape=&cs;}
                body->CreateFixture(&fd);
                body->ApplyLinearImpulse(b2Vec2((10-rand()%21)*0.05f,0),body->GetWorldCenter(),true);
                Ball ball;ball.body=body;ball.radius=radius;ball.orbIdx=chosenOrb;ball.isPlayer=false;
                balls.push_back(ball);
                nextSpawn++;
            }

            if(!playerSpawned&&nextSpawn>=numBalls/2){
                playerSpawned=true;
                if((rand()%100)<g_settings.cube_chance){
                    float cubeW=PLAYER_SIZE*g_settings.orb_scale;
                    float cubeH=PLAYER_SIZE*g_settings.orb_scale;
                    if(cubeTex.ok){
                        float tw=(float)cubeTex.w,th=(float)cubeTex.h,md=fmaxf(tw,th);
                        cubeW*=(tw/md);cubeH*=(th/md);
                    }
                    b2BodyDef bd;bd.type=b2_dynamicBody;
                    bd.position.Set((float)(rand()%W)/PPM,-(float)(200+rand()%800)/PPM);
                    bd.angle=(float)(rand()%360)*((float)M_PI/180.0f);
                    b2Body* body=world.CreateBody(&bd);
                    b2PolygonShape ps;ps.SetAsBox((cubeW*0.5f)/PPM,(cubeH*0.5f)/PPM);
                    b2FixtureDef fd;fd.shape=&ps;fd.density=1.0f;fd.restitution=0.5f;fd.friction=0.7f;
                    body->CreateFixture(&fd);
                    Ball ball;ball.body=body;ball.radius=PLAYER_SIZE*0.5f*g_settings.orb_scale;ball.orbIdx=0;ball.isPlayer=true;
                    balls.push_back(ball);
                }
            }

            if(!g_settings.no_ground&&!fillingDone&&nextSpawn>=numBalls){
                if(allSpawnedAt==0)allSpawnedAt=SDL_GetTicks();
                if(SDL_GetTicks()-allSpawnedAt>=5000+(rand()%1001)){
                    fillingDone=true;draining=true;
                    if(wallBottom){world.DestroyBody(wallBottom);wallBottom=nullptr;}
                }
            }
            if(!g_settings.no_ground&&draining){
                bool allOff=true;
                for(auto& b:balls)if(b.body->GetPosition().y*PPM<H+300){allOff=false;break;}
                if(allOff)simRunning=false;
            }
            if(g_settings.no_ground&&globalTime>numBalls*dropTime+500)simRunning=false;

            Uint32 now=SDL_GetTicks();
            physAccum+=(now-lastTick)/1000.0f;lastTick=now;
            while(physAccum>=physStep){world.Step(physStep,8,3);physAccum-=physStep;}

            // draw background
            if(g_settings.bg_mode==BG_IMAGE&&bgTex.ok){
                glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);drawBgTex(bgTex,W,H);
            } else if(g_settings.bg_mode==BG_COLOR){
                glClearColor(g_settings.bg_color[0],g_settings.bg_color[1],g_settings.bg_color[2],1);
                glClear(GL_COLOR_BUFFER_BIT);
            } else {
                glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            }

            // draw balls
            for(auto& b:balls){
                float px=b.body->GetPosition().x*PPM;
                float py=b.body->GetPosition().y*PPM;
                float ang=b.body->GetAngle()*180.0f/(float)M_PI;
                if(b.isPlayer){
                    float s=PLAYER_SIZE*g_settings.orb_scale;
                    if(cubeTex.ok){
                        float tw=(float)cubeTex.w,th=(float)cubeTex.h,md=fmaxf(tw,th);
                        drawTexturedQuad(cubeTex.id,px,py,s*(tw/md),s*(th/md),ang);
                    } else {
                        glColor3f(0.78f,0.39f,0.39f);glPushMatrix();glTranslatef(px,py,0);glRotatef(-ang,0,0,1);
                        float h2=s/2;glBegin(GL_QUADS);glVertex2f(-h2,-h2);glVertex2f(h2,-h2);glVertex2f(h2,h2);glVertex2f(-h2,h2);glEnd();
                        glPopMatrix();glColor3f(1,1,1);
                    }
                } else {
                    float d=b.radius*2;
                    if(orbTex[b.orbIdx].ok){
                        float tw=(float)orbTex[b.orbIdx].w,th=(float)orbTex[b.orbIdx].h,md=fmaxf(tw,th);
                        drawTexturedQuad(orbTex[b.orbIdx].id,px,py,d*(tw/md),d*(th/md),ang);
                    } else {
                        drawCircleFallback(px,py,b.radius);
                    }
                }
            }

            SDL_GL_SwapWindow(win);
            Uint32 elapsed=SDL_GetTicks()-now;
            Uint32 target=1000/fps;
            if(elapsed<target)SDL_Delay(target-elapsed);
        }
        balls.clear();
    }

    for(int i=0;i<NUM_ORBS;i++) if(orbTex[i].ok)glDeleteTextures(1,&orbTex[i].id);
    if(cubeTex.ok)glDeleteTextures(1,&cubeTex.id);
    if(bgTex.ok)glDeleteTextures(1,&bgTex.id);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR lpCmdLine,int){
    timeBeginPeriod(1);
    loadCfg();

    // check for --stop argument (sent by settings UI)
    if(lpCmdLine && strstr(lpCmdLine,"--stop")){
        // find and close any running wallpaper instances
        HWND hw=FindWindowA(nullptr,"orbit-wallpaper");
        while(hw){ PostMessage(hw,WM_QUIT,0,0); hw=FindWindowA(nullptr,"orbit-wallpaper"); }
        timeEndPeriod(1);
        return 0;
    }

    enumMonitors();
    HWND workerW = getDesktopWorkerW();
    if(!workerW){
        MessageBoxA(NULL,"Could not get desktop WorkerW.\nAre you on Windows 10/11?",
                    "Orbit Wallpaper",MB_OK|MB_ICONERROR);
        timeEndPeriod(1);
        return 1;
    }

    // Run on selected monitor(s)
    // For now: single thread, single monitor (or primary if -1)
    // Multi-monitor multi-thread is left as a future extension
    RECT targetRect;
    if(g_settings.monitor_index<0 || g_settings.monitor_index>=(int)g_monitors.size()){
        // all monitors: use virtual desktop bounding rect
        targetRect.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
        targetRect.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
        targetRect.right  = targetRect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        targetRect.bottom = targetRect.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    } else {
        targetRect = g_monitors[g_settings.monitor_index].rc;
    }

    runWallpaperOnRect(workerW, targetRect);

    timeEndPeriod(1);
    return 0;
}
