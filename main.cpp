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
#include <commdlg.h>
#include <timeapi.h>
#include <winhttp.h>
#include <urlmon.h>
#include <SDL2/SDL_syswm.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl2.h"
#include "logo_data.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define BG_BLACK  0
#define BG_COLOR  1
#define BG_IMAGE  2

#define FIT_STRETCH 0
#define FIT_ZOOM    1
#define FIT_TILE    2

#define WALLPAPER_MODE_WORKERW   0
#define WALLPAPER_MODE_BOTTOM    1

#define LOOP_FILL_DRAIN  0
#define LOOP_INFINITE    1

#define NUM_ORBS    11
#define PPM         40.0f
#define PLAYER_SIZE 80

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_OPEN   1001
#define ID_TRAY_TOGGLE 1002
#define ID_TRAY_EXIT   1003

struct Settings {
    int   speed;
    int   fps;
    int   bg_mode;
    float bg_color[3];
    char  bg_image[512];
    int   bg_fit;
    char  cube_path[512];
    bool  no_ground;
    float orb_scale;
    int   orb_count;
    bool  auto_update_check;
    bool  auto_update_install;
    int   cube_chance;
    int   wallpaper_mode;   // WALLPAPER_MODE_WORKERW or WALLPAPER_MODE_BOTTOM
    int   loop_mode;        // LOOP_FILL_DRAIN or LOOP_INFINITE
    bool  autostart;
};

static Settings g_settings = {
    10, 60, BG_BLACK, {0.12f,0.12f,0.12f}, "", FIT_STRETCH, "",
    false, 1.0f, 120, true, false, 50,
    WALLPAPER_MODE_WORKERW, LOOP_FILL_DRAIN, false
};

// ── globals shared between wallpaper thread and tray ─────────────────────────
static volatile bool g_wallpaperRunning = false;
static volatile bool g_wallpaperPaused  = false;
static HWND          g_wallpaperHwnd    = nullptr; // SDL window HWND
static HANDLE        g_wallpaperThread  = nullptr;
static HWND          g_trayMsgHwnd      = nullptr;
static NOTIFYICONDATAA g_nid            = {};
static bool          g_trayAdded        = false;

// ── path helpers ─────────────────────────────────────────────────────────────
static std::string getExeDir() {
    char buf[MAX_PATH]; GetModuleFileNameA(NULL,buf,MAX_PATH);
    std::string s(buf); return s.substr(0,s.rfind('\\'));
}
static std::string getCfgPath() { return getExeDir()+"\\settings.ini"; }

// ── config ───────────────────────────────────────────────────────────────────
static void loadCfg() {
    FILE* f=fopen(getCfgPath().c_str(),"r"); if(!f)return;
    char line[640];
    while(fgets(line,sizeof(line),f)){
        int iv; float fv,fv2,fv3; char sv[512];
        if(sscanf(line,"speed=%d",&iv)==1)               g_settings.speed=iv;
        if(sscanf(line,"fps=%d",&iv)==1)                 g_settings.fps=iv;
        if(sscanf(line,"bg_mode=%d",&iv)==1)             g_settings.bg_mode=iv;
        if(sscanf(line,"bg_color=%f,%f,%f",&fv,&fv2,&fv3)==3){g_settings.bg_color[0]=fv;g_settings.bg_color[1]=fv2;g_settings.bg_color[2]=fv3;}
        if(sscanf(line,"bg_fit=%d",&iv)==1)              g_settings.bg_fit=iv;
        if(sscanf(line,"no_ground=%d",&iv)==1)           g_settings.no_ground=(iv!=0);
        if(sscanf(line,"orb_scale=%f",&fv)==1)           g_settings.orb_scale=fv;
        if(sscanf(line,"orb_count=%d",&iv)==1)           g_settings.orb_count=iv;
        if(sscanf(line,"auto_update_check=%d",&iv)==1)   g_settings.auto_update_check=(iv!=0);
        if(sscanf(line,"auto_update_install=%d",&iv)==1) g_settings.auto_update_install=(iv!=0);
        if(sscanf(line,"cube_chance=%d",&iv)==1)         g_settings.cube_chance=iv;
        if(sscanf(line,"wallpaper_mode=%d",&iv)==1)      g_settings.wallpaper_mode=iv;
        if(sscanf(line,"loop_mode=%d",&iv)==1)           g_settings.loop_mode=iv;
        if(sscanf(line,"autostart=%d",&iv)==1)           g_settings.autostart=(iv!=0);
        if(sscanf(line,"bg_image=%511[^\n]",sv)==1)      strncpy(g_settings.bg_image,sv,511);
        if(sscanf(line,"cube_path=%511[^\n]",sv)==1)     strncpy(g_settings.cube_path,sv,511);
    }
    fclose(f);
}
static void saveCfg() {
    FILE* f=fopen(getCfgPath().c_str(),"w"); if(!f)return;
    fprintf(f,"speed=%d\n",g_settings.speed);
    fprintf(f,"fps=%d\n",g_settings.fps);
    fprintf(f,"bg_mode=%d\n",g_settings.bg_mode);
    fprintf(f,"bg_color=%f,%f,%f\n",g_settings.bg_color[0],g_settings.bg_color[1],g_settings.bg_color[2]);
    fprintf(f,"bg_fit=%d\n",g_settings.bg_fit);
    fprintf(f,"no_ground=%d\n",(int)g_settings.no_ground);
    fprintf(f,"orb_scale=%f\n",g_settings.orb_scale);
    fprintf(f,"orb_count=%d\n",g_settings.orb_count);
    fprintf(f,"auto_update_check=%d\n",(int)g_settings.auto_update_check);
    fprintf(f,"auto_update_install=%d\n",(int)g_settings.auto_update_install);
    fprintf(f,"cube_chance=%d\n",g_settings.cube_chance);
    fprintf(f,"wallpaper_mode=%d\n",g_settings.wallpaper_mode);
    fprintf(f,"loop_mode=%d\n",g_settings.loop_mode);
    fprintf(f,"autostart=%d\n",(int)g_settings.autostart);
    fprintf(f,"bg_image=%s\n",g_settings.bg_image);
    fprintf(f,"cube_path=%s\n",g_settings.cube_path);
    fclose(f);
}

