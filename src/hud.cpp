#include "core.hpp"
#include "rom_hud.hpp"
#include "CScene.h"
#include "CTimer.h"
#include "CHud.h"
#include <algorithm>
#include <cstring>

namespace vcsm {
struct HudTexture {unsigned w=0,h=0;std::vector<uint8_t> pixels;RwRaster* raster=nullptr;};
// Digits 0..9, multiply, coin, Mario, star, camera, Lakitu, POWER left/right, 8..1 wedges.
static std::array<HudTexture,27> hud;
void HudInit(const std::vector<uint8_t>& rom){
    try{
        auto glyphs=DecodeMio0(rom,1083968),power=DecodeMio0(rom,2102288);
        auto add=[](unsigned i,const auto& segment,size_t off,unsigned w,unsigned h){hud[i].w=w;hud[i].h=h;hud[i].pixels=DecodeRgba16(segment,off,w,h);};
        for(unsigned i=0;i<10;++i)add(i,glyphs,i*0x200,16,16);
        // File names in assets.json use Japanese segment labels. US offsets differ.
        // These values are from each entry's "us" tuple, not from its filename.
        const unsigned offsets[]={16896,17408,17920,18432,28672,29184};
        for(unsigned i=0;i<6;++i)add(10+i,glyphs,offsets[i],16,16);
        add(16,power,144352,32,64);add(17,power,148448,32,64);
        for(unsigned i=0;i<8;++i)add(18+i,power,152544+i*2048,32,32);
        // Original mirrored IA8 Bowser death mask, US segment2 metadata offset.
        auto& mask=hud[26];mask.w=mask.h=64;mask.pixels.resize(64*64*4);
        if(glyphs.size()<82616+2048)throw std::runtime_error("Bowser transition ROM range");
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){unsigned src=x<32?31-x:x-32;
            auto i=(y*64+x)*4;mask.pixels[i]=mask.pixels[i+1]=mask.pixels[i+2]=0;
            mask.pixels[i+3]=(glyphs[82616+y*32+src]&15)*17;}
        Log("HUD original ROM sprites decoded: 26 RGBA16 textures");
    }catch(const std::exception& e){Log("HUD ROM decode failed: %s",e.what());}
}
void HudDestroy(){for(auto& t:hud)if(t.raster){RwRasterDestroy(t.raster);t.raster=nullptr;}}
static bool Upload(HudTexture& t){
    if(t.raster)return true;if(t.pixels.empty())return false;
    auto image=RwImageCreate(t.w,t.h,32);if(!image)return false;
    if(!RwImageAllocatePixels(image)){RwImageDestroy(image);return false;}
    for(unsigned y=0;y<t.h;++y)memcpy(image->cpPixels+y*image->stride,t.pixels.data()+y*t.w*4,t.w*4);
    t.raster=RwRasterCreate(t.w,t.h,32,rwRASTERTYPETEXTURE|rwRASTERFORMAT8888);
    if(t.raster && !RwRasterSetFromImage(t.raster,image)){RwRasterDestroy(t.raster);t.raster=nullptr;}
    RwImageDestroy(image);return t.raster!=nullptr;
}
static void Quad(unsigned sprite,float x,float y,float w,float h){
    auto& t=hud[sprite];if(!Upload(t))return;
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,t.raster);
    RwIm2DVertex v[4]{};float xs[]={x,x,x+w,x+w},ys[]={y,y+h,y,y+h},us[]={0,0,1,1},vs[]={0,1,0,1};
    for(int i=0;i<4;++i){RwIm2DVertexSetScreenX(&v[i],xs[i]);RwIm2DVertexSetScreenY(&v[i],ys[i]);
        RwIm2DVertexSetScreenZ(&v[i],RwIm2DGetNearScreenZ());RwIm2DVertexSetRecipCameraZ(&v[i],1.f);
        RwIm2DVertexSetIntRGBA(&v[i],255,255,255,255);RwIm2DVertexSetU(&v[i],us[i],1.f);RwIm2DVertexSetV(&v[i],vs[i],1.f);}
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP,v,4);
}
static void Solid(float x,float y,float w,float h){
    if(w<=0 || h<=0)return;
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,nullptr);
    RwIm2DVertex v[4]{};float xs[]={x,x,x+w,x+w},ys[]={y,y+h,y,y+h};
    for(int i=0;i<4;++i){RwIm2DVertexSetScreenX(&v[i],xs[i]);RwIm2DVertexSetScreenY(&v[i],ys[i]);
        RwIm2DVertexSetScreenZ(&v[i],RwIm2DGetNearScreenZ());RwIm2DVertexSetRecipCameraZ(&v[i],1.f);
        RwIm2DVertexSetIntRGBA(&v[i],0,0,0,255);}
    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP,v,4);
}
void HudRender(){
    float fade=DeathTransition();
    if((mario<0 && fade<=0) || CTimer::m_UserPause || !Scene.m_pCamera || hud[0].pixels.empty())return;
    auto screen=RwCameraGetRaster(Scene.m_pCamera);if(!screen)return;
    float scale=std::min(screen->width/320.f,screen->height/240.f)*0.72f;
    constexpr RwRenderState states[]={rwRENDERSTATETEXTURERASTER,rwRENDERSTATEZTESTENABLE,rwRENDERSTATEZWRITEENABLE,
        rwRENDERSTATEVERTEXALPHAENABLE,rwRENDERSTATESRCBLEND,rwRENDERSTATEDESTBLEND,rwRENDERSTATETEXTUREFILTER,
        rwRENDERSTATETEXTUREADDRESSU,rwRENDERSTATETEXTUREADDRESSV,rwRENDERSTATEFOGENABLE,rwRENDERSTATECULLMODE};
    uintptr_t saved[std::size(states)]{};for(unsigned i=0;i<std::size(states);++i)RwRenderStateGet(states[i],&saved[i]);
    auto set=[](RwRenderState state,uintptr_t value){RwRenderStateSet(state,reinterpret_cast<void*>(value));};
    set(rwRENDERSTATEZTESTENABLE,FALSE);set(rwRENDERSTATEZWRITEENABLE,FALSE);set(rwRENDERSTATEVERTEXALPHAENABLE,TRUE);
    set(rwRENDERSTATESRCBLEND,rwBLENDSRCALPHA);set(rwRENDERSTATEDESTBLEND,rwBLENDINVSRCALPHA);
    set(rwRENDERSTATETEXTUREFILTER,rwFILTERNEAREST);set(rwRENDERSTATETEXTUREADDRESSU,rwTEXTUREADDRESSCLAMP);
    set(rwRENDERSTATETEXTUREADDRESSV,rwTEXTUREADDRESSCLAMP);set(rwRENDERSTATEFOGENABLE,FALSE);set(rwRENDERSTATECULLMODE,rwCULLMODECULLNONE);
    auto glyph=[&](unsigned id,float x,float y){Quad(id,x*scale,y*scale,16*scale,16*scale);};
    auto number=[&](unsigned value,float x,float y){
        char digits[12]{};sprintf(digits,"%u",std::min(value,999u));
        for(unsigned i=0;digits[i];++i)glyph(digits[i]-'0',x+14*i,y);
    };
    float row=CHud::m_HelpMessageState?90.f:20.f;
    glyph(12,20,row);glyph(10,38,row);number(MarioLives(),54,row);
    glyph(11,100,row);glyph(10,118,row);number(MarioCoins(),134,row);
    glyph(13,200,row);glyph(10,218,row);number(MarioStars(),234,row);
    float cameraX=screen->width/scale-54.f,cameraY=screen->height/scale-35.f;
    glyph(14,cameraX,cameraY);glyph(15,cameraX+16,cameraY);
    unsigned wedges=std::clamp(unsigned(state.health>>8),0u,8u);
    if(wedges<8 || (state.action&0x2000)){
        float x=screen->width/2.f,y=70*scale;Quad(16,x-32*scale,y-32*scale,32*scale,64*scale);
        Quad(17,x,y-32*scale,32*scale,64*scale);if(wedges)Quad(18+8-wedges,x-16*scale,y-16*scale,32*scale,32*scale);
    }
    if(fade>0){
        if(fade>=1)Solid(0,0,float(screen->width),float(screen->height));
        else {
            float radius=(320.f-304.f*fade)*std::min(screen->width/320.f,screen->height/240.f);
            float left=screen->width/2.f-radius,top=screen->height/2.f-radius,right=left+2*radius,bottom=top+2*radius;
            Solid(0,0,float(screen->width),std::max(0.f,top));
            Solid(0,std::max(0.f,bottom),float(screen->width),screen->height-bottom);
            Solid(0,std::max(0.f,top),std::max(0.f,left),std::min(float(screen->height),bottom)-std::max(0.f,top));
            Solid(std::max(0.f,right),std::max(0.f,top),screen->width-right,std::min(float(screen->height),bottom)-std::max(0.f,top));
            set(rwRENDERSTATETEXTUREFILTER,rwFILTERLINEAR);
            Quad(26,left,top,2*radius,2*radius);
        }
    }
    for(unsigned i=0;i<std::size(states);++i)set(states[i],saved[i]);
    static bool logged=false;if(!logged){logged=true;Log("HUD ROM textures rendered; POWER reads live Mario health");}
}
}
