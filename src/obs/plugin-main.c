#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <Windows.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define VBOX_CAMERA_MAGIC 0x41434256u
#define VBOX_CONTROL_MAGIC 0x54434256u
#define VBOX_CONTROL_VERSION 4u
#define MAX_WIDTH 1920
#define MAX_HEIGHT 1080
#define MAX_FRAME_BYTES (MAX_WIDTH * MAX_HEIGHT * 4)
#define ASPECT_MODE_4_3 0
#define ASPECT_MODE_16_9 1
#define VBOX_MAX_VMS 64
#define VBOX_VM_NAME_CHARS 128
#define VBOX_VM_UUID_CHARS 64
#define VBOX_AUDIO_SOURCE_TYPE "wasapi_process_output_capture"
#define VBOX_AUDIO_WINDOW "::VirtualBoxVM.exe"
#define VBOX_WINDOW_PRIORITY_EXE 2

#pragma pack(push, 1)
struct VBoxCameraFrameHeader { uint32_t magic,width,height,stride,frameCounter; };
struct VBoxCameraVmEntry { WCHAR name[VBOX_VM_NAME_CHARS]; WCHAR uuid[VBOX_VM_UUID_CHARS]; volatile LONG running; };
struct VBoxCameraControl {
 uint32_t magic,version; volatile LONG targetFps,captureAudio,aspectMode;
 volatile LONG audioConfigVersion; WCHAR audioDeviceName[128]; volatile LONG followVm;
 volatile LONG selectedVmSequence; WCHAR selectedVmUuid[VBOX_VM_UUID_CHARS];
 volatile LONG vmListSequence,vmCount; struct VBoxCameraVmEntry vmList[VBOX_MAX_VMS];
 volatile LONG activeVmSequence; WCHAR activeVmUuid[VBOX_VM_UUID_CHARS]; uint32_t reserved[8];
};
#pragma pack(pop)

struct vbox_camera {
 obs_source_t *source,*audio_source; HANDLE video_mapping,control_mapping; BYTE *video_shared,*video_pixels,*video_local;
 struct VBoxCameraFrameHeader *video_header; struct VBoxCameraControl *control; uint32_t lastFrameCounter;
 bool audio_reinit_pending,capture_audio,follow_vm,control_dirty; float audio_reinit_timer; int target_fps,aspect_mode;
 char selected_vm_uuid[VBOX_VM_UUID_CHARS]; LONG last_active_vm_sequence;
};

