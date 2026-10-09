#include "core.hpp"
#include <mmsystem.h>
#include <algorithm>

namespace vcsm {
static HWAVEOUT output=nullptr;
struct Block {WAVEHDR header{};std::array<int16_t,544*4> samples{};bool prepared=false;};
static std::array<Block,6> blocks;
static bool paused=true,signalLogged=false;
static DWORD deathAudioUntil=0;
static void SoundEvent(uint32_t bits,float*){
    unsigned bank=bits>>28,id=(bits>>16)&255;
    if((bank==2 && id==0x15) || (bank==7 && id==0x18)){
        Log("original ROM death sound requested bits=%08x",bits);deathAudioUntil=GetTickCount()+1500;}
}
void AudioInit(const std::vector<uint8_t>& rom) {
    WAVEFORMATEX format{WAVE_FORMAT_PCM,2,32000,32000*4,4,16,0};
    auto error=waveOutOpen(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
    if(error!=MMSYSERR_NOERROR){output=nullptr;Log("audio device failed code=%u",error);return;}
    // Upstream audio contains extremely verbose DEBUG_PRINT calls. Disable its callback
    // during synthesis, on this same thread, so even printf formatting is skipped.
    sm.register_debug_print_function(nullptr);sm.audio_init(rom.data());
    sm.register_debug_print_function(LibDiagnostic);waveOutPause(output);paused=true;
    sm.register_play_sound_function(SoundEvent);
    Log("ROM audio initialized; PCM stereo 32000 Hz; synthesis on game thread");
}
void AudioPump(bool pause) {
    if(!output)return;
    if(pause!=paused){paused=pause;if(paused)waveOutPause(output);else waveOutRestart(output);}
    if(paused)return;
    unsigned queued=0,frames=0;
    for(auto& b:blocks)if(b.prepared){
        if(b.header.dwFlags&WHDR_DONE){waveOutUnprepareHeader(output,&b.header,sizeof(b.header));b.prepared=false;}
        else{++queued;frames+=b.header.dwBufferLength/4;}
    }
    // libsm64 writes two blocks of n stereo frames. The return value is n, not total frames.
    for(auto& b:blocks)if(!b.prepared && queued<3){
        sm.register_debug_print_function(nullptr);
        auto n=sm.audio_tick(frames,2048,b.samples.data());
        sm.register_debug_print_function(LibDiagnostic);
        if(!n || n>544){Log("invalid audio count=%u",n);return;}
        b.header={};b.header.lpData=reinterpret_cast<char*>(b.samples.data());b.header.dwBufferLength=n*2*4;
        auto e=waveOutPrepareHeader(output,&b.header,sizeof(b.header));
        if(e!=MMSYSERR_NOERROR){Log("audio prepare failed=%u",e);return;}
        b.prepared=true;e=waveOutWrite(output,&b.header,sizeof(b.header));
        if(e!=MMSYSERR_NOERROR){Log("audio submit failed=%u",e);return;}
        ++queued;frames+=n*2;
        int peak=0;for(unsigned i=0;i<n*4;++i)peak=std::max(peak,std::abs(int(b.samples[i])));
        if(peak && !signalLogged){signalLogged=true;Log("ROM sound PCM nonzero peak=%d frames=%u",peak,n*2);}
        if(peak && deathAudioUntil && GetTickCount()<deathAudioUntil){
            Log("original death sound PCM verified peak=%d frames=%u",peak,n*2);deathAudioUntil=0;}
    }
}
void AudioClose(){if(!output)return;waveOutReset(output);
    for(auto& b:blocks)if(b.prepared){waveOutUnprepareHeader(output,&b.header,sizeof(b.header));b.prepared=false;}
    waveOutClose(output);output=nullptr;Log("audio closed; all PCM buffers released");
}
}
