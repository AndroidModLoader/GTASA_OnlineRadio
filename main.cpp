#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <thread>
#include <sys/time.h>
#include <cmath>
#include <dlfcn.h>

#include <Events.h>
#include <engine/Font.h>
#include <engine/RsGlobal.h>
#include <base/Timer.h>
#include <entity/PlayerPed.h>

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio, 1.2-fixed, RusJJ)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

#define MAX_RADIOS 32

#include "ibass.h"
IBASS* BASS = nullptr;

#include "isautils.h"
ISAUtils* sautils = nullptr;

uintptr_t pGTASA = 0;
void* hGTASA = NULL;

// ================= CONFIG =================
ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;

char nRadiosCount = 0;
int nRadioIndex = 0;

bool bIsRadioStarted = false;
bool bIsRadioShouldBeRendered = false;
bool bRadioPending = false;

GxtChar RadioGXT[256]{0};
CRGBA clrRadioLoading(255, 228, 181, 255);
CRGBA clrRadioPlaying(255, 255, 255, 255);

// ================= TIME (unused safe) =================
struct timeval pTimeNow;

// ================= SAFETY =================
inline void ClampRadioIndex()
{
    if(nRadiosCount <= 0) return;

    if(nRadioIndex < 0)
        nRadioIndex = nRadiosCount - 1;

    if(nRadioIndex >= nRadiosCount)
        nRadioIndex = 0;
}

// ================= RADIO CORE =================
void DoRadio()
{
    if(bRadioPending) return;
    bRadioPending = true;

    ClampRadioIndex();

    int index = nRadioIndex;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    bIsRadioShouldBeRendered = true;

    sprintf((char*)RadioGXT, "< Current radiostation >~n~%s", pRadioNames[index]);

    auto stream = BASS->StreamCreateURL(
        pRadioStreams[index],
        0,
        BASS_STREAM_BLOCK | BASS_STREAM_STATUS | BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT,
        0
    );

    if(stream)
    {
        pCurrentRadio = stream;

        BASS->ChannelSetAttribute(
            pCurrentRadio,
            BASS_ATTRIB_VOL,
            0.005f * pRadioVolume->GetInt()
        );

        if(!CTimer::IsPaused())
        {
            BASS->ChannelPlay(pCurrentRadio, true);
            bIsRadioStarted = true;
        }
    }
    else
    {
        logger->Error("Radio failed to open stream: %d", BASS->ErrorGetCode());
    }

    bRadioPending = false;
}

// ================= START RADIO =================
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false))
    {
        nRadioIndex = pCurrentRadioIndex->GetInt();
        std::thread(DoRadio).detach();
    }

    StartRadio(self, vehicleInfo);
}

// ================= STOP RADIO =================
DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    bIsRadioStarted = false;
    bRadioPending = false;

    if(pCurrentRadio)
    {
        BASS->ChannelStop(pCurrentRadio);
        BASS->StreamFree(pCurrentRadio);
        pCurrentRadio = 0;
    }

    nRadioIndex = 0;
    bIsRadioShouldBeRendered = false;

    StopRadio(self, vehicleInfo, flag);
}

// ================= VOLUME =================
void VolumeChanged(int oldVal, int newVal, void* data)
{
    pRadioVolume->SetInt(newVal);

    if(pCurrentRadio)
        BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * newVal);

    cfg->Save();
}

// ================= TOUCH UI =================
Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
{
    if(!bIsRadioShouldBeRendered || type != 2)
        return;

    if(bRadioPending)
        return;

    float top = RsGlobal.maximumHeight * 0.135f;
    float left = RsGlobal.maximumWidth * 0.33f;
    float right = RsGlobal.maximumWidth * 0.66f;
    float mid = RsGlobal.maximumWidth * 0.5f;

    if(y < top && x > left && x < right)
    {
        if(x > mid)
            nRadioIndex++;
        else
            nRadioIndex--;

        ClampRadioIndex();

        pCurrentRadioIndex->SetInt(nRadioIndex);
        cfg->Save();

        std::thread(DoRadio).detach();
    }
};

// ================= HUD =================
Events::drawHudEvent.after += []()
{
    if(!bIsRadioShouldBeRendered)
        return;

    float scale = (float)RsGlobal.maximumHeight / 540.0f;

    CFont::SetScale(scale);
    CFont::SetColor(bIsRadioStarted ? clrRadioPlaying : clrRadioLoading);
    CFont::SetFontStyle(FO_FONT_STYLE_HEADING);
    CFont::SetEdge(1);
    CFont::SetOrientation(ALIGN_CENTER);

    CFont::PrintString(
        0.5f * RsGlobal.maximumWidth,
        0.02f * RsGlobal.maximumHeight,
        RadioGXT
    );

    CFont::RenderFontBuffer();
};

// ================= MOD LOAD =================
ON_MOD_LOAD()
{
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    BASS = (IBASS*)GetInterface("BASS");
    BASS->SetConfig(BASS_CONFIG_NET_TIMEOUT, 5000);

    pCurrentRadioIndex = cfg->Bind("CurrentRadioIndex", 0);
    pRadioVolume = cfg->Bind("RadioVolume", 80);

    nRadiosCount = cfg->Bind("RadiosCount", 0)->GetInt();
    if(nRadiosCount > MAX_RADIOS) nRadiosCount = MAX_RADIOS;

    if(nRadiosCount <= 0)
    {
        logger->Error("No radios found in config!");
        return;
    }

    pRadioStreams = new const char*[nRadiosCount];
    pRadioNames   = new const char*[nRadiosCount];

    char key[64];

    for(int i = 0; i < nRadiosCount; i++)
    {
        sprintf(key, "Radio_%d", i + 1);

        pRadioStreams[i] = cfg->Bind(key, "", "URLs")->GetString();
        pRadioNames[i]   = cfg->Bind(key, "Untitled Radio", "Names")->GetString();
    }

    cfg->Save();

    HOOKPLT(StartRadio, pGTASA + BYBIT(0x66F738, 0x83F5C0));
    HOOK(StopRadio, aml->GetSym(hGTASA,
        "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh"));
}
