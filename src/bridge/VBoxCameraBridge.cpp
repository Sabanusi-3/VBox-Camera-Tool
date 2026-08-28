#include <Windows.h>
#include <conio.h>
#include <stdio.h>
#include <cstdint>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include "VirtualBox.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Uuid.lib")

#define SAFE_RELEASE(x) do { if (x) { (x)->Release(); (x)=NULL; } } while(0)

constexpr DWORD MAX_WIDTH=1920,MAX_HEIGHT=1080,MAX_FRAME_BYTES=MAX_WIDTH*MAX_HEIGHT*4;
constexpr uint32_t OUTPUT_HEIGHT=720,OUTPUT_WIDTH_4_3=960,OUTPUT_WIDTH_16_9=1280;
constexpr uint32_t VBOX_CAMERA_MAGIC=0x41434256,VBOX_CONTROL_MAGIC=0x54434256,CONTROL_VERSION=4;
constexpr LONG ASPECT_MODE_4_3=0,ASPECT_MODE_16_9=1,VBOX_MAX_VMS=64;
constexpr size_t VBOX_VM_NAME_CHARS=128,VBOX_VM_UUID_CHARS=64;

#pragma pack(push,1)
struct VBoxCameraFrameHeader{uint32_t magic,width,height,stride,frameCounter;};
struct VBoxCameraVmEntry{WCHAR name[VBOX_VM_NAME_CHARS];WCHAR uuid[VBOX_VM_UUID_CHARS];volatile LONG running;};
struct VBoxCameraControl{
 uint32_t magic,version;volatile LONG targetFps,captureAudio,aspectMode;volatile LONG audioConfigVersion;WCHAR audioDeviceName[128];volatile LONG followVm;
 volatile LONG selectedVmSequence;WCHAR selectedVmUuid[VBOX_VM_UUID_CHARS];volatile LONG vmListSequence,vmCount;VBoxCameraVmEntry vmList[VBOX_MAX_VMS];
 volatile LONG activeVmSequence;WCHAR activeVmUuid[VBOX_VM_UUID_CHARS];uint32_t reserved[8];
};
#pragma pack(pop)

HANDLE g_videoMap=NULL,g_controlMap=NULL;BYTE*g_videoShared=NULL,*g_videoPixels=NULL;VBoxCameraFrameHeader*g_videoHeader=NULL;VBoxCameraControl*g_control=NULL;std::atomic<bool>g_stop(false);