// ── autostart registry ───────────────────────────────────────────────────────
static void setAutostart(bool enable) {
    HKEY hKey;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,KEY_SET_VALUE,&hKey)!=ERROR_SUCCESS) return;
    if(enable){
        char exePath[MAX_PATH]; GetModuleFileNameA(NULL,exePath,MAX_PATH);
        // append /w so it launches straight into wallpaper mode
        std::string val=std::string("\"")+exePath+"\" /w";
        RegSetValueExA(hKey,"OrbitWallpaper",0,REG_SZ,
            (const BYTE*)val.c_str(),(DWORD)val.size()+1);
    } else {
        RegDeleteValueA(hKey,"OrbitWallpaper");
    }
    RegCloseKey(hKey);
}

// ── updater helpers ───────────────────────────────────────────────────────────
static std::string fetchLatestTag() {
    std::string result;
    HINTERNET hSession=WinHttpOpen(L"OrbitUpdater/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hSession) return result;
    HINTERNET hConnect=WinHttpConnect(hSession,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!hConnect){WinHttpCloseHandle(hSession);return result;}
    HINTERNET hRequest=WinHttpOpenRequest(hConnect,L"GET",
        L"/repos/MalikHw/orbit-wallpaper/releases/latest",
        NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!hRequest){WinHttpCloseHandle(hConnect);WinHttpCloseHandle(hSession);return result;}
    WinHttpAddRequestHeaders(hRequest,L"User-Agent: OrbitWallpaper",-1,WINHTTP_ADDREQ_FLAG_ADD);
    if(WinHttpSendRequest(hRequest,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)
       && WinHttpReceiveResponse(hRequest,NULL)){
        char buf[4096]=""; DWORD read=0;
        WinHttpReadData(hRequest,buf,sizeof(buf)-1,&read); buf[read]=0;
        const char* p=strstr(buf,"\"tag_name\":");
        if(p){
            p+=11; while(*p=='"'||*p==' ')p++;
            char tag[64]=""; int i=0;
            while(*p&&*p!='"'&&i<63) tag[i++]=*p++;
            tag[i]=0; result=tag;
        }
    }
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}
static void launchUpdater() {
    std::string p=getExeDir()+"\\updater.exe";
    ShellExecuteA(NULL,"open",p.c_str(),NULL,NULL,SW_SHOW);
}

struct UpdateDownloadState {
    volatile float progress;
    volatile int   done;
    std::string    url, destPath;
};
struct UpdateCallback : public IBindStatusCallback {
    UpdateDownloadState* s; UpdateCallback(UpdateDownloadState* s):s(s){}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG prog,ULONG progMax,ULONG,LPCWSTR) override {
        if(progMax>0) s->progress=(float)prog/(float)progMax; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD,IBinding*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT,LPCWSTR) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*,BINDINFO*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD,DWORD,FORMATETC*,STGMEDIUM*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID,IUnknown*) override{return E_NOTIMPL;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
};
static DWORD WINAPI updateDownloadThread(void* param){
    UpdateDownloadState* s=(UpdateDownloadState*)param;
    UpdateCallback cb(s);
    HRESULT hr=URLDownloadToFileA(NULL,s->url.c_str(),s->destPath.c_str(),0,&cb);
    s->done=(hr==S_OK)?1:-1; return 0;
}
static UpdateDownloadState* g_updateDL=nullptr;

// Mesa3D
struct MesaDownloadState {
    volatile float progress;
    volatile int   done;
    std::string    url, destPath;
};
struct MesaCallback : public IBindStatusCallback {
    MesaDownloadState* s; MesaCallback(MesaDownloadState* s):s(s){}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG prog,ULONG progMax,ULONG,LPCWSTR) override {
        if(progMax>0) s->progress=(float)prog/(float)progMax; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD,IBinding*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT,LPCWSTR) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*,BINDINFO*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD,DWORD,FORMATETC*,STGMEDIUM*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID,IUnknown*) override{return E_NOTIMPL;}
    ULONG STDMETHODCALLTYPE AddRef() override{return 1;}
    ULONG STDMETHODCALLTYPE Release() override{return 1;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
};
static DWORD WINAPI mesaThread(void* param){
    MesaDownloadState* s=(MesaDownloadState*)param;
    MesaCallback cb(s);
    HRESULT hr=URLDownloadToFileA(NULL,s->url.c_str(),s->destPath.c_str(),0,&cb);
    s->done=(hr==S_OK)?1:-1; return 0;
}
static MesaDownloadState* g_mesaDL=nullptr;
static void startMesaDownload(){
    g_mesaDL=new MesaDownloadState();
    g_mesaDL->progress=0.0f; g_mesaDL->done=0;
    g_mesaDL->url="https://github.com/MalikHw/orbit-screensaver/releases/download/mesa3d/opengl32.dll";
    g_mesaDL->destPath=getExeDir()+"\\opengl32.dll";
    CreateThread(NULL,0,mesaThread,g_mesaDL,0,NULL);
}

// ── WorkerW helper ────────────────────────────────────────────────────────────
// Sends the magic message to Progman that spawns a WorkerW behind desktop icons.
// Returns the WorkerW HWND, or NULL on failure.
static HWND getWorkerW(){
    HWND progman=FindWindowA("Progman",NULL);
    if(!progman) return NULL;
    // Tell Progman to spawn WorkerW
    SendMessageTimeoutA(progman,0x052C,0,0,SMTO_NORMAL,1000,NULL);
    HWND workerW=NULL;
    EnumWindows([](HWND hwnd,LPARAM lp)->BOOL{
        HWND* out=(HWND*)lp;
        HWND shelldll=FindWindowExA(hwnd,NULL,"SHELLDLL_DefView",NULL);
        if(shelldll){
            *out=FindWindowExA(NULL,hwnd,"WorkerW",NULL);
            return FALSE;
        }
        return TRUE;
    },(LPARAM)&workerW);
    return workerW;
}

