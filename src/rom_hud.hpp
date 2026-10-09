#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

namespace vcsm {
// Runtime decoding of the user's ROM only. Metadata: n64decomp/sm64 assets.json,
// pinned 06ec56df7f951f88da05f468cdcacecba496145a. No texture pixels in the plugin.
inline std::vector<uint8_t> DecodeMio0(const std::vector<uint8_t>& rom,size_t base) {
    if(base+16>rom.size() || memcmp(rom.data()+base,"MIO0",4))throw std::runtime_error("HUD MIO0 header");
    auto be=[&](size_t p){return uint32_t(rom[p])<<24|uint32_t(rom[p+1])<<16|uint32_t(rom[p+2])<<8|rom[p+3];};
    size_t size=be(base+4),comp=base+be(base+8),raw=base+be(base+12),bits=base+16;
    if(!size || size>8388608 || comp>=rom.size() || raw>=rom.size())throw std::runtime_error("HUD MIO0 size");
    std::vector<uint8_t> out;out.reserve(size);unsigned mask=0,flags=0;
    while(out.size()<size){
        if(!mask){if(bits>=rom.size())throw std::runtime_error("HUD MIO0 bits");flags=rom[bits++];mask=128;}
        if(flags&mask){if(raw>=rom.size())throw std::runtime_error("HUD MIO0 literal");out.push_back(rom[raw++]);}
        else{if(comp+2>rom.size())throw std::runtime_error("HUD MIO0 pair");unsigned pair=unsigned(rom[comp])<<8|rom[comp+1];comp+=2;
            unsigned count=(pair>>12)+3,back=(pair&4095)+1;
            if(back>out.size() || out.size()+count>size)throw std::runtime_error("HUD MIO0 backref");
            while(count--)out.push_back(out[out.size()-back]);
        }
        mask>>=1;
    }
    return out;
}
inline std::vector<uint8_t> DecodeRgba16(const std::vector<uint8_t>& segment,size_t offset,unsigned width,unsigned height){
    size_t count=size_t(width)*height;if(offset+count*2>segment.size())throw std::runtime_error("HUD texture range");
    std::vector<uint8_t> pixels(count*4);
    for(size_t i=0;i<count;++i){unsigned v=unsigned(segment[offset+2*i])<<8|segment[offset+2*i+1];
        pixels[4*i]=((v>>11)&31)*255/31;pixels[4*i+1]=((v>>6)&31)*255/31;
        pixels[4*i+2]=((v>>1)&31)*255/31;pixels[4*i+3]=(v&1)?255:0;
    }
    return pixels;
}
}