LONG readLong(volatile LONG*p,LONG fallback){return p?InterlockedCompareExchange(p,0,0):fallback;}
bool initControl(){
 g_controlMap=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(VBoxCameraControl),L"Local\\VBoxCameraControl");if(!g_controlMap)return false;
 g_control=(VBoxCameraControl*)MapViewOfFile(g_controlMap,FILE_MAP_ALL_ACCESS,0,0,sizeof(VBoxCameraControl));if(!g_control)return false;ZeroMemory(g_control,sizeof(*g_control));
 g_control->magic=VBOX_CONTROL_MAGIC;g_control->version=CONTROL_VERSION;InterlockedExchange(&g_control->targetFps,30);InterlockedExchange(&g_control->captureAudio,1);InterlockedExchange(&g_control->aspectMode,ASPECT_MODE_4_3);InterlockedExchange(&g_control->followVm,1);return true;
}
bool initVideo(){
 DWORD n=sizeof(VBoxCameraFrameHeader)+MAX_FRAME_BYTES;g_videoMap=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,n,L"Local\\VBoxCameraFrame");if(!g_videoMap)return false;
 g_videoShared=(BYTE*)MapViewOfFile(g_videoMap,FILE_MAP_ALL_ACCESS,0,0,n);if(!g_videoShared)return false;ZeroMemory(g_videoShared,n);g_videoHeader=(VBoxCameraFrameHeader*)g_videoShared;g_videoPixels=g_videoShared+sizeof(VBoxCameraFrameHeader);g_videoHeader->magic=VBOX_CAMERA_MAGIC;return true;
}
void cleanup(){if(g_videoShared)UnmapViewOfFile(g_videoShared);if(g_videoMap)CloseHandle(g_videoMap);if(g_control)UnmapViewOfFile(g_control);if(g_controlMap)CloseHandle(g_controlMap);g_videoShared=NULL;g_videoMap=NULL;g_control=NULL;g_controlMap=NULL;}
uint32_t targetFps(){LONG n=readLong(&g_control->targetFps,30);return(uint32_t)std::clamp<LONG>(n,1,60);}
LONG aspectMode(){return readLong(&g_control->aspectMode,ASPECT_MODE_4_3)==ASPECT_MODE_16_9?ASPECT_MODE_16_9:ASPECT_MODE_4_3;}
bool followVm(){return readLong(&g_control->followVm,1)!=0;}
LONG selectedUuid(WCHAR*out,size_t n){
 if(!out||!n||!g_control)return-1;for(;;){LONG a=readLong(&g_control->selectedVmSequence,0);if(a&1){Sleep(1);continue;}wcsncpy_s(out,n,g_control->selectedVmUuid,_TRUNCATE);MemoryBarrier();LONG b=readLong(&g_control->selectedVmSequence,0);if(a==b&&!(b&1))return b;}
}
void activeUuid(const WCHAR*uuid){
 if(!g_control)return;const WCHAR*v=uuid?uuid:L"";if(wcscmp(g_control->activeVmUuid,v)==0)return;InterlockedIncrement(&g_control->activeVmSequence);MemoryBarrier();wcsncpy_s(g_control->activeVmUuid,VBOX_VM_UUID_CHARS,v,_TRUNCATE);MemoryBarrier();InterlockedIncrement(&g_control->activeVmSequence);
}
bool machineUuid(IMachine*m,WCHAR*out,size_t n){BSTR id=NULL;if(!m||FAILED(m->get_Id(&id))||!id)return false;wcsncpy_s(out,n,id,_TRUNCATE);SysFreeString(id);return out[0]!=0;}
void publishVmList(IVirtualBox*vbox){
 if(!vbox||!g_control)return;InterlockedIncrement(&g_control->vmListSequence);MemoryBarrier();InterlockedExchange(&g_control->vmCount,0);for(LONG i=0;i<VBOX_MAX_VMS;i++){g_control->vmList[i].name[0]=0;g_control->vmList[i].uuid[0]=0;InterlockedExchange(&g_control->vmList[i].running,0);}
 SAFEARRAY*a=NULL;if(SUCCEEDED(vbox->get_Machines(&a))&&a){IMachine**m=NULL;if(SUCCEEDED(SafeArrayAccessData(a,(void**)&m))){ULONG count=a->rgsabound[0].cElements;LONG out=0;for(ULONG i=0;i<count&&out<VBOX_MAX_VMS;i++){if(!m[i])continue;BSTR name=NULL,id=NULL;MachineState state=(MachineState)0;if(FAILED(m[i]->get_Id(&id))||!id)continue;m[i]->get_Name(&name);m[i]->get_State(&state);wcsncpy_s(g_control->vmList[out].uuid,VBOX_VM_UUID_CHARS,id,_TRUNCATE);if(name)wcsncpy_s(g_control->vmList[out].name,VBOX_VM_NAME_CHARS,name,_TRUNCATE);InterlockedExchange(&g_control->vmList[out].running,state==MachineState_Running?1:0);out++;if(name)SysFreeString(name);SysFreeString(id);}InterlockedExchange(&g_control->vmCount,out);SafeArrayUnaccessData(a);}SafeArrayDestroy(a);}MemoryBarrier();InterlockedIncrement(&g_control->vmListSequence);
}
IMachine*findMachine(IVirtualBox*vbox,const WCHAR*wanted){
 if(!vbox||!wanted||!*wanted)return NULL;SAFEARRAY*a=NULL;if(FAILED(vbox->get_Machines(&a))||!a)return NULL;IMachine**m=NULL;IMachine*r=NULL;if(SUCCEEDED(SafeArrayAccessData(a,(void**)&m))){ULONG count=a->rgsabound[0].cElements;for(ULONG i=0;i<count;i++){WCHAR id[VBOX_VM_UUID_CHARS];if(machineUuid(m[i],id,VBOX_VM_UUID_CHARS)&&_wcsicmp(id,wanted)==0){r=m[i];r->AddRef();break;}}SafeArrayUnaccessData(a);}SafeArrayDestroy(a);return r;
}
bool sendFrame(IDisplay*d){
 ULONG sw=0,sh=0,bpp=0;LONG xo=0,yo=0;GuestMonitorStatus status;if(FAILED(d->GetScreenResolution(0,&sw,&sh,&bpp,&xo,&yo,&status))||!sw||!sh||sw>MAX_WIDTH||sh>MAX_HEIGHT)return false;
 SAFEARRAY*shot=NULL;if(FAILED(d->TakeScreenShotToArray(0,sw,sh,BitmapFormat_BGR0,&shot))||!shot)return false;BYTE*data=NULL;if(FAILED(SafeArrayAccessData(shot,(void**)&data))){SafeArrayDestroy(shot);return false;}
 LONG lo=0,hi=0;SafeArrayGetLBound(shot,1,&lo);SafeArrayGetUBound(shot,1,&hi);size_t actual=(size_t)(hi-lo+1),expected=(size_t)sw*sh*4;bool ok=false;
 if(actual>=expected){uint32_t cw=aspectMode()==ASPECT_MODE_16_9?OUTPUT_WIDTH_16_9:OUTPUT_WIDTH_4_3,ch=OUTPUT_HEIGHT,dw,dh;if((uint64_t)cw*sh<=(uint64_t)ch*sw){dw=cw;dh=(uint32_t)((uint64_t)sh*cw/sw);}else{dh=ch;dw=(uint32_t)((uint64_t)sw*ch/sh);}dw=std::max(dw,1u);dh=std::max(dh,1u);uint32_t ox=(cw-dw)/2,oy=(ch-dh)/2;
  g_videoHeader->frameCounter++;MemoryBarrier();ZeroMemory(g_videoPixels,(size_t)cw*ch*4);const uint32_t*src=(const uint32_t*)data;uint32_t*dst=(uint32_t*)g_videoPixels;
  for(uint32_t y=0;y<dh;y++){uint32_t sy=(uint32_t)((uint64_t)y*sh/dh);uint32_t*row=dst+(size_t)(y+oy)*cw+ox;const uint32_t*srow=src+(size_t)sy*sw;for(uint32_t x=0;x<dw;x++)row[x]=srow[(uint32_t)((uint64_t)x*sw/dw)];}
  g_videoHeader->width=cw;g_videoHeader->height=ch;g_videoHeader->stride=cw*4;MemoryBarrier();g_videoHeader->frameCounter++;ok=true;}
 SafeArrayUnaccessData(shot);SafeArrayDestroy(shot);return ok;
}
int streamMachine(IVirtualBox*vbox,IMachine*m,const WCHAR*expected){
 ISession*s=NULL;IConsole*c=NULL;IDisplay*d=NULL;HRESULT rc=CoCreateInstance(CLSID_Session,NULL,CLSCTX_INPROC_SERVER,IID_ISession,(void**)&s);if(FAILED(rc))return 1;
 rc=m->LockMachine(s,LockType_Shared);if(FAILED(rc)){SAFE_RELEASE(s);return 1;}if(FAILED(s->get_Console(&c))||FAILED(c->get_Display(&d))){SAFE_RELEASE(c);s->UnlockMachine();SAFE_RELEASE(s);return 1;}
 using Clock=std::chrono::steady_clock;auto next=Clock::now();ULONGLONG check=0;while(!g_stop.load()){
  if(_kbhit()&&_getch()==27){g_stop.store(true);break;}ULONGLONG now=GetTickCount64();if(now-check>=500){check=now;WCHAR selected[VBOX_VM_UUID_CHARS];selectedUuid(selected,VBOX_VM_UUID_CHARS);MachineState state=(MachineState)0;if(FAILED(m->get_State(&state))||state!=MachineState_Running||_wcsicmp(selected,expected)!=0)break;publishVmList(vbox);}
  uint32_t fps=targetFps();auto dur=std::chrono::nanoseconds(1000000000ULL/fps);auto t=Clock::now();if(t<next)std::this_thread::sleep_until(next);else if(t-next>std::chrono::seconds(1))next=t;sendFrame(d);next+=dur;
 }
 SAFE_RELEASE(d);SAFE_RELEASE(c);s->UnlockMachine();SAFE_RELEASE(s);return 0;
}
int main(){
 printf("VBox Camera Bridge alpha\n");HRESULT rc=CoInitialize(NULL);if(FAILED(rc))return 1;IVirtualBoxClient*client=NULL;rc=CoCreateInstance(CLSID_VirtualBoxClient,NULL,CLSCTX_INPROC_SERVER,IID_IVirtualBoxClient,(void**)&client);if(FAILED(rc)){CoUninitialize();return 1;}
 if(!initControl()||!initVideo()){cleanup();SAFE_RELEASE(client);CoUninitialize();return 1;}IVirtualBox*vbox=NULL;if(FAILED(client->get_VirtualBox(&vbox))){cleanup();SAFE_RELEASE(client);CoUninitialize();return 1;}
 printf("Bridge ready. Select a VM in OBS VBox Camera properties. ESC exits.\n");LONG lastSeq=-1;bool oneShot=true;activeUuid(L"");
 while(!g_stop.load()){
  if(_kbhit()&&_getch()==27){g_stop.store(true);break;}publishVmList(vbox);WCHAR selected[VBOX_VM_UUID_CHARS];LONG seq=selectedUuid(selected,VBOX_VM_UUID_CHARS);if(seq!=lastSeq){lastSeq=seq;oneShot=true;}
  if(!selected[0]){activeUuid(L"");Sleep(500);continue;}bool follow=followVm();IMachine*m=findMachine(vbox,selected);if(!m){activeUuid(L"");Sleep(500);continue;}MachineState state=(MachineState)0;HRESULT sr=m->get_State(&state);
  if(SUCCEEDED(sr)&&state==MachineState_Running&&(follow||oneShot)){activeUuid(selected);streamMachine(vbox,m,selected);activeUuid(L"");if(!follow)oneShot=false;}else if(!follow)oneShot=false;SAFE_RELEASE(m);Sleep(500);
 }
 activeUuid(L"");SAFE_RELEASE(vbox);SAFE_RELEASE(client);cleanup();CoUninitialize();return 0;
}