// ── texture helpers ───────────────────────────────────────────────────────────
struct Texture { GLuint id; int w,h; bool ok; };
static Texture loadTexture(const char* path){
    Texture t={0,0,0,false};
    SDL_Surface* surf=IMG_Load(path); if(!surf)return t;
    SDL_Surface* conv=SDL_ConvertSurfaceFormat(surf,SDL_PIXELFORMAT_RGBA32,0);
    SDL_FreeSurface(surf); if(!conv)return t;
    glGenTextures(1,&t.id);glBindTexture(GL_TEXTURE_2D,t.id);
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
    glTexCoord2f(0,0);glVertex2f(-hw,-hh);glTexCoord2f(1,0);glVertex2f(hw,-hh);
    glTexCoord2f(1,1);glVertex2f(hw,hh);glTexCoord2f(0,1);glVertex2f(-hw,hh);
    glEnd();
    glPopMatrix();glDisable(GL_TEXTURE_2D);glDisable(GL_BLEND);
}
static void drawBgTex(Texture& bg,int W,int H){
    if(!bg.ok)return;
    glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,bg.id);glColor4f(1,1,1,1);
    if(g_settings.bg_fit==FIT_ZOOM){
        float sx=(float)W/bg.w,sy=(float)H/bg.h,sc=fmaxf(sx,sy);
        float dw=bg.w*sc,dh=bg.h*sc,ox=(W-dw)/2,oy=(H-dh)/2;
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(ox,oy);glTexCoord2f(1,0);glVertex2f(ox+dw,oy);
        glTexCoord2f(1,1);glVertex2f(ox+dw,oy+dh);glTexCoord2f(0,1);glVertex2f(ox,oy+dh);glEnd();
    } else if(g_settings.bg_fit==FIT_TILE){
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        float tx=(float)W/bg.w,ty=(float)H/bg.h;
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(tx,0);glVertex2f(W,0);
        glTexCoord2f(tx,ty);glVertex2f(W,H);glTexCoord2f(0,ty);glVertex2f(0,H);glEnd();
    } else {
        glBegin(GL_QUADS);glTexCoord2f(0,0);glVertex2f(0,0);glTexCoord2f(1,0);glVertex2f(W,0);
        glTexCoord2f(1,1);glVertex2f(W,H);glTexCoord2f(0,1);glVertex2f(0,H);glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void drawCircleFallback(float cx,float cy,float r){
    glColor3f(0.39f,0.39f,0.78f);
    glBegin(GL_TRIANGLE_FAN);glVertex2f(cx,cy);
    for(int i=0;i<=32;i++){float a=i*2*(float)M_PI/32;glVertex2f(cx+cosf(a)*r,cy+sinf(a)*r);}
    glEnd();glColor3f(1,1,1);
}

// ── ball struct ───────────────────────────────────────────────────────────────
struct Ball { b2Body* body; float radius; int orbIdx; bool isPlayer; };

// ── tray icon ─────────────────────────────────────────────────────────────────
static LRESULT CALLBACK trayWndProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==WM_TRAYICON && lp==WM_RBUTTONUP){
        POINT pt; GetCursorPos(&pt);
        HMENU menu=CreatePopupMenu();
        AppendMenuA(menu,MF_STRING,ID_TRAY_OPEN,"Open Settings");
        AppendMenuA(menu,MF_STRING|( g_wallpaperPaused?MF_CHECKED:0 ),ID_TRAY_TOGGLE,
            g_wallpaperPaused?"Resume Wallpaper":"Pause Wallpaper");
        AppendMenuA(menu,MF_SEPARATOR,0,NULL);
        AppendMenuA(menu,MF_STRING,ID_TRAY_EXIT,"Exit");
        SetForegroundWindow(hwnd);
        int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd,NULL);
        DestroyMenu(menu);
        if(cmd==ID_TRAY_OPEN){
            // signal settings window to show — post to main thread via PostThreadMessage
            PostThreadMessage(GetCurrentThreadId(),WM_USER+10,0,0);
        } else if(cmd==ID_TRAY_TOGGLE){
            g_wallpaperPaused=!g_wallpaperPaused;
        } else if(cmd==ID_TRAY_EXIT){
            g_wallpaperRunning=false;
            PostQuitMessage(0);
        }
    }
    if(msg==WM_TRAYICON && lp==WM_LBUTTONDBLCLK){
        PostThreadMessage(GetCurrentThreadId(),WM_USER+10,0,0);
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

static void addTrayIcon(HINSTANCE hInst){
    WNDCLASSEXA wc={};
    wc.cbSize=sizeof(wc);wc.lpfnWndProc=trayWndProc;
    wc.hInstance=hInst;wc.lpszClassName="OrbitTray";
    RegisterClassExA(&wc);
    g_trayMsgHwnd=CreateWindowExA(0,"OrbitTray","",0,0,0,0,0,HWND_MESSAGE,NULL,hInst,NULL);

    ZeroMemory(&g_nid,sizeof(g_nid));
    g_nid.cbSize=sizeof(g_nid);
    g_nid.hWnd=g_trayMsgHwnd;
    g_nid.uID=1;
    g_nid.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    g_nid.uCallbackMessage=WM_TRAYICON;
    g_nid.hIcon=LoadIconA(hInst,MAKEINTRESOURCEA(1));
    if(!g_nid.hIcon) g_nid.hIcon=LoadIconA(NULL,IDI_APPLICATION);
    strncpy(g_nid.szTip,"Orbit Wallpaper",sizeof(g_nid.szTip)-1);
    Shell_NotifyIconA(NIM_ADD,&g_nid);
    g_trayAdded=true;
}

static void removeTrayIcon(){
    if(g_trayAdded){ Shell_NotifyIconA(NIM_DELETE,&g_nid); g_trayAdded=false; }
}

// ── wallpaper thread ──────────────────────────────────────────────────────────
struct WallpaperThreadParams { Settings settings; };

static DWORD WINAPI wallpaperThread(void* param){
    WallpaperThreadParams* p=(WallpaperThreadParams*)param;
    Settings cfg=p->settings;
    delete p;

    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)<0){ g_wallpaperRunning=false; return 1; }
    IMG_Init(IMG_INIT_PNG|IMG_INIT_JPG);

    SDL_DisplayMode dm; SDL_GetCurrentDisplayMode(0,&dm);
    int W=dm.w, H=dm.h;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,8);SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);

    Uint32 winFlags=SDL_WINDOW_OPENGL|SDL_WINDOW_BORDERLESS;

    SDL_Window* win=SDL_CreateWindow("orbit-wallpaper",0,0,W,H,winFlags);
    if(!win){ SDL_Quit(); g_wallpaperRunning=false; return 1; }
    SDL_ShowCursor(SDL_DISABLE);

    // get native HWND
    SDL_SysWMinfo wminfo; SDL_VERSION(&wminfo.version);
    SDL_GetWindowWMInfo(win,&wminfo);
    HWND hwnd=wminfo.info.win.window;
    g_wallpaperHwnd=hwnd;

    if(cfg.wallpaper_mode==WALLPAPER_MODE_WORKERW){
        HWND workerW=getWorkerW();
        if(workerW){
            SetParent(hwnd,workerW);
            // remove WS_POPUP, add WS_CHILD so it behaves as a child of WorkerW
            LONG style=GetWindowLongA(hwnd,GWL_STYLE);
            style=(style&~WS_POPUP)|WS_CHILD;
            SetWindowLongA(hwnd,GWL_STYLE,style);
            SetWindowPos(hwnd,NULL,0,0,W,H,SWP_NOZORDER|SWP_FRAMECHANGED);
        }
        // if no WorkerW found, fall through to always-on-bottom behaviour
    } else {
        // Always-on-bottom: place window at HWND_BOTTOM and keep it there
        SetWindowPos(hwnd,HWND_BOTTOM,0,0,W,H,SWP_NOACTIVATE);
        SetWindowLongA(hwnd,GWL_EXSTYLE,
            GetWindowLongA(hwnd,GWL_EXSTYLE)|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW);
    }

    SDL_GLContext ctx=SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,W,H,0,-1,1);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);

    std::string assetDir=getExeDir();
    Texture orbTex[NUM_ORBS];
    for(int i=0;i<NUM_ORBS;i++){
        char path[600];snprintf(path,sizeof(path),"%s/orb%d.png",assetDir.c_str(),i+1);
        orbTex[i]=loadTexture(path);
    }
    Texture cubeTex={0,0,0,false};
    {
        const char* cs=cfg.cube_path[0]?cfg.cube_path:nullptr;
        if(!cs){char path[600];snprintf(path,sizeof(path),"%s/cube.png",assetDir.c_str());cubeTex=loadTexture(path);}
        else cubeTex=loadTexture(cs);
    }
    Texture bgTex={0,0,0,false};
    if(cfg.bg_mode==BG_IMAGE&&cfg.bg_image[0]) bgTex=loadTexture(cfg.bg_image);

    srand((unsigned)time(nullptr));

    while(g_wallpaperRunning){
        // check if settings changed while running — reload snapshot of settings
        cfg = g_settings;

        int fps=cfg.fps; if(fps<1)fps=1; if(fps>500)fps=500;
        float speedMult=cfg.speed/10.0f;
        int numBalls=cfg.orb_count; if(numBalls<1)numBalls=1;
        int dropTime=(int)(20.0f/speedMult); if(dropTime<1)dropTime=1;

        b2Vec2 gravity(0.0f,9.8f*speedMult*3.0f);
        b2World world(gravity);

        auto makeWall=[&](float x1,float y1,float x2,float y2){
            b2BodyDef bd;bd.type=b2_staticBody;b2Body* b=world.CreateBody(&bd);
            b2EdgeShape es;es.SetTwoSided(b2Vec2(x1/PPM,y1/PPM),b2Vec2(x2/PPM,y2/PPM));
            b2FixtureDef fd;fd.shape=&es;fd.restitution=0.5f;fd.friction=0.7f;
            b->CreateFixture(&fd);return b;
        };
        makeWall(0,0,0,H);makeWall(W,0,W,H);
        b2Body* wallBottom=nullptr;
        bool hasGround = !cfg.no_ground && cfg.loop_mode==LOOP_FILL_DRAIN;
        if(hasGround) wallBottom=makeWall(0,H,W,H);

        std::vector<Ball> balls;
        int globalTime=0;
        bool fillingDone=false,draining=false;
        int nextSpawn=0;
        bool playerSpawned=false;
        Uint32 allSpawnedAt=0;
        Uint32 lastTick=SDL_GetTicks();
        float physAccum=0;
        const float physStep=1.0f/fps;
        bool simRunning=true;

        while(simRunning && g_wallpaperRunning){
            globalTime++;

            // keep always-on-bottom in bottom mode
            if(cfg.wallpaper_mode==WALLPAPER_MODE_BOTTOM){
                SetWindowPos(hwnd,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            }

            // process SDL events (we don't quit on input — it's a wallpaper)
            SDL_Event ev;
            while(SDL_PollEvent(&ev)){
                if(ev.type==SDL_QUIT){ g_wallpaperRunning=false; simRunning=false; }
            }

            if(g_wallpaperPaused){
                SDL_Delay(50); lastTick=SDL_GetTicks(); continue;
            }

            // spawn balls
            while(nextSpawn < numBalls && globalTime >= dropTime * nextSpawn){
                float radius=(40+rand()%20)*cfg.orb_scale;
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

            // spawn cube/player
            if(!playerSpawned && nextSpawn>=numBalls/2){
                playerSpawned=true;
                if((rand()%100)<cfg.cube_chance){
                    float cubeW=PLAYER_SIZE*cfg.orb_scale,cubeH=PLAYER_SIZE*cfg.orb_scale;
                    if(cubeTex.ok){
                        float tw=(float)cubeTex.w,th=(float)cubeTex.h,mx=fmaxf(tw,th);
                        cubeW*=(tw/mx);cubeH*=(th/mx);
                    }
                    b2BodyDef bd;bd.type=b2_dynamicBody;
                    bd.position.Set((float)(rand()%W)/PPM,-(float)(200+rand()%800)/PPM);
                    bd.angle=(float)(rand()%360)*((float)M_PI/180.0f);
                    b2Body* body=world.CreateBody(&bd);
                    b2PolygonShape ps;ps.SetAsBox((cubeW*0.5f)/PPM,(cubeH*0.5f)/PPM);
                    b2FixtureDef fd;fd.shape=&ps;fd.density=1.0f;fd.restitution=0.5f;fd.friction=0.7f;
                    body->CreateFixture(&fd);
                    Ball ball;ball.body=body;ball.radius=PLAYER_SIZE*0.5f*cfg.orb_scale;
                    ball.orbIdx=0;ball.isPlayer=true;
                    balls.push_back(ball);
                }
            }

            // fill/drain cycle (only in LOOP_FILL_DRAIN with ground)
            if(hasGround && !fillingDone && nextSpawn>=numBalls){
                if(allSpawnedAt==0) allSpawnedAt=SDL_GetTicks();
                Uint32 delay=5000+(rand()%1001);
                if(SDL_GetTicks()-allSpawnedAt>=delay){
                    fillingDone=true;draining=true;
                    if(wallBottom){world.DestroyBody(wallBottom);wallBottom=nullptr;}
                }
            }
            if(hasGround && draining){
                bool allOff=true;
                for(auto& b:balls) if(b.body->GetPosition().y*PPM < H+300){allOff=false;break;}
                if(allOff) simRunning=false; // restart loop
            }

            // infinite fall: just keep running, restart after a while to avoid spiral physics mess
            if(cfg.loop_mode==LOOP_INFINITE && globalTime > numBalls*dropTime+600)
                simRunning=false;

            // physics step
            Uint32 now=SDL_GetTicks();
            physAccum+=(now-lastTick)/1000.0f;lastTick=now;
            while(physAccum>=physStep){world.Step(physStep,8,3);physAccum-=physStep;}

            // background
            if(cfg.bg_mode==BG_IMAGE && bgTex.ok){
                glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);drawBgTex(bgTex,W,H);
            } else if(cfg.bg_mode==BG_COLOR){
                glClearColor(cfg.bg_color[0],cfg.bg_color[1],cfg.bg_color[2],1);glClear(GL_COLOR_BUFFER_BIT);
            } else {
                glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
            }

            // draw balls
            for(auto& b:balls){
                float px=b.body->GetPosition().x*PPM,py=b.body->GetPosition().y*PPM;
                float ang=b.body->GetAngle()*180.0f/(float)M_PI;
                if(b.isPlayer){
                    float s=PLAYER_SIZE*cfg.orb_scale;
                    if(cubeTex.ok){
                        float tw=(float)cubeTex.w,th=(float)cubeTex.h,mx=fmaxf(tw,th);
                        drawTexturedQuad(cubeTex.id,px,py,s*(tw/mx),s*(th/mx),ang);
                    } else {
                        glColor3f(0.78f,0.39f,0.39f);glPushMatrix();glTranslatef(px,py,0);
                        glRotatef(-ang,0,0,1);float h2=s/2;
                        glBegin(GL_QUADS);glVertex2f(-h2,-h2);glVertex2f(h2,-h2);
                        glVertex2f(h2,h2);glVertex2f(-h2,h2);glEnd();
                        glPopMatrix();glColor3f(1,1,1);
                    }
                } else {
                    float d=b.radius*2;
                    if(orbTex[b.orbIdx].ok){
                        float tw=(float)orbTex[b.orbIdx].w,th=(float)orbTex[b.orbIdx].h,mx=fmaxf(tw,th);
                        drawTexturedQuad(orbTex[b.orbIdx].id,px,py,d*(tw/mx),d*(th/mx),ang);
                    } else drawCircleFallback(px,py,b.radius);
                }
            }

            SDL_GL_SwapWindow(win);
            Uint32 elapsed=SDL_GetTicks()-now;
            Uint32 target=1000/fps;
            if(elapsed<target) SDL_Delay(target-elapsed);
        }
        balls.clear();
    }

    for(int i=0;i<NUM_ORBS;i++) if(orbTex[i].ok)glDeleteTextures(1,&orbTex[i].id);
    if(cubeTex.ok)glDeleteTextures(1,&cubeTex.id);
    if(bgTex.ok)glDeleteTextures(1,&bgTex.id);
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);
    IMG_Quit();SDL_Quit();
    g_wallpaperRunning=false;
    g_wallpaperHwnd=nullptr;
    return 0;
}

