/*
 * updater.cpp  –  Orbit Wallpaper auto-updater
 *
 * Launched by the main exe after downloading orbit-update.zip.
 * - Waits for the main process to exit (if still running)
 * - Extracts orbit-update.zip over the install directory
 * - Restarts orbit_wallpaper.exe
 *
 * Build: mingw / MSVC — no extra dependencies beyond Win32 + shell32 + ole32
 */

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <string>

#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")

static std::string getExeDir(){
    char buf[MAX_PATH];GetModuleFileNameA(NULL,buf,MAX_PATH);
    std::string s(buf);return s.substr(0,s.rfind('\\'));
}

static bool extractZip(const std::string& zipPath, const std::string& destDir){
    // Use Windows Shell IShellDispatch to extract zip (no external lib needed)
    CoInitialize(NULL);
    IShellDispatch* pSD=nullptr;
    if(CoCreateInstance(CLSID_Shell,NULL,CLSCTX_INPROC_SERVER,
        IID_IShellDispatch,(void**)&pSD)!=S_OK) return false;

    BSTR bZip  =SysAllocStringLen(NULL,(UINT)zipPath.size());
    BSTR bDest =SysAllocStringLen(NULL,(UINT)destDir.size());
    MultiByteToWideChar(CP_ACP,0,zipPath.c_str(),-1,bZip,(int)zipPath.size()+1);
    MultiByteToWideChar(CP_ACP,0,destDir.c_str(),-1,bDest,(int)destDir.size()+1);

    VARIANT vZip,vDest;
    VariantInit(&vZip);VariantInit(&vDest);
    vZip.vt=VT_BSTR; vZip.bstrVal=bZip;
    vDest.vt=VT_BSTR;vDest.bstrVal=bDest;

    Folder* pZipFolder=nullptr;
    Folder* pDestFolder=nullptr;
    pSD->NameSpace(vZip,&pZipFolder);
    pSD->NameSpace(vDest,&pDestFolder);

    bool ok=false;
    if(pZipFolder&&pDestFolder){
        FolderItems* pItems=nullptr;
        pZipFolder->Items(&pItems);
        if(pItems){
            VARIANT vItems;VariantInit(&vItems);
            vItems.vt=VT_DISPATCH;
            pItems->QueryInterface(IID_IDispatch,(void**)&vItems.pdispVal);
            VARIANT vOpts;VariantInit(&vOpts);
            vOpts.vt=VT_I4;
            // 4=no progress, 16=yes to all, 1024=no error UI
            vOpts.lVal=4|16|1024;
            HRESULT hr=pDestFolder->CopyHere(vItems,vOpts);
            ok=(hr==S_OK||hr==S_FALSE); // S_FALSE = already exists, still fine
            // give the async copy a moment to finish
            Sleep(2000);
            VariantClear(&vItems);
            pItems->Release();
        }
        pZipFolder->Release();
        pDestFolder->Release();
    }
    pSD->Release();
    SysFreeString(bZip);SysFreeString(bDest);
    CoUninitialize();
    return ok;
}

static void killRunningInstance(){
    // find any running orbit_wallpaper.exe and wait for it to die
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return;
    PROCESSENTRY32 pe;pe.dwSize=sizeof(pe);
    if(Process32First(snap,&pe)){
        do {
            if(_stricmp(pe.szExeFile,"orbit_wallpaper.exe")==0){
                HANDLE h=OpenProcess(SYNCHRONIZE|PROCESS_TERMINATE,FALSE,pe.th32ProcessID);
                if(h){
                    TerminateProcess(h,0);
                    WaitForSingleObject(h,5000);
                    CloseHandle(h);
                }
            }
        } while(Process32Next(snap,&pe));
    }
    CloseHandle(snap);
}

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int){
    std::string exeDir=getExeDir();
    std::string zipPath=exeDir+"\\orbit-update.zip";

    // check zip exists
    if(GetFileAttributesA(zipPath.c_str())==INVALID_FILE_ATTRIBUTES){
        MessageBoxA(NULL,"orbit-update.zip not found!\nNothing to update.","Orbit Updater",MB_ICONERROR);
        return 1;
    }

    // kill running instance
    killRunningInstance();
    Sleep(500);

    // backup updater itself before extraction overwrites it
    std::string selfPath=exeDir+"\\updater.exe";
    std::string selfBak =exeDir+"\\updater.exe.bak";
    CopyFileA(selfPath.c_str(),selfBak.c_str(),FALSE);

    // extract
    if(!extractZip(zipPath,exeDir)){
        MessageBoxA(NULL,"Failed to extract update.\nTry extracting orbit-update.zip manually.","Orbit Updater",MB_ICONERROR);
        return 1;
    }

    // rename new updater if it landed as .pending (shouldn't on clean zip, but just in case)
    std::string pending=exeDir+"\\updater.exe.pending";
    if(GetFileAttributesA(pending.c_str())!=INVALID_FILE_ATTRIBUTES){
        DeleteFileA(selfPath.c_str());
        MoveFileA(pending.c_str(),selfPath.c_str());
    }

    // clean up zip
    DeleteFileA(zipPath.c_str());
    DeleteFileA(selfBak.c_str());

    // restart main exe
    std::string mainExe=exeDir+"\\orbit_wallpaper.exe";
    ShellExecuteA(NULL,"open",mainExe.c_str(),NULL,exeDir.c_str(),SW_SHOW);

    return 0;
}