static LONG read_active(struct vbox_camera *c){ if(!c||!c->control)return -1; LONG s=InterlockedCompareExchange(&c->control->activeVmSequence,0,0); return (s&1)?-1:s; }
static const char *get_name(void *p){UNUSED_PARAMETER(p);return "VBox Camera";}
static bool open_video(struct vbox_camera *c){
 if(c->video_mapping)return true; c->video_mapping=OpenFileMappingW(FILE_MAP_READ,FALSE,L"Local\\VBoxCameraFrame"); if(!c->video_mapping)return false;
 c->video_shared=(BYTE*)MapViewOfFile(c->video_mapping,FILE_MAP_READ,0,0,0); if(!c->video_shared){CloseHandle(c->video_mapping);c->video_mapping=NULL;return false;}
 c->video_header=(struct VBoxCameraFrameHeader*)c->video_shared; c->video_pixels=c->video_shared+sizeof(struct VBoxCameraFrameHeader); return true;
}
static bool open_control(struct vbox_camera *c){
 if(c->control_mapping)return true; c->control_mapping=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,L"Local\\VBoxCameraControl"); if(!c->control_mapping)return false;
 c->control=(struct VBoxCameraControl*)MapViewOfFile(c->control_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);
 if(!c->control){CloseHandle(c->control_mapping);c->control_mapping=NULL;return false;}
 if(c->control->magic!=VBOX_CONTROL_MAGIC||c->control->version!=VBOX_CONTROL_VERSION){UnmapViewOfFile(c->control);c->control=NULL;CloseHandle(c->control_mapping);c->control_mapping=NULL;return false;}
 c->control_dirty=true;c->last_active_vm_sequence=read_active(c);return true;
}
static void u8tow(const char*s,WCHAR*d,size_t n){if(!d||!n)return;d[0]=0;if(s&&*s)MultiByteToWideChar(CP_UTF8,0,s,-1,d,(int)n);d[n-1]=0;}
static void wtou8(const WCHAR*s,char*d,size_t n){if(!d||!n)return;d[0]=0;if(s&&*s)WideCharToMultiByte(CP_UTF8,0,s,-1,d,(int)n,NULL,NULL);d[n-1]=0;}
static void publish_selection(struct vbox_camera*c){
 if(!c||!c->control||!c->control_dirty)return; InterlockedExchange(&c->control->followVm,c->follow_vm?1:0); InterlockedIncrement(&c->control->selectedVmSequence); MemoryBarrier();
 u8tow(c->selected_vm_uuid,c->control->selectedVmUuid,VBOX_VM_UUID_CHARS); MemoryBarrier(); InterlockedIncrement(&c->control->selectedVmSequence);c->control_dirty=false;
}
static void write_control(struct vbox_camera*c){
 if(!c)return;if(!c->control_mapping&&!open_control(c))return;if(!c->control)return;
 LONG fps=c->target_fps;if(fps<1)fps=1;if(fps>60)fps=60;InterlockedExchange(&c->control->targetFps,fps);InterlockedExchange(&c->control->captureAudio,c->capture_audio?1:0);
 InterlockedExchange(&c->control->aspectMode,c->aspect_mode==ASPECT_MODE_16_9?ASPECT_MODE_16_9:ASPECT_MODE_4_3);publish_selection(c);
}
static void process_video(struct vbox_camera*c){
 if(!c->video_mapping){open_video(c);return;} if(!c->video_header||c->video_header->magic!=VBOX_CAMERA_MAGIC)return;
 uint32_t w=c->video_header->width,h=c->video_header->height,s=c->video_header->stride,n=c->video_header->frameCounter;if(n&1||!w||!h||w>MAX_WIDTH||h>MAX_HEIGHT||s<w*4||n==c->lastFrameCounter)return;
 size_t bytes=(size_t)s*h;if(!bytes||bytes>MAX_FRAME_BYTES||!c->video_local)return;memcpy(c->video_local,c->video_pixels,bytes);MemoryBarrier();uint32_t after=c->video_header->frameCounter;if(n!=after||(after&1))return;
 struct obs_source_frame f;memset(&f,0,sizeof(f));f.data[0]=c->video_local;f.linesize[0]=s;f.width=w;f.height=h;f.format=VIDEO_FORMAT_BGRX;f.full_range=true;c->lastFrameCounter=n;obs_source_output_video(c->source,&f);
}
static void reroute(obs_source_t*w,obs_source_t*t){if(!w)return;proc_handler_t*p=obs_source_get_proc_handler(w);if(!p)return;calldata_t d={0};calldata_set_ptr(&d,"target",t);proc_handler_call(p,"reroute_audio",&d);calldata_free(&d);}
static void destroy_audio(struct vbox_camera*c){if(!c||!c->audio_source)return;reroute(c->audio_source,NULL);obs_source_remove_active_child(c->source,c->audio_source);obs_source_release(c->audio_source);c->audio_source=NULL;}
static void schedule_audio(struct vbox_camera*c,float delay){if(c&&c->capture_audio){c->audio_reinit_pending=true;c->audio_reinit_timer=delay;}}
static void setup_audio(struct vbox_camera*c,bool force){
 if(!c)return;if(!c->capture_audio){obs_source_set_audio_active(c->source,false);destroy_audio(c);c->audio_reinit_pending=false;return;}
 if(!obs_get_latest_input_type_id(VBOX_AUDIO_SOURCE_TYPE)){obs_source_set_audio_active(c->source,false);return;}
 obs_data_t*s=obs_data_create();obs_data_set_string(s,"window",VBOX_AUDIO_WINDOW);obs_data_set_int(s,"priority",VBOX_WINDOW_PRIORITY_EXE);if(force&&c->audio_source)destroy_audio(c);
 if(!c->audio_source){c->audio_source=obs_source_create_private(VBOX_AUDIO_SOURCE_TYPE,"VBox Camera Application Audio",s);if(c->audio_source){obs_source_add_active_child(c->source,c->audio_source);reroute(c->audio_source,c->source);}}else obs_source_update(c->audio_source,s);
 obs_source_set_audio_active(c->source,c->audio_source!=NULL);obs_data_release(s);
}
static void update(void*d,obs_data_t*s){
 struct vbox_camera*c=d;if(!c)return;int fps=(int)obs_data_get_int(s,"fps");if(fps<1)fps=1;if(fps>60)fps=60;c->target_fps=fps;c->capture_audio=obs_data_get_bool(s,"capture_audio");c->aspect_mode=obs_data_get_int(s,"aspect_mode")==ASPECT_MODE_16_9?ASPECT_MODE_16_9:ASPECT_MODE_4_3;
 bool follow=obs_data_get_bool(s,"follow_vm");if(c->follow_vm!=follow){c->follow_vm=follow;c->control_dirty=true;}const char*u=obs_data_get_string(s,"virtual_machine");if(!u)u="";if(strcmp(c->selected_vm_uuid,u)){strncpy(c->selected_vm_uuid,u,sizeof(c->selected_vm_uuid)-1);c->selected_vm_uuid[sizeof(c->selected_vm_uuid)-1]=0;c->control_dirty=true;}
 write_control(c);setup_audio(c,false);schedule_audio(c,0.75f);
}
static void defaults(obs_data_t*s){obs_data_set_default_int(s,"fps",30);obs_data_set_default_bool(s,"capture_audio",true);obs_data_set_default_int(s,"aspect_mode",ASPECT_MODE_4_3);obs_data_set_default_string(s,"virtual_machine","");obs_data_set_default_bool(s,"follow_vm",true);}
static obs_properties_t *properties(void*d){
 struct vbox_camera*c=d;obs_properties_t*p=obs_properties_create();obs_property_t*vm=obs_properties_add_list(p,"virtual_machine","Virtual Machine",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_STRING);bool added=false;
 if(c){if(!c->control_mapping)open_control(c);if(c->control&&c->control->magic==VBOX_CONTROL_MAGIC&&c->control->version==VBOX_CONTROL_VERSION){LONG count=InterlockedCompareExchange(&c->control->vmCount,0,0);if(count<0)count=0;if(count>VBOX_MAX_VMS)count=VBOX_MAX_VMS;for(LONG i=0;i<count;i++){char name[384],uuid[128],label[448];wtou8(c->control->vmList[i].name,name,sizeof(name));wtou8(c->control->vmList[i].uuid,uuid,sizeof(uuid));if(!uuid[0])continue;bool run=InterlockedCompareExchange(&c->control->vmList[i].running,0,0)!=0;snprintf(label,sizeof(label),"%s%s",name[0]?name:"(Unnamed VM)",run?"  [Running]":"  [Stopped]");obs_property_list_add_string(vm,label,uuid);added=true;}}}
 if(!added)obs_property_list_add_string(vm,"(No VM list - start Bridge and reopen Properties)",c&&c->selected_vm_uuid[0]?c->selected_vm_uuid:"");obs_properties_add_bool(p,"follow_vm","Follow when VM starts");obs_property_t*a=obs_properties_add_list(p,"aspect_mode","Aspect Ratio",OBS_COMBO_TYPE_LIST,OBS_COMBO_FORMAT_INT);obs_property_list_add_int(a,"4:3",ASPECT_MODE_4_3);obs_property_list_add_int(a,"16:9",ASPECT_MODE_16_9);obs_properties_add_int(p,"fps","Frame Rate",1,60,1);obs_properties_add_bool(p,"capture_audio","Capture VirtualBox Audio");return p;
}
static void *create(obs_data_t*s,obs_source_t*src){struct vbox_camera*c=bzalloc(sizeof(*c));c->source=src;c->target_fps=30;c->capture_audio=true;c->aspect_mode=ASPECT_MODE_4_3;c->follow_vm=true;c->control_dirty=true;c->last_active_vm_sequence=-1;c->video_local=(BYTE*)bzalloc(MAX_FRAME_BYTES);open_video(c);open_control(c);update(c,s);return c;}
static void destroy(void*d){struct vbox_camera*c=d;if(!c)return;if(c->video_shared)UnmapViewOfFile(c->video_shared);if(c->video_mapping)CloseHandle(c->video_mapping);if(c->video_local)bfree(c->video_local);destroy_audio(c);if(c->control)UnmapViewOfFile(c->control);if(c->control_mapping)CloseHandle(c->control_mapping);bfree(c);}
static void tick(void*d,float sec){struct vbox_camera*c=d;if(!c)return;if(!c->control_mapping)open_control(c);write_control(c);process_video(c);LONG seq=read_active(c);if(seq>=0&&seq!=c->last_active_vm_sequence){c->last_active_vm_sequence=seq;if(c->capture_audio)schedule_audio(c,0.5f);}if(c->audio_reinit_pending&&c->capture_audio){c->audio_reinit_timer-=sec;if(c->audio_reinit_timer<=0){c->audio_reinit_pending=false;setup_audio(c,true);}}}
static void activate(void*d){schedule_audio((struct vbox_camera*)d,0.25f);}
static struct obs_source_info info={.id="vbox_camera",.type=OBS_SOURCE_TYPE_INPUT,.output_flags=OBS_SOURCE_ASYNC_VIDEO|OBS_SOURCE_AUDIO|OBS_SOURCE_DO_NOT_DUPLICATE,.get_name=get_name,.create=create,.destroy=destroy,.get_defaults=defaults,.get_properties=properties,.update=update,.activate=activate,.video_tick=tick};
bool obs_module_load(void){obs_register_source(&info);obs_log(LOG_INFO,"VBox Camera source registered (version %s)",PLUGIN_VERSION);return true;}
void obs_module_unload(void){obs_log(LOG_INFO,"VBox Camera unloaded");}
