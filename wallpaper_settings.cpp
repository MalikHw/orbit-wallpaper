#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <SDL2/SDL_image.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commdlg.h>
#include <winhttp.h>
#include <urlmon.h>
#include <tlhelp32.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl2.h"
#include "logo_data.h"

#ifndef APP_VERSION
#define APP_VERSION "dev"
#endif

#define BG_BLACK     0
#define BG_COLOR     1
#define BG_IMAGE     2

#define FIT_STRETCH 0
#define FIT_ZOOM    1
#define FIT_TILE    2

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
static std::string getWallpaperExePath() { return getExeDir()+"\\orbit_wallpaper.exe"; }

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

static void saveCfg() {
    FILE* f=fopen(getCfgPath().c_str(),"w"); if(!f)return;
    fprintf(f,"speed=%d\n",g_settings.speed);
    fprintf(f,"fps=%d\n",g_settings.fps);
    fprintf(f,"fps_battery=%d\n",g_settings.fps_battery);
    fprintf(f,"bg_mode=%d\n",g_settings.bg_mode);
    fprintf(f,"bg_color=%f,%f,%f\n",g_settings.bg_color[0],g_settings.bg_color[1],g_settings.bg_color[2]);
    fprintf(f,"bg_fit=%d\n",g_settings.bg_fit);
    fprintf(f,"no_ground=%d\n",(int)g_settings.no_ground);
    fprintf(f,"orb_scale=%f\n",g_settings.orb_scale);
    fprintf(f,"orb_count=%d\n",g_settings.orb_count);
    fprintf(f,"cube_chance=%d\n",g_settings.cube_chance);
    fprintf(f,"pause_fullscreen=%d\n",(int)g_settings.pause_fullscreen);
    fprintf(f,"start_with_windows=%d\n",(int)g_settings.start_with_windows);
    fprintf(f,"monitor_index=%d\n",g_settings.monitor_index);
    fprintf(f,"bg_image=%s\n",g_settings.bg_image);
    fprintf(f,"cube_path=%s\n",g_settings.cube_path);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Autostart registry
// ---------------------------------------------------------------------------
static void setAutostart(bool enable) {
    HKEY hKey;
    RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey);
    if(enable){
        std::string path = "\"" + getWallpaperExePath() + "\"";
        RegSetValueExA(hKey,"OrbitWallpaper",0,REG_SZ,
            (const BYTE*)path.c_str(),(DWORD)path.size()+1);
    } else {
        RegDeleteValueA(hKey,"OrbitWallpaper");
    }
    RegCloseKey(hKey);
}

// ---------------------------------------------------------------------------
// Wallpaper process control
// ---------------------------------------------------------------------------
static bool isWallpaperRunning() {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32 pe;pe.dwSize=sizeof(pe);
    bool found=false;
    if(Process32First(snap,&pe)){
        do{
            if(_stricmp(pe.szExeFile,"orbit_wallpaper.exe")==0){found=true;break;}
        }while(Process32Next(snap,&pe));
    }
    CloseHandle(snap);
    return found;
}

static void startWallpaper() {
    std::string path = getWallpaperExePath();
    ShellExecuteA(NULL,"open",path.c_str(),NULL,NULL,SW_HIDE);
}

static void stopWallpaper() {
    std::string path = getWallpaperExePath();
    ShellExecuteA(NULL,"open",path.c_str(),"--stop",NULL,SW_HIDE);
    // also brute-force close any window with that title
    Sleep(300);
    HWND hw=FindWindowA(nullptr,"orbit-wallpaper");
    while(hw){ PostMessage(hw,WM_QUIT,0,0); hw=FindWindowA(nullptr,"orbit-wallpaper"); }
}

static void restartWallpaper() {
    stopWallpaper();
    Sleep(600);
    startWallpaper();
}