static void startWallpaper(){
    if(g_wallpaperRunning) return;
    g_wallpaperRunning=true;
    g_wallpaperPaused=false;
    WallpaperThreadParams* p=new WallpaperThreadParams();
    p->settings=g_settings;
    g_wallpaperThread=CreateThread(NULL,0,wallpaperThread,p,0,NULL);
}
static void stopWallpaper(){
    g_wallpaperRunning=false;
    if(g_wallpaperThread){
        WaitForSingleObject(g_wallpaperThread,5000);
        CloseHandle(g_wallpaperThread);
        g_wallpaperThread=nullptr;
    }
}
static void restartWallpaper(){
    stopWallpaper();
    Sleep(300);
    startWallpaper();
}

// ── settings UI ───────────────────────────────────────────────────────────────
static void runImGuiSettings(HINSTANCE hInst){
    if(SDL_Init(SDL_INIT_VIDEO)<0) return;
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_Window* win=SDL_CreateWindow("Orbit Wallpaper - Settings",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,480,640,
        SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    SDL_GLContext ctx=SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();io.IniFilename=nullptr;
    if(!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf",16.0f))
        if(!io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\cour.ttf",16.0f))
            io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark();
    ImGuiStyle& style=ImGui::GetStyle();
    style.WindowRounding=6;style.FrameRounding=4;style.GrabRounding=4;
    style.Colors[ImGuiCol_Button]       =ImVec4(0.26f,0.59f,0.98f,0.5f);
    style.Colors[ImGuiCol_ButtonHovered]=ImVec4(0.26f,0.59f,0.98f,0.8f);
    ImGui_ImplSDL2_InitForOpenGL(win,ctx);
    ImGui_ImplOpenGL2_Init();
    {ImGui_ImplOpenGL2_NewFrame();ImGui_ImplSDL2_NewFrame();ImGui::NewFrame();ImGui::EndFrame();}

    // logo texture
    GLuint logoTex=0;
    {
        SDL_RWops* rw=SDL_RWFromConstMem(logo_png,logo_png_len);
        SDL_Surface* surf=IMG_LoadPNG_RW(rw);SDL_RWclose(rw);
        if(surf){
            SDL_Surface* conv=SDL_ConvertSurfaceFormat(surf,SDL_PIXELFORMAT_RGBA32,0);
            SDL_FreeSurface(surf);
            if(conv){
                glGenTextures(1,&logoTex);glBindTexture(GL_TEXTURE_2D,logoTex);
                glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,conv->w,conv->h,0,GL_RGBA,GL_UNSIGNED_BYTE,conv->pixels);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                SDL_FreeSurface(conv);
            }
        }
    }

    const char* bgNames[]={"Black","Custom Color","Image"};
    const char* fitNames[]={"Stretch","Zoom","Tile"};
    const char* wpmNames[]={"WorkerW (behind icons)","Always on Bottom"};
    const char* loopNames[]={"Fill then Drain","Infinite Fall"};

    static std::string latestTag="";
    static bool updateChecked=false,checkingNow=false;
    static bool showWorkerWWarning=false;

    // close-to-tray flag
    bool closeToTray=true; // always minimize to tray from settings window
    bool running=true;

    // handle tray "Open Settings" message
    MSG winMsg;

    while(running){
        // pump tray messages
        while(PeekMessage(&winMsg,NULL,0,0,PM_REMOVE)){
            if(winMsg.message==WM_QUIT){ running=false; break; }
            if(winMsg.message==WM_USER+10){ /* already in settings, ignore */ }
            TranslateMessage(&winMsg);DispatchMessage(&winMsg);
        }

        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if(ev.type==SDL_QUIT){
                if(closeToTray){
                    // hide settings window but keep running
                    SDL_HideWindow(win);
                    // don't set running=false — wait for tray open or quit
                } else {
                    running=false;
                }
            }
        }

        // if window is hidden, just sleep and pump messages
        Uint32 wflags=SDL_GetWindowFlags(win);
        if(!(wflags & SDL_WINDOW_SHOWN)){
            // wait for tray open signal
            if(PeekMessage(&winMsg,NULL,WM_USER+10,WM_USER+10,PM_REMOVE)){
                SDL_ShowWindow(win); SDL_RaiseWindow(win);
            }
            Sleep(30); continue;
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int W,H;SDL_GetWindowSize(win,&W,&H);
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2((float)W,(float)H));
        ImGui::Begin("##main",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove);

        // header
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"ORBIT WALLPAPER");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f),"v%s",APP_VERSION);
        ImGui::Separator();ImGui::Spacing();

        // ── wallpaper mode ────────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1.0f),"Wallpaper");
        ImGui::SetNextItemWidth(220);
        int prevWpMode=g_settings.wallpaper_mode;
        if(ImGui::Combo("Mode##wp",&g_settings.wallpaper_mode,wpmNames,2)){
            if(g_settings.wallpaper_mode==WALLPAPER_MODE_WORKERW && !showWorkerWWarning){
                showWorkerWWarning=true;
                ImGui::OpenPopup("WorkerW Warning");
            }
        }
        if(ImGui::BeginPopupModal("WorkerW Warning",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextColored(ImVec4(1,0.8f,0,1),"WorkerW mode uses an undocumented Windows trick");
            ImGui::Text("(SendMessageTimeout to Progman).");
            ImGui::Text("Some antivirus software may flag this as suspicious.");
            ImGui::Text("The wallpaper will still work correctly.");
            ImGui::Spacing();
            if(ImGui::Button("Understood",ImVec2(120,24))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SameLine();ImGui::TextDisabled("(?)");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("WorkerW = renders behind desktop icons\nAlways on Bottom = renders above desktop but below windows");

        ImGui::SetNextItemWidth(220);
        ImGui::Combo("Loop##loop",&g_settings.loop_mode,loopNames,2);
        ImGui::SameLine();ImGui::TextDisabled("(?)");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Fill then Drain: orbs fill up, floor drops, they fall away, repeats.\nInfinite Fall: orbs fall forever with no ground.");

        ImGui::Spacing();
        bool wpRunning=g_wallpaperRunning;
        if(wpRunning){
            if(g_wallpaperPaused){
                if(ImGui::Button("Resume",ImVec2(90,26))) g_wallpaperPaused=false;
            } else {
                if(ImGui::Button("Pause",ImVec2(90,26))) g_wallpaperPaused=true;
            }
            ImGui::SameLine();
            if(ImGui::Button("Restart",ImVec2(90,26))) restartWallpaper();
            ImGui::SameLine();
            if(ImGui::Button("Stop",ImVec2(70,26))) stopWallpaper();
        } else {
            if(ImGui::Button("Start Wallpaper",ImVec2(160,26))) startWallpaper();
        }

        // autostart
        ImGui::SameLine();
        bool prevAutostart=g_settings.autostart;
        ImGui::Checkbox("Autostart",&g_settings.autostart);
        if(g_settings.autostart!=prevAutostart) setAutostart(g_settings.autostart);

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // ── physics / display ─────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1.0f),"Physics & Display");
        ImGui::SliderInt("Speed",&g_settings.speed,1,20);
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("FPS",&g_settings.fps,0);
        if(g_settings.fps<1)g_settings.fps=1;if(g_settings.fps>500)g_settings.fps=500;
        ImGui::SameLine();
        int fpsP[]={30,60,120,144,240,500};
        for(int fp:fpsP){char l[8];sprintf(l,"%d",fp);if(ImGui::SmallButton(l))g_settings.fps=fp;ImGui::SameLine();}
        ImGui::NewLine();

        static bool broPopupPending=false,wasOrbFieldFocused=false;
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Orb count",&g_settings.orb_count,1);
        if(g_settings.orb_count<1)g_settings.orb_count=1;
        bool orbFieldFocused=ImGui::IsItemActive();
        if(wasOrbFieldFocused&&!orbFieldFocused&&g_settings.orb_count<20) broPopupPending=true;
        wasOrbFieldFocused=orbFieldFocused;
        ImGui::SameLine();
        if(ImGui::SmallButton("Low"))  g_settings.orb_count=30;ImGui::SameLine();
        if(ImGui::SmallButton("Med"))  g_settings.orb_count=80;ImGui::SameLine();
        if(ImGui::SmallButton("High")) g_settings.orb_count=120;ImGui::SameLine();
        if(ImGui::SmallButton("Giga")) g_settings.orb_count=210;
        if(broPopupPending){ImGui::OpenPopup("bro");broPopupPending=false;}
        if(ImGui::BeginPopupModal("bro",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("bro what the fuck? :sob:, why is even THAT!?");
            if(ImGui::Button("yes")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SetNextItemWidth(180);
        ImGui::SliderFloat("Orb size",&g_settings.orb_scale,0.3f,3.0f);
        ImGui::SameLine();
        if(ImGui::SmallButton("S"))g_settings.orb_scale=0.5f;ImGui::SameLine();
        if(ImGui::SmallButton("N"))g_settings.orb_scale=1.0f;ImGui::SameLine();
        if(ImGui::SmallButton("L"))g_settings.orb_scale=1.5f;

        ImGui::SetNextItemWidth(180);
        ImGui::SliderInt("Cube chance",&g_settings.cube_chance,0,100);
        ImGui::SameLine();ImGui::TextDisabled("%%");
        ImGui::Checkbox("No ground (infinite fall physics)",&g_settings.no_ground);
        ImGui::Spacing();

        // ── cube ──────────────────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1.0f),"Cube PNG");
        ImGui::SetNextItemWidth(280);
        ImGui::InputText("##cube",g_settings.cube_path,sizeof(g_settings.cube_path));
        ImGui::SameLine();
        if(ImGui::Button("Browse##cube")){
            OPENFILENAMEA ofn={};char buf[512]="";
            ofn.lStructSize=sizeof(ofn);ofn.lpstrFilter="PNG\0*.png\0All\0*.*\0";
            ofn.lpstrFile=buf;ofn.nMaxFile=sizeof(buf);ofn.Flags=OFN_FILEMUSTEXIST;
            if(GetOpenFileNameA(&ofn))strncpy(g_settings.cube_path,buf,511);
        }
        ImGui::Spacing();

        // ── background ────────────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.9f,0.7f,0.3f,1.0f),"Background");
        ImGui::SetNextItemWidth(200);
        ImGui::Combo("##bg",&g_settings.bg_mode,bgNames,3);
        if(g_settings.bg_mode==BG_COLOR)
            ImGui::ColorEdit3("Color",&g_settings.bg_color[0]);
        if(g_settings.bg_mode==BG_IMAGE){
            ImGui::SetNextItemWidth(280);
            ImGui::InputText("##img",g_settings.bg_image,sizeof(g_settings.bg_image));
            ImGui::SameLine();
            if(ImGui::Button("Browse##img")){
                OPENFILENAMEA ofn={};char buf[512]="";
                ofn.lStructSize=sizeof(ofn);ofn.lpstrFilter="Images\0*.png;*.jpg;*.bmp\0All\0*.*\0";
                ofn.lpstrFile=buf;ofn.nMaxFile=sizeof(buf);ofn.Flags=OFN_FILEMUSTEXIST;
                if(GetOpenFileNameA(&ofn))strncpy(g_settings.bg_image,buf,511);
            }
            ImGui::SetNextItemWidth(120);
            ImGui::Combo("Fit",&g_settings.bg_fit,fitNames,3);
        }

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // ── updates ───────────────────────────────────────────────────────────
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"Updates");
        ImGui::Spacing();
        if(g_updateDL && g_updateDL->done==0){
            char lbl[16];snprintf(lbl,sizeof(lbl),"%.0f%%",g_updateDL->progress*100.0f);
            ImGui::ProgressBar(g_updateDL->progress,ImVec2(220,20),lbl);
            ImGui::SameLine();ImGui::TextColored(ImVec4(1,1,0,1),"Downloading update...");
        } else if(g_updateDL && g_updateDL->done==1){
            ImGui::TextColored(ImVec4(0,1,0,1),"Downloaded! Launching updater...");
            saveCfg();launchUpdater();running=false;
        } else if(g_updateDL && g_updateDL->done==-1){
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"Download failed!");
        } else {
            if(checkingNow){
                ImGui::TextColored(ImVec4(1,1,0,1),"Checking...");
            } else if(!updateChecked){
                if(ImGui::Button("Check for updates",ImVec2(200,24))){
                    checkingNow=true;
                    latestTag=fetchLatestTag();
                    updateChecked=true;checkingNow=false;
                }
            } else if(latestTag.empty()||latestTag==APP_VERSION){
                ImGui::TextColored(ImVec4(0,1,0,1),"You're up to date! (%s)",APP_VERSION);
            } else {
                ImGui::TextColored(ImVec4(1,0.5f,0,1),"Update available: %s",latestTag.c_str());
                ImGui::SameLine();
                if(ImGui::Button("Install")){
                    g_updateDL=new UpdateDownloadState();
                    g_updateDL->progress=0.0f;g_updateDL->done=0;
                    g_updateDL->url="https://github.com/MalikHw/orbit-wallpaper/releases/download/"+latestTag+"/orbit-update.zip";
                    g_updateDL->destPath=getExeDir()+"\\orbit-update.zip";
                    CreateThread(NULL,0,updateDownloadThread,g_updateDL,0,NULL);
                }
            }
        }
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // ── save / apply ──────────────────────────────────────────────────────
        if(ImGui::Button("Save",ImVec2(100,30))){saveCfg();ImGui::OpenPopup("Saved!");}
        ImGui::SameLine();
        if(ImGui::Button("Save & Apply",ImVec2(130,30))){saveCfg();restartWallpaper();}
        ImGui::SameLine();
        if(ImGui::Button("Minimize to Tray",ImVec2(150,30))){SDL_HideWindow(win);}
        if(ImGui::BeginPopupModal("Saved!",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Settings saved!");
            if(ImGui::Button("OK"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // ── mesa3d ────────────────────────────────────────────────────────────
        if(g_mesaDL && g_mesaDL->done==0){
            char lbl[16];snprintf(lbl,sizeof(lbl),"%.0f%%",g_mesaDL->progress*100.0f);
            ImGui::ProgressBar(g_mesaDL->progress,ImVec2(180,20),lbl);
            ImGui::SameLine();ImGui::TextColored(ImVec4(1,1,0,1),"Downloading...");
        } else if(g_mesaDL && g_mesaDL->done==1){
            ImGui::TextColored(ImVec4(0,1,0,1),"Mesa3D installed!");
        } else if(g_mesaDL && g_mesaDL->done==-1){
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"Download failed!");
            ImGui::SameLine();
            if(ImGui::SmallButton("Retry")){delete g_mesaDL;g_mesaDL=nullptr;startMesaDownload();}
        } else {
            if(ImGui::Button("Install Mesa3D",ImVec2(180,24)))ImGui::OpenPopup("mesa_confirm");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Software OpenGL renderer - only if you get a white square!");
        }
        if(ImGui::BeginPopupModal("mesa_confirm",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextColored(ImVec4(1,0.8f,0,1),"Only use this if you get a white square,");
            ImGui::Text("or you don't want GPU usage.");ImGui::Spacing();
            if(ImGui::Button("Download",ImVec2(100,24))){startMesaDownload();ImGui::CloseCurrentPopup();}
            ImGui::SameLine();
            if(ImGui::Button("Cancel",ImVec2(80,24)))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f),"by MalikHw47");ImGui::Spacing();
        if(ImGui::SmallButton("MalikHw47"))ShellExecuteA(0,"open","https://malikhw.github.io",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("youtube"))ShellExecuteA(0,"open","https://youtube.com/@MalikHw47",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("github"))ShellExecuteA(0,"open","https://github.com/MalikHw",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("twitch"))ShellExecuteA(0,"open","https://twitch.tv/MalikHw47",0,0,SW_SHOW);
        ImGui::Spacing();
        if(ImGui::Button("Join my server",ImVec2(180,22)))ShellExecuteA(0,"open","https://discord.gg/G9bZ92eg2n",0,0,SW_SHOW);
        ImGui::SameLine();
        if(ImGui::Button("Get me a gift!",ImVec2(150,22)))ShellExecuteA(0,"open","https://throne.com/MalikHw47",0,0,SW_SHOW);
        if(ImGui::Button("Get me MegaHack!",ImVec2(180,22)))ShellExecuteA(0,"open","https://absolllute.com/store/mega_hack?gift=1",0,0,SW_SHOW);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("My discord is MalikHw btw");
        ImGui::SameLine();
        if(ImGui::Button("Donate!",ImVec2(150,22)))ShellExecuteA(0,"open","https://ko-fi.com/malikhw47",0,0,SW_SHOW);

        // logo watermark
        if(logoTex){
            const float logoSize=56.0f;
            ImVec2 wp2=ImGui::GetWindowPos();
            ImVec2 ws=ImGui::GetWindowSize();
            ImVec2 logoPos=ImVec2(wp2.x+ws.x-logoSize-6,wp2.y+ws.y-logoSize-6);
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(uintptr_t)logoTex,logoPos,
                ImVec2(logoPos.x+logoSize,logoPos.y+logoSize),
                ImVec2(0,0),ImVec2(1,1),IM_COL32(255,255,255,180));
        }

        ImGui::End();
        ImGui::Render();
        glViewport(0,0,W,H);
        glClearColor(0.1f,0.1f,0.1f,1);glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(win);
    }

    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if(logoTex)glDeleteTextures(1,&logoTex);
    SDL_GL_DeleteContext(ctx);SDL_DestroyWindow(win);
    SDL_Quit();
}

// ── entry point ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst,HINSTANCE,LPSTR,int){
    timeBeginPeriod(1);
    loadCfg();

    // swap pending updater
    {
        std::string exeDir=getExeDir();
        std::string pending=exeDir+"\\updater.exe.pending";
        std::string real=exeDir+"\\updater.exe";
        if(GetFileAttributesA(pending.c_str())!=INVALID_FILE_ATTRIBUTES){
            DeleteFileA(real.c_str());MoveFileA(pending.c_str(),real.c_str());
        }
    }

    int argc;LPWSTR* wargv=CommandLineToArgvW(GetCommandLineW(),&argc);
    bool doWallpaper=false,doSettings=false;
    for(int i=1;i<argc;i++){
        char a[64];WideCharToMultiByte(CP_ACP,0,wargv[i],-1,a,sizeof(a),0,0);
        for(char* pp=a;*pp;pp++)*pp=tolower(*pp);
        if(!strncmp(a,"/w",2)||!strncmp(a,"-w",2)) doWallpaper=true;
        if(!strncmp(a,"/s",2)||!strncmp(a,"-s",2)) doSettings=true;
    }
    LocalFree(wargv);

    // no args = open settings and start wallpaper
    if(!doWallpaper&&!doSettings){ doSettings=true; }

    addTrayIcon(hInst);

    // always start wallpaper
    startWallpaper();

    if(doSettings||(!doWallpaper)){
        // blocks until settings window is closed/quit from tray
        runImGuiSettings(hInst);
    } else {
        // /w mode: headless, just keep message loop alive for tray
        MSG msg;
        while(GetMessage(&msg,NULL,0,0)){
            if(msg.message==WM_USER+10){
                // tray asked to open settings
                runImGuiSettings(hInst);
            }
            if(msg.message==WM_QUIT) break;
            TranslateMessage(&msg);DispatchMessage(&msg);
        }
    }

    stopWallpaper();
    removeTrayIcon();
    timeEndPeriod(1);
    return 0;
}
