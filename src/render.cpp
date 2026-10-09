#include "core.hpp"
#include "CScene.h"
#include <algorithm>
#include <cstring>

namespace vcsm {
static RwRaster* raster=nullptr;
static std::array<RwIm3DVertex,SM64_GEO_MAX_TRIANGLES*3> vertices{};
static std::array<RwImVertexIndex,SM64_GEO_MAX_TRIANGLES*3> indices{};
void DestroyRenderer(){HudDestroy();if(raster){RwRasterDestroy(raster);raster=nullptr;Log("renderer raster released");}}
static bool MakeRaster() {
    if(raster) return true;
    if(texture.empty()) return false;
    // D3D8 hardware can require power-of-two textures. Preserve atlas pixels, pad to 1024.
    auto image=RwImageCreate(1024,SM64_TEXTURE_HEIGHT,32);
    if(!image || !RwImageAllocatePixels(image)){if(image) RwImageDestroy(image);Log("RwImage allocation failed");return false;}
    memset(image->cpPixels,0,image->stride*SM64_TEXTURE_HEIGHT);
    for(int y=0;y<SM64_TEXTURE_HEIGHT;++y) memcpy(image->cpPixels+y*image->stride,
        texture.data()+y*SM64_TEXTURE_WIDTH*4,SM64_TEXTURE_WIDTH*4);
    raster=RwRasterCreate(1024,SM64_TEXTURE_HEIGHT,32,rwRASTERTYPETEXTURE|rwRASTERFORMAT8888);
    if(raster && !RwRasterSetFromImage(raster,image)){RwRasterDestroy(raster);raster=nullptr;}
    RwImageDestroy(image);
    Log("RW/D3D8 original atlas upload %s",raster?"OK":"FAILED");return raster!=nullptr;
}
void Render() {
    if(mario<0 || !geometry.buffers.numTrianglesUsed || !Scene.m_pCamera || !MakeRaster()) return;
    unsigned count=geometry.buffers.numTrianglesUsed*3;
    for(unsigned i=0;i<count;++i) {
        indices[i]=static_cast<RwImVertexIndex>(i);
        auto& v=vertices[i];float smooth[3];
        bool interpolate=previousGeometry.buffers.numTrianglesUsed==geometry.buffers.numTrianglesUsed;
        for(int j=0;j<3;++j){float current=geometry.position[3*i+j],prior=previousGeometry.position[3*i+j];smooth[j]=interpolate?prior+renderAlpha*(current-prior):current;}
        auto p=ToGta(smooth);
        RwIm3DVertexSetPos(&v,p.x,p.y,p.z);
        RwIm3DVertexSetNormal(&v,geometry.normal[3*i],-geometry.normal[3*i+2],geometry.normal[3*i+1]);
        RwIm3DVertexSetU(&v,geometry.uv[2*i]*(float(SM64_TEXTURE_WIDTH)/1024.f));
        RwIm3DVertexSetV(&v,geometry.uv[2*i+1]);
        auto color=[&](unsigned c){return static_cast<RwUInt8>(std::clamp(geometry.color[3*i+c],0.f,1.f)*255.f);};
        RwIm3DVertexSetRGBA(&v,color(0),color(1),color(2),255);
    }
    constexpr RwRenderState states[]={rwRENDERSTATETEXTURERASTER,rwRENDERSTATEZTESTENABLE,
        rwRENDERSTATEZWRITEENABLE,rwRENDERSTATEVERTEXALPHAENABLE,rwRENDERSTATESRCBLEND,
        rwRENDERSTATEDESTBLEND,rwRENDERSTATEFOGENABLE,rwRENDERSTATECULLMODE,rwRENDERSTATESHADEMODE,
        rwRENDERSTATETEXTUREFILTER,rwRENDERSTATETEXTUREADDRESSU,rwRENDERSTATETEXTUREADDRESSV};
    uintptr_t saved[sizeof(states)/sizeof(states[0])]{};
    for(unsigned i=0;i<std::size(states);++i) RwRenderStateGet(states[i],&saved[i]);
    auto set=[](RwRenderState s,uintptr_t value){RwRenderStateSet(s,reinterpret_cast<void*>(value));};
    set(rwRENDERSTATEZTESTENABLE,TRUE);set(rwRENDERSTATEZWRITEENABLE,TRUE);
    set(rwRENDERSTATEFOGENABLE,FALSE);set(rwRENDERSTATECULLMODE,rwCULLMODECULLNONE);
    set(rwRENDERSTATESHADEMODE,rwSHADEMODEGOURAUD);
    set(rwRENDERSTATEVERTEXALPHAENABLE,FALSE);set(rwRENDERSTATETEXTURERASTER,0);
    bool ok=false;
    if(RwIm3DTransform(vertices.data(),count,nullptr,rwIM3D_VERTEXXYZ|rwIM3D_VERTEXRGBA|rwIM3D_VERTEXUV)) {
        ok=RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST,indices.data(),count)!=FALSE;RwIm3DEnd();
    }
    // Match libsm64's example: vertex-color base + alpha texture overlay.
    for(unsigned i=0;i<count;++i) RwIm3DVertexSetRGBA(&vertices[i],255,255,255,255);
    set(rwRENDERSTATETEXTURERASTER,reinterpret_cast<uintptr_t>(raster));
    set(rwRENDERSTATEZWRITEENABLE,FALSE);set(rwRENDERSTATEVERTEXALPHAENABLE,TRUE);
    set(rwRENDERSTATESRCBLEND,rwBLENDSRCALPHA);set(rwRENDERSTATEDESTBLEND,rwBLENDINVSRCALPHA);
    set(rwRENDERSTATETEXTUREFILTER,rwFILTERLINEAR);
    set(rwRENDERSTATETEXTUREADDRESSU,rwTEXTUREADDRESSCLAMP);set(rwRENDERSTATETEXTUREADDRESSV,rwTEXTUREADDRESSCLAMP);
    if(RwIm3DTransform(vertices.data(),count,nullptr,rwIM3D_VERTEXXYZ|rwIM3D_VERTEXRGBA|rwIM3D_VERTEXUV)) {
        ok=ok && RwIm3DRenderIndexedPrimitive(rwPRIMTYPETRILIST,indices.data(),count)!=FALSE;RwIm3DEnd();
    }
    for(unsigned i=0;i<std::size(states);++i) set(states[i],saved[i]);
    static bool logged=false;if(!logged){Log("Mario render triangles=%u result=%s",geometry.buffers.numTrianglesUsed,ok?"OK":"FAILED");logged=true;}
    static DWORD interval=GetTickCount();static unsigned frames=0;++frames;
    DWORD now=GetTickCount();if(now-interval>2000){Log("render FPS=%.1f interpolated alpha=%.3f",frames*1000.f/(now-interval),renderAlpha);interval=now;frames=0;}
}
}