// ---------------------------------------------------------------------------
// Monitor enumeration
// ---------------------------------------------------------------------------
struct MonitorInfo { HMONITOR hmon; RECT rc; char name[64]; };
static std::vector<MonitorInfo> g_monitors;
static BOOL CALLBACK monitorEnumProc(HMONITOR hmon, HDC, LPRECT rc, LPARAM) {
    MonitorInfo mi; mi.hmon=hmon; mi.rc=*rc;
    MONITORINFOEXA mie; mie.cbSize=sizeof(mie);
    GetMonitorInfoA(hmon,&mie);
    snprintf(mi.name,sizeof(mi.name),"Monitor %d (%dx%d)",
        (int)g_monitors.size()+1,
        rc->right-rc->left, rc->bottom-rc->top);
    g_monitors.push_back(mi);
    return TRUE;
}
static void enumMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr,nullptr,monitorEnumProc,0);
}

// ---------------------------------------------------------------------------
// Update stuff (same as screensaver)
// ---------------------------------------------------------------------------
static std::string fetchLatestTag() {
    std::string result="";
    HINTERNET hSession=WinHttpOpen(L"OrbitUpdater/1.0",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
    if(!hSession)return result;
    HINTERNET hConnect=WinHttpConnect(hSession,L"api.github.com",INTERNET_DEFAULT_HTTPS_PORT,0);
    if(!hConnect){WinHttpCloseHandle(hSession);return result;}
    HINTERNET hRequest=WinHttpOpenRequest(hConnect,L"GET",
        L"/repos/MalikHw/orbit-wallpaper/releases/latest",
        NULL,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
    if(!hRequest){WinHttpCloseHandle(hConnect);WinHttpCloseHandle(hSession);return result;}
    WinHttpAddRequestHeaders(hRequest,L"User-Agent: OrbitWallpaper",-1,WINHTTP_ADDREQ_FLAG_ADD);
    if(WinHttpSendRequest(hRequest,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)
       &&WinHttpReceiveResponse(hRequest,NULL)){
        char buf[4096]="";DWORD read=0;
        WinHttpReadData(hRequest,buf,sizeof(buf)-1,&read);buf[read]=0;
        const char* p=strstr(buf,"\"tag_name\":");
        if(p){p+=11;while(*p=='"'||*p==' ')p++;char tag[64]="";int i=0;while(*p&&*p!='"'&&i<63)tag[i++]=*p++;tag[i]=0;result=tag;}
    }
    WinHttpCloseHandle(hRequest);WinHttpCloseHandle(hConnect);WinHttpCloseHandle(hSession);
    return result;
}

static void launchUpdater() {
    std::string updaterPath=getExeDir()+"\\updater.exe";
    ShellExecuteA(NULL,"open",updaterPath.c_str(),NULL,NULL,SW_SHOW);
}

struct UpdateDownloadState {
    volatile float progress;
    volatile int   done;
    std::string    url;
    std::string    destPath;
};
struct UpdateCallback : public IBindStatusCallback {
    UpdateDownloadState* s;
    UpdateCallback(UpdateDownloadState* s):s(s){}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG prog,ULONG progMax,ULONG,LPCWSTR) override{
        if(progMax>0)s->progress=(float)prog/(float)progMax;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD,IBinding*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT,LPCWSTR)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*,BINDINFO*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD,DWORD,FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID,IUnknown*)override{return E_NOTIMPL;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 1;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
};
static DWORD WINAPI updateDownloadThread(void* param){
    UpdateDownloadState* s=(UpdateDownloadState*)param;
    UpdateCallback cb(s);
    HRESULT hr=URLDownloadToFileA(NULL,s->url.c_str(),s->destPath.c_str(),0,&cb);
    s->done=(hr==S_OK)?1:-1;
    return 0;
}
static UpdateDownloadState* g_updateDL=nullptr;

// Mesa download (same as screensaver)
struct MesaDownloadState {
    volatile float progress;
    volatile int   done;
    std::string    url, destPath;
};
struct MesaCallback : public IBindStatusCallback {
    MesaDownloadState* s;
    MesaCallback(MesaDownloadState* s):s(s){}
    HRESULT STDMETHODCALLTYPE OnProgress(ULONG prog,ULONG progMax,ULONG,LPCWSTR)override{
        if(progMax>0)s->progress=(float)prog/(float)progMax;return S_OK;}
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD,IBinding*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPriority(LONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnLowResource(DWORD)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT,LPCWSTR)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD*,BINDINFO*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD,DWORD,FORMATETC*,STGMEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID,IUnknown*)override{return E_NOTIMPL;}
    ULONG STDMETHODCALLTYPE AddRef()override{return 1;}
    ULONG STDMETHODCALLTYPE Release()override{return 1;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
};
static DWORD WINAPI mesaThread(void* param){
    MesaDownloadState* s=(MesaDownloadState*)param;
    MesaCallback cb(s);
    HRESULT hr=URLDownloadToFileA(NULL,s->url.c_str(),s->destPath.c_str(),0,&cb);
    s->done=(hr==S_OK)?1:-1;
    return 0;
}
static MesaDownloadState* g_mesaDL=nullptr;
static void startMesaDownload(){
    const char* url="https://github.com/MalikHw/orbit-screensaver/releases/download/mesa3d/opengl32.dll";
    g_mesaDL=new MesaDownloadState();
    g_mesaDL->progress=0.0f;g_mesaDL->done=0;
    g_mesaDL->url=url;
    g_mesaDL->destPath=getExeDir()+"\\opengl32.dll";
    CreateThread(NULL,0,mesaThread,g_mesaDL,0,NULL);
}

// ---------------------------------------------------------------------------
// Settings window
// ---------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){
    enumMonitors();
    loadCfg();

    if(SDL_Init(SDL_INIT_VIDEO)<0)return 1;
    IMG_Init(IMG_INIT_PNG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_Window* win=SDL_CreateWindow("Orbit Wallpaper - Settings",
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,480,620,
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

    // Load logo
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

    // Build monitor combo items
    // index 0 = "All monitors", then one per monitor
    std::vector<std::string> monitorNames;
    monitorNames.push_back("All monitors");
    for(auto& m:g_monitors) monitorNames.push_back(m.name);
    // combo selection: 0 = all (-1), 1..N = monitor 0..N-1
    int monComboSel = (g_settings.monitor_index<0) ? 0 : g_settings.monitor_index+1;
    if(monComboSel>=(int)monitorNames.size()) monComboSel=0;

    static std::string latestTag="";
    static bool updateChecked=false;
    static bool checkingNow=false;

    static bool broPopupPending=false;
    static bool wasOrbFieldFocused=false;

    bool running=true;

    while(running){
        SDL_Event ev;
        while(SDL_PollEvent(&ev)){
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if(ev.type==SDL_QUIT)running=false;
        }
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int W,H;SDL_GetWindowSize(win,&W,&H);
        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(ImVec2((float)W,(float)H));
        ImGui::Begin("##main",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_HorizontalScrollbar);

        // Header
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"ORBIT WALLPAPER");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f),"v%s",APP_VERSION);
        ImGui::Separator();ImGui::Spacing();

        // Speed / FPS
        ImGui::SliderInt("Speed",&g_settings.speed,1,20);

        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("FPS",&g_settings.fps,0);
        if(g_settings.fps<1)g_settings.fps=1;if(g_settings.fps>500)g_settings.fps=500;
        ImGui::SameLine();
        int fpsP[]={30,60,120,144,240,500};
        for(int fp:fpsP){char l[8];sprintf(l,"%d",fp);if(ImGui::SmallButton(l))g_settings.fps=fp;ImGui::SameLine();}
        ImGui::NewLine();

        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("Battery FPS",&g_settings.fps_battery,0);
        if(g_settings.fps_battery<1)g_settings.fps_battery=1;
        if(g_settings.fps_battery>g_settings.fps)g_settings.fps_battery=g_settings.fps;
        ImGui::SameLine();ImGui::TextDisabled("limit when on battery");

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // Orb settings
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Orb count",&g_settings.orb_count,1);
        if(g_settings.orb_count<1)g_settings.orb_count=1;
        bool orbFieldFocused=ImGui::IsItemActive();
        if(wasOrbFieldFocused&&!orbFieldFocused&&g_settings.orb_count<20) broPopupPending=true;
        wasOrbFieldFocused=orbFieldFocused;
        ImGui::SameLine();
        if(ImGui::SmallButton("Low"))  g_settings.orb_count=30;  ImGui::SameLine();
        if(ImGui::SmallButton("Med"))  g_settings.orb_count=80;  ImGui::SameLine();
        if(ImGui::SmallButton("High")) g_settings.orb_count=120; ImGui::SameLine();
        if(ImGui::SmallButton("Giga")) g_settings.orb_count=210;
        if(broPopupPending){ImGui::OpenPopup("bro");broPopupPending=false;}
        if(ImGui::BeginPopupModal("bro",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("bro what the fuck? :sob:, why is even THAT!?");
            if(ImGui::Button("yes"))ImGui::CloseCurrentPopup();
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
        ImGui::Spacing();

        ImGui::Text("Cube PNG");
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

        // Background
        ImGui::Text("Background");
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
        ImGui::Spacing();
        ImGui::Checkbox("No ground (infinite fall)",&g_settings.no_ground);

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // ---- Wallpaper section ----
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"Wallpaper");
        ImGui::Spacing();

        // Monitor picker
        ImGui::Text("Monitor");ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        if(ImGui::BeginCombo("##monitor", monitorNames[monComboSel].c_str())){
            for(int i=0;i<(int)monitorNames.size();i++){
                bool sel=(monComboSel==i);
                if(ImGui::Selectable(monitorNames[i].c_str(),sel)){
                    monComboSel=i;
                    g_settings.monitor_index=(i==0)?-1:i-1;
                }
                if(sel)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::Checkbox("Pause when fullscreen app detected",&g_settings.pause_fullscreen);

        bool autostartChanged=false;
        bool prevAutostart=g_settings.start_with_windows;
        ImGui::Checkbox("Start with Windows",&g_settings.start_with_windows);
        if(g_settings.start_with_windows!=prevAutostart) autostartChanged=true;

        ImGui::Spacing();

        // Status + controls
        bool wallRunning=isWallpaperRunning();
        if(wallRunning){
            ImGui::TextColored(ImVec4(0.2f,1.0f,0.2f,1.0f),"● running");
            ImGui::SameLine();
            if(ImGui::Button("Restart")){saveCfg();restartWallpaper();}
            ImGui::SameLine();
            if(ImGui::SmallButton("Stop")){stopWallpaper();}
        } else {
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1.0f),"● stopped");
            ImGui::SameLine();
            if(ImGui::Button("Start")){saveCfg();startWallpaper();}
        }

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // Updates
        ImGui::TextColored(ImVec4(0.4f,0.8f,1.0f,1.0f),"Updates");
        ImGui::Spacing();
        if(g_updateDL&&g_updateDL->done==0){
            char lbl[16];snprintf(lbl,sizeof(lbl),"%.0f%%",g_updateDL->progress*100.0f);
            ImGui::ProgressBar(g_updateDL->progress,ImVec2(220,20),lbl);
            ImGui::SameLine();ImGui::TextColored(ImVec4(1,1,0,1),"Downloading update...");
        } else if(g_updateDL&&g_updateDL->done==1){
            ImGui::TextColored(ImVec4(0,1,0,1),"Downloaded! Launching updater...");
            saveCfg();launchUpdater();running=false;
        } else if(g_updateDL&&g_updateDL->done==-1){
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"Download failed!");
        } else {
            if(checkingNow){
                ImGui::TextColored(ImVec4(1,1,0,1),"Checking...");
            } else if(!updateChecked){
                if(ImGui::Button("Check for updates",ImVec2(200,24))){
                    checkingNow=true;
                    latestTag=fetchLatestTag();
                    updateChecked=true;
                    checkingNow=false;
                }
            } else if(latestTag.empty()||latestTag==APP_VERSION){
                ImGui::TextColored(ImVec4(0,1,0,1),"You're up to date! (%s)",APP_VERSION);
            } else {
                ImGui::TextColored(ImVec4(1,0.5f,0,1),"Update available: %s",latestTag.c_str());
                ImGui::SameLine();
                if(ImGui::Button("Install")){
                    g_updateDL=new UpdateDownloadState();
                    g_updateDL->progress=0.0f;g_updateDL->done=0;
                    g_updateDL->url="https://github.com/MalikHw/orbit-wallpaper/releases/download/"+latestTag+"/orbit-wallpaper.zip";
                    g_updateDL->destPath=getExeDir()+"\\orbit-update.zip";
                    CreateThread(NULL,0,updateDownloadThread,g_updateDL,0,NULL);
                }
            }
        }
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // Save buttons
        if(ImGui::Button("Save",ImVec2(100,30))){
            saveCfg();
            if(autostartChanged)setAutostart(g_settings.start_with_windows);
            ImGui::OpenPopup("Saved");
        }
        ImGui::SameLine();
        if(ImGui::Button("Save and Apply",ImVec2(130,30))){
            saveCfg();
            if(autostartChanged)setAutostart(g_settings.start_with_windows);
            restartWallpaper();
            ImGui::OpenPopup("Saved");
        }
        ImGui::SameLine();
        if(ImGui::Button("Save and Exit",ImVec2(120,30))){
            saveCfg();
            if(autostartChanged)setAutostart(g_settings.start_with_windows);
            running=false;
        }
        if(ImGui::BeginPopupModal("Saved",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Settings saved!");
            if(ImGui::Button("OK"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();

        // Mesa
        if(g_mesaDL&&g_mesaDL->done==0){
            char lbl[16];snprintf(lbl,sizeof(lbl),"%.0f%%",g_mesaDL->progress*100.0f);
            ImGui::ProgressBar(g_mesaDL->progress,ImVec2(180,20),lbl);
            ImGui::SameLine();ImGui::TextColored(ImVec4(1,1,0,1),"Downloading...");
        } else if(g_mesaDL&&g_mesaDL->done==1){
            ImGui::TextColored(ImVec4(0,1,0,1),"Mesa3D installed!");
        } else if(g_mesaDL&&g_mesaDL->done==-1){
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
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f),"by MalikHw47");
        ImGui::Spacing();
        if(ImGui::SmallButton("MalikHw47"))  ShellExecuteA(0,"open","https://malikhw.github.io",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("youtube"))    ShellExecuteA(0,"open","https://youtube.com/@MalikHw47",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("github"))     ShellExecuteA(0,"open","https://github.com/MalikHw",0,0,SW_SHOW);
        ImGui::SameLine();ImGui::Text("-");ImGui::SameLine();
        if(ImGui::SmallButton("twitch"))     ShellExecuteA(0,"open","https://twitch.tv/MalikHw47",0,0,SW_SHOW);
        ImGui::Spacing();
        if(ImGui::Button("Join my server",ImVec2(180,22)))   ShellExecuteA(0,"open","https://discord.gg/G9bZ92eg2n",0,0,SW_SHOW);
        ImGui::SameLine();
        if(ImGui::Button("Get me a gift!",ImVec2(150,22)))   ShellExecuteA(0,"open","https://throne.com/MalikHw47",0,0,SW_SHOW);
        if(ImGui::Button("Get me MegaHack!",ImVec2(180,22))) ShellExecuteA(0,"open","https://absolllute.com/store/mega_hack?gift=1",0,0,SW_SHOW);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("My discord is MalikHw btw");
        ImGui::SameLine();
        if(ImGui::Button("Donate!",ImVec2(150,22)))           ShellExecuteA(0,"open","https://ko-fi.com/malikhw47",0,0,SW_SHOW);

        // Logo corner
        if(logoTex){
            const float logoSize=56.0f;
            ImVec2 winPos=ImGui::GetWindowPos();
            ImVec2 winSize=ImGui::GetWindowSize();
            ImVec2 logoPos=ImVec2(winPos.x+winSize.x-logoSize-6,winPos.y+winSize.y-logoSize-6);
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(uintptr_t)logoTex,
                logoPos,ImVec2(logoPos.x+logoSize,logoPos.y+logoSize),
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
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    IMG_Quit();
    SDL_Quit();
    return 0;
}
