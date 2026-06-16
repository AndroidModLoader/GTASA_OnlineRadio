#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <sys/time.h>
#include <ctime>
#include <cmath>
#include <dlfcn.h>

#include <Events.h>
#include <engine/Font.h>
#include <engine/RsGlobal.h>
#include <base/Timer.h>
#include <entity/PlayerPed.h>

MYMODCFG(net.rusjj.gtasa.onlineradio, GTA:SA Online Radio, 1.2, RusJJ)
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


struct timeval pTimeNow;
time_t lCurrentS;
time_t lCurrentMs;

ConfigEntry* pCurrentRadioIndex;
ConfigEntry* pRadioVolume;

uint32_t pCurrentRadio = 0;
const char** pRadioStreams;
const char** pRadioNames;
char nRadiosCount, nRadioIndex;
bool bIsRadioStarted = false;
bool bIsRadioShouldBeRendered = false;
GxtChar RadioGXT[256] { 0 };
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
void VolumeChanged(int oldVal, int newVal, void* data)
{
    pRadioVolume->SetInt(newVal);
    BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * newVal);
    cfg->Save();
}



static char szNewText[0xFF];
std::atomic<unsigned int> nRadioGen{0};
std::mutex radioMutex;
void DoRadio()
{
    unsigned int myGen = nRadioGen.fetch_add(1) + 1;

    int idx = pCurrentRadioIndex->GetInt();
    if(idx < 0) idx = nRadiosCount - 1;
    if(idx >= nRadiosCount) idx = 0;
    {
        std::lock_guard<std::mutex> lk(radioMutex);
        if(myGen != nRadioGen.load()) return;
        
        nRadioIndex = idx;
        if(pCurrentRadio)
        {
            BASS->ChannelStop(pCurrentRadio);
            BASS->StreamFree(pCurrentRadio);
            pCurrentRadio = 0;
        }
        bIsRadioStarted = false;
        bIsRadioShouldBeRendered = true;
        sprintf(szNewText, "< Current radiostation >~n~%s", pRadioNames[idx]);
        AsciiToGxtChar(szNewText, RadioGXT);
    }

    auto currentRadio = BASS->StreamCreateURL(pRadioStreams[idx], 0, BASS_STREAM_BLOCK | BASS_STREAM_STATUS | BASS_STREAM_AUTOFREE | BASS_SAMPLE_FLOAT, 0);

    std::lock_guard<std::mutex> lk(radioMutex);
    if(myGen != nRadioGen.load())
    {
        if(currentRadio) BASS->StreamFree(currentRadio);
        return;
    }
    if(currentRadio)
    {
        pCurrentRadio = currentRadio;
        BASS->ChannelSetAttribute(pCurrentRadio, BASS_ATTRIB_VOL, 0.005f * pRadioVolume->GetInt());
        bIsRadioStarted = true;
        if(!CTimer::IsPaused()) BASS->ChannelPlay(pCurrentRadio, true);
    }
    else
    {
        logger->Error("Failed to open stream! Error Code: %d", BASS->ErrorGetCode());
    }
}
DECL_HOOK(void, StartRadio, uintptr_t self, uintptr_t vehicleInfo)
{
    if(FindPlayerVehicle(-1, false) != 0)
        std::thread(DoRadio).detach();
}

DECL_HOOK(void, StopRadio, uintptr_t self, uintptr_t vehicleInfo, unsigned char flag)
{
    nRadioGen.fetch_add(1);
    {
        std::lock_guard<std::mutex> lk(radioMutex);
        if(!CTimer::IsPaused())
        {
            bIsRadioStarted = false;
            if(pCurrentRadio)
            {
                BASS->ChannelStop(pCurrentRadio);
                BASS->StreamFree(pCurrentRadio);
                pCurrentRadio = 0;
            }
            nRadioIndex = -1;
        }
        bIsRadioShouldBeRendered = false;
    }
    StopRadio(self, vehicleInfo, flag);
}

static char szTemp[16];
ON_MOD_LOAD()
{
    pGTASA = aml->GetLib("libGTASA.so");
    hGTASA = aml->GetLibHandle("libGTASA.so");

    BASS = (IBASS*)GetInterface("BASS");
    BASS->SetConfig(BASS_CONFIG_NET_TIMEOUT, 5000);

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
        if(pRadioVolume->GetInt() > 100) pRadioVolume->SetInt(100);
        else if(pRadioVolume->GetInt() < 0) pRadioVolume->SetInt(0);
        cfg->Save();
    }
    else 
    {
        logger->Error("There is no radios in the config! Mod is not loaded.");
        return;
    }

    HOOKPLT(PauseGame,          pGTASA + BYBIT(0x672644, 0x844230));
    HOOKPLT(ResumeGame,         pGTASA + BYBIT(0x67056C, 0x840CB0));
    HOOKPLT(StartRadio,         pGTASA + BYBIT(0x66F738, 0x83F5C0));
    HOOK(StopRadio,             aml->GetSym(hGTASA, "_ZN20CAERadioTrackManager9StopRadioEP21tVehicleAudioSettingsh"));

    aml->PlaceB(pGTASA + BYBIT(0x2A4D28 + 0x1, 0x3638A4), pGTASA + BYBIT(0x2A4D3C + 0x1, 0x3638C0)); // Remove radio from Audio settings
    sautils = (ISAUtils*)GetInterface("SAUtils");
    if(sautils)
    {
        sautils->AddSliderItem(SetType_Audio, "Online-Radio Volume", pRadioVolume->GetInt(), 0, 100, VolumeChanged);
    }

    Events::drawHudEvent.after += []()
    {
        if(bIsRadioShouldBeRendered)
        {
            float flScale = (float)RsGlobal.maximumHeight / 540.0f;
            CFont::SetScale(flScale);
            CFont::SetColor(bIsRadioStarted ? clrRadioPlaying : clrRadioLoading);
            CFont::SetFontStyle(FO_FONT_STYLE_HEADING);
            CFont::SetEdge(1);
            CFont::SetOrientation(ALIGN_CENTER);
            CFont::SetProportional(1);
            //SetFontAlphaFade(1.0f);
            CFont::PrintString(0.5f * RsGlobal.maximumWidth, 0.02f * RsGlobal.maximumHeight, RadioGXT);
            CFont::RenderFontBuffer();
        }
    };

    Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
    {
        if(bIsRadioShouldBeRendered && type == 2 /*TOUCH_PRESS*/)
        {
            if(/*!bRadioPending &&*/
                y < (RsGlobal.maximumHeight * 0.135f) &&
                x > (RsGlobal.maximumWidth * 0.33f) &&
                x < (RsGlobal.maximumWidth * 0.66f) )
            {
                if(x > (RsGlobal.maximumWidth * 0.5f) )
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
            // No slider for y'all
        }
    };
}
