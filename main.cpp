#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <thread>
#include <sys/time.h>
#include <ctime>
#include <cmath>
#include <dlfcn.h>

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio, 1.0.1, RusJJ)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

#define MAX_RADIOS 32

#define STYLE_GOTHIC 0
#define STYLE_REGULAR 1
#define STYLE_PRESSED 2
#define STYLE_LOGOSTYLED 3

#define ALIGN_CENTER 0
#define ALIGN_LEFT 1
#define ALIGN_RIGHT 2

class CRGBA
{
public:
	union {
		struct { unsigned char red, green, blue, alpha; };
    };
	CRGBA() {}
	CRGBA(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha)
	{
		this->red = red;
		this->green = green;
		this->blue = blue;
		this->alpha = alpha;
	}
};

#include "ibass.h"
IBASS* BASS = nullptr;

#include "isautils.h"
ISAUtils* sautils = nullptr;

uintptr_t pGTASA = 0;
void* pGTASAHandle = nullptr;
struct timeval pTimeNow;
time_t lCurrentS;
time_t lCurrentMs;

ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

void (*SetFontScale)(float x, float y);
void (*SetFontColor)(CRGBA* clr);
void (*SetFontStyle)(unsigned char style);
void (*SetFontEdge)(signed char countOf);
void (*SetFontAlignment)(unsigned char align);
void (*SetFontAlphaFade)(float alpha);
bool (*PrintString)(float x, float y, unsigned short* gxtText);
void (*AsciiToGxt)(const char* txt, unsigned short* saveTo);
void (*RenderFontBuffer)(void);
uintptr_t (*FindPlayerVehicle)(int playerId, bool includeRemote);
int* ScreenX;
int* ScreenY;

uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;
char nRadiosCount, nRadioIndex;
bool bIsRadioStarted = false;
bool bIsRadioShouldBeRendered = false;
unsigned short* pGXT = new unsigned short[0xFF];
CRGBA clrRadioLoading(255, 228, 181, 255);
CRGBA clrRadioPlaying(255, 255, 255, 255);

inline time_t GetCurrentTimeS()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentS = pTimeNow.tv_sec;
    return lCurrentS;
}
inline time_t GetCurrentTimeMs()
{
    gettimeofday(&pTimeNow, nullptr);
    lCurrentMs = (1000 * pTimeNow.tv_sec) + (0.001f * pTimeNow.tv_usec);
    return lCurrentMs;
}
inline bool IsGamePaused() { return *(bool*)(pGTASA + 0x96B514); };

DECL_HOOK(void, PreRenderEnd, void* self)
{
    PreRenderEnd(self);
    if(bIsRadioShouldBeRendered)
    {
        static float flScale;
        flScale = 2160.0f / (float)*ScreenY;
        SetFontScale(flScale, flScale);
        SetFontColor(bIsRadioStarted ? &clrRadioPlaying : &clrRadioLoading);
        SetFontStyle(STYLE_LOGOSTYLED);
        SetFontEdge(1);
        SetFontAlignment(ALIGN_CENTER);
        //SetFontAlphaFade(1.0f);
        PrintString(0.5f * *ScreenX, 0.02f * *ScreenY, pGXT);
        RenderFontBuffer();
    }
}

DECL_HOOK(bool, PauseGame, void* self)
{
    bIsRadioShouldBeRendered = false;
    if(pCurrentRadio != 0) BASS->ChannelPause(pCurrentRadio);
    return PauseGame(self);
}

DECL_HOOK(bool, ResumeGame, void* self)
{
    if(pCurrentRadio != 0)
    {
        bIsRadioShouldBeRendered = true;
        BASS->ChannelPlay(pCurrentRadio, false);
    }
    return ResumeGame(self);
}

void VolumeChanged(int oldVal, int newVal)
{
    pRadioVolume->SetInt(newVal);
    BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * newVal);
    cfg->Save();
}
static char szNewText[0xFF];
void DoRadio()
{
    nRadioIndex = pCurrentRadioIndex->GetInt();
    if(nRadioIndex < 0) nRadioIndex = nRadiosCount - 1;
    if(nRadioIndex >= nRadiosCount) nRadioIndex = 0;
    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }
    bIsRadioStarted = false;
    bIsRadioShouldBeRendered = true;

    sprintf(szNewText, "< Current radiostation >~n~%s", pRadioNames[nRadioIndex]);
    AsciiToGxt(szNewText, pGXT);
    char myIndex = nRadioIndex;
    auto currentRadio = BASS->StreamCreateURL(pRadioStreams[nRadioIndex], 0, BASS_STREAM_BLOCK | BASS_STREAM_STATUS | BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT, 0);
    if(currentRadio)
    {
        if(nRadioIndex == myIndex)
        {
            pCurrentRadio = currentRadio;
            BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * pRadioVolume->GetInt());
            if(!IsGamePaused()) BASS->ChannelPlay(pCurrentRadio, true);
            bIsRadioStarted = true;
        }
        else
        {
            BASS->StreamFree(currentRadio);
        }
    }
    else
    {
        logger->Error("Failed to open stream! Error Code: %d", BASS->ErrorGetCode());
        //StartRadio(pSaved[0], pSaved[1]);
    }
}
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false) != 0)
        std::thread(DoRadio).detach();
}

DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    if(!IsGamePaused())
    {
        bIsRadioStarted = false;
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
        nRadioIndex = -1;
    }
    bIsRadioShouldBeRendered = false;
    StopRadio(self, vehicleInfo, flag);
}

#define TOUCH_UNPRESS 1
#define TOUCH_PRESS 2
#define TOUCH_MOVE 3
DECL_HOOK(void, TouchEvent, int type, int finger, int x, int y)
{
    //sprintf(szNewText, "Type: %d~n~Finger: %d~n~XY: %d %d", type, finger, x, y);
    //AsciiToGxt(szNewText, pGXT);
    if(bIsRadioShouldBeRendered) switch(type)
    {
        case TOUCH_PRESS:
            if(y < *ScreenY * 0.135f && x > *ScreenX * 0.33f && x < *ScreenX * 0.66f)
            {
                if(x > *ScreenX * 0.5f)
                {
                    ++nRadioIndex;
                }
                else
                {
                    --nRadioIndex;
                }
                pCurrentRadioIndex->SetInt(nRadioIndex);
                cfg->Save();
                std::thread(DoRadio).detach();
            }
            break;
        // No slider for ya
    }
    TouchEvent(type, finger, x, y);
}

static char szTemp[16];
extern "C" void OnModLoad()
{
    pGXT[0] = 0;
    pGTASA = aml->GetLib("libGTASA.so");
    pGTASAHandle = dlopen("libGTASA.so", RTLD_LAZY);

    BASS = (IBASS*)GetInterface("BASS");
    BASS->SetConfig(BASS_CONFIG_NET_TIMEOUT, 10000);

    pCurrentRadioIndex = cfg->Bind("CurrentRadioIndex", 0);
    pRadioVolume = cfg->Bind("RadioVolume", 80);
    nRadiosCount = cfg->Bind("RadiosCount", 0)->GetInt();
    if(nRadiosCount > 0)
    {
        if(nRadiosCount > MAX_RADIOS) nRadiosCount = MAX_RADIOS;
        if(pCurrentRadioIndex->GetInt() < 0) pCurrentRadioIndex->SetInt(0);
        if(pCurrentRadioIndex->GetInt() > MAX_RADIOS) pCurrentRadioIndex->SetInt(MAX_RADIOS);

        nRadioIndex = -1;
        pRadioStreams = new const char*[nRadiosCount];
        pRadioNames = new const char*[nRadiosCount];
        for(int i = 0; i < nRadiosCount; ++i)
        {
            sprintf(szTemp, "Radio_%d", i+1);
            pRadioStreams[i] = cfg->Bind(szTemp, "", "URLs")->GetString();
            pRadioNames[i] = cfg->Bind(szTemp, "Untitled Radio", "Names")->GetString();
        }
        if(pRadioVolume->GetInt() > 100)
        {
            pRadioVolume->SetInt(100);
        }
        else if(pRadioVolume->GetInt() < 0)
        {
            pRadioVolume->SetInt(0);
        }
        cfg->Save();
    }

    SetFontScale =      (void(*)(float, float))dlsym(pGTASAHandle, "_ZN5CFont8SetScaleEf");
    SetFontColor =      (void(*)(CRGBA*))dlsym(pGTASAHandle, "_ZN5CFont8SetColorE5CRGBA");
    SetFontStyle =      (void(*)(unsigned char))dlsym(pGTASAHandle, "_ZN5CFont12SetFontStyleEh");
    SetFontEdge =       (void(*)(signed char))dlsym(pGTASAHandle, "_ZN5CFont7SetEdgeEa");
    SetFontAlignment =  (void(*)(unsigned char))dlsym(pGTASAHandle, "_ZN5CFont14SetOrientationEh");
    SetFontAlphaFade =  (void(*)(float))dlsym(pGTASAHandle, "_ZN5CFont12SetAlphaFadeEf");
    PrintString =       (bool(*)(float, float, unsigned short*))dlsym(pGTASAHandle, "_ZN5CFont11PrintStringEffPt");
    AsciiToGxt =        (void(*)(const char*, unsigned short*))dlsym(pGTASAHandle, "_Z14AsciiToGxtCharPKcPt");
    RenderFontBuffer =  (void(*)(void))dlsym(pGTASAHandle, "_ZN5CFont16RenderFontBufferEv");
    FindPlayerVehicle = (uintptr_t(*)(int, bool))dlsym(pGTASAHandle, "_Z17FindPlayerVehicleib");

    ScreenX = (int*)(pGTASA + 0x6855B4);
    ScreenY = (int*)(pGTASA + 0x6855B8);

    HOOKPLT(PreRenderEnd, pGTASA + 0x674188);
    HOOKPLT(PauseGame,    pGTASA + 0x672644);
    HOOKPLT(ResumeGame,   pGTASA + 0x67056C);
    HOOKPLT(StartRadio,   pGTASA + 0x66F738);
    HOOKPLT(StopRadio,    pGTASA + 0x671284);
    HOOKPLT(TouchEvent,   pGTASA + 0x675DE4);

    sautils = (ISAUtils*)GetInterface("SAUtils");
    sautils = (ISAUtils*)GetInterface("SAUtils");
    if(sautils)
    {
        sautils->AddSliderItem(Audio, "Online-Radio Volume", pRadioVolume->GetInt(), 0, 100, VolumeChanged);
    }
}