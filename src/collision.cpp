#include "core.hpp"
#include "CPools.h"
#include "CModelInfo.h"
#include "CWorld.h"
#include "CSector.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace vcsm {
// In-memory VC triangles, not disk COL1 faces. Verified in the user's EXE:
// CalculateTrianglePlanes increments triangles by 8; GetTrianglePoint uses float3 stride 12.
struct VcTriangle {uint16_t a,b,c; uint8_t material,piece;};
static_assert(sizeof(VcTriangle)==8);
static std::vector<SM64Surface> previous;
static unsigned lastFrame=0;
static CVector lastCenter(1e9f,1e9f,1e9f);
static constexpr float Radius=40.f; // > six seconds at walk speed; refreshed every 250 ms / 5 m.
static bool Readable(const void* p,size_t size) {
    if(!p || !size) return false;
    auto a=reinterpret_cast<uintptr_t>(p), end=a+size;
    if(end<a) return false;
    while(a<end) {
        MEMORY_BASIC_INFORMATION m{};
        if(!VirtualQuery(reinterpret_cast<void*>(a),&m,sizeof(m)) || m.State!=MEM_COMMIT
           || (m.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        a=reinterpret_cast<uintptr_t>(m.BaseAddress)+m.RegionSize;
    }
    return true;
}
static CVector World(CEntity* e,const CVector& v) {
    return CVector(e->pos.x+e->right.x*v.x+e->up.x*v.y+e->at.x*v.z,
                   e->pos.y+e->right.y*v.x+e->up.y*v.y+e->at.y*v.z,
                   e->pos.z+e->right.z*v.x+e->up.z*v.y+e->at.z*v.z);
}
static void Triangle(std::vector<SM64Surface>& out,CVector a,CVector b,CVector c,const CVector& center) {
    constexpr float radius=Radius;
    if(std::min({a.x,b.x,c.x})>center.x+radius || std::max({a.x,b.x,c.x})<center.x-radius
      || std::min({a.y,b.y,c.y})>center.y+radius || std::max({a.y,b.y,c.y})<center.y-radius) return;
    CVector ab(b.x-a.x,b.y-a.y,b.z-a.z), ac(c.x-a.x,c.y-a.y,c.z-a.z);
    float nx=ab.y*ac.z-ab.z*ac.y,ny=ab.z*ac.x-ab.x*ac.z,nz=ab.x*ac.y-ab.y*ac.x;
    if(nx*nx+ny*ny+nz*nz<0.000001f) return;
    SM64Surface s{}; // DEFAULT surface, stone terrain. Preserve original winding.
    CVector v[]={ToSm(a),ToSm(b),ToSm(c)};
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) {
        float f=j==0?v[i].x:j==1?v[i].y:v[i].z;
        if(!std::isfinite(f) || std::abs(f)>10000000.f) return;
        s.vertices[i][j]=static_cast<int32_t>(std::lround(f));
    }
    // Quantization can collapse tiny primitives even when the original float triangle was valid.
    int64_t u[3],w[3];for(int j=0;j<3;++j){u[j]=int64_t(s.vertices[1][j])-s.vertices[0][j];w[j]=int64_t(s.vertices[2][j])-s.vertices[0][j];}
    if(u[1]*w[2]==u[2]*w[1] && u[2]*w[0]==u[0]*w[2] && u[0]*w[1]==u[1]*w[0]) return;
    out.push_back(s);
}
static void Entity(std::vector<SM64Surface>& out,CEntity* e,const CVector& center,unsigned& entities,unsigned& rejected) {
    if(!e || !e->bUsesCollision || e->m_nModelIndex<0) return;
    auto mi=CModelInfo::GetModelInfo(e->m_nModelIndex);
    if(!mi || !mi->m_pColModel) return;
    auto col=mi->m_pColModel;
    if(!Readable(col,sizeof(*col))) {++rejected;return;}
    // Include large meshes intersecting the radius, even when entity origin is far away.
    CVector localCenter=col->m_boundSphere.m_vecCenter;
    CVector wc=World(e,localCenter);
    float r=col->m_boundSphere.m_fRadius+Radius*1.415f;
    float dx=wc.x-center.x,dy=wc.y-center.y;
    if(dx*dx+dy*dy>r*r) return;
    ++entities;
    if(col->m_nNumTriangles && col->m_pTriangles && col->m_pVertices) {
        auto tris=static_cast<VcTriangle*>(col->m_pTriangles);
        if(!Readable(tris,col->m_nNumTriangles*sizeof(VcTriangle))) {++rejected;return;}
        unsigned maxIndex=0;
        for(unsigned i=0;i<col->m_nNumTriangles;++i) maxIndex=std::max({maxIndex,unsigned(tris[i].a),unsigned(tris[i].b),unsigned(tris[i].c)});
        auto verts=static_cast<CVector*>(col->m_pVertices);
        if(!Readable(verts,(maxIndex+1)*sizeof(CVector))) {++rejected;return;}
        for(unsigned i=0;i<col->m_nNumTriangles;++i) {
            auto& t=tris[i];
            // VC collision planes use the opposite winding to SM64. Proven against
            // ProcessVerticalLine: road model 3103 cross.z=-79.215, hit normal.z=+1.
            Triangle(out,World(e,verts[t.a]),World(e,verts[t.c]),World(e,verts[t.b]),center);
        }
    }
    // Bounds are only a filter. These are actual collision boxes, triangulated exactly.
    if(col->m_nNumBoxes && Readable(col->m_pBoxes,col->m_nNumBoxes*sizeof(CColBox))) {
        constexpr int faces[][3]={{0,2,1},{1,2,3},{4,5,6},{5,7,6},{0,1,4},{1,5,4},
          {2,6,3},{3,6,7},{0,4,2},{2,4,6},{1,3,5},{3,7,5}};
        for(unsigned k=0;k<col->m_nNumBoxes;++k) {
            auto& b=col->m_pBoxes[k]; CVector v[8];
            for(int i=0;i<8;++i) v[i]=World(e,CVector(i&1?b.m_vecMax.x:b.m_vecMin.x,
                i&2?b.m_vecMax.y:b.m_vecMin.y,i&4?b.m_vecMax.z:b.m_vecMin.z));
            for(auto& f:faces) Triangle(out,v[f[0]],v[f[1]],v[f[2]],center);
        }
    }
    // Actual spherical collision primitives, approximated with a bounded latitude mesh.
    if(col->m_nNumSpheres && Readable(col->m_pSpheres,col->m_nNumSpheres*sizeof(CColSphere))) {
        const int rings=e->m_nType==2?4:8,segments=e->m_nType==2?8:16;
        constexpr float pi=3.14159265359f;
        for(unsigned k=0;k<col->m_nNumSpheres;++k) {
            auto& s=col->m_pSpheres[k];
            auto point=[&](int i,int j){float lat=-pi/2+pi*i/rings,lon=2*pi*j/segments;
                return World(e,CVector(s.m_vecCenter.x+s.m_fRadius*cosf(lat)*cosf(lon),
                    s.m_vecCenter.y+s.m_fRadius*cosf(lat)*sinf(lon),s.m_vecCenter.z+s.m_fRadius*sinf(lat)));};
            for(int i=0;i<rings;++i) for(int j=0;j<segments;++j) {
                Triangle(out,point(i,j),point(i,j+1),point(i+1,j),center);
                Triangle(out,point(i,j+1),point(i+1,j+1),point(i+1,j),center);
            }
        }
    }
}
static void List(CPtrList& list,std::unordered_set<CEntity*>& seen,std::vector<SM64Surface>& out,const CVector& center,unsigned& entities,unsigned& rejected) {
    unsigned guard=0;
    // VC's head is the newest node; ProcessVerticalLineSector follows offset +8 (m_pPrev).
    for(auto n=list.GetNode();n;n=n->m_pPrev) {
        if(++guard>20000 || !Readable(n,sizeof(*n))) {++rejected;return;}
        auto e=static_cast<CEntity*>(n->m_pVoid);
        if(e && seen.insert(e).second) {
            if(!Readable(e,sizeof(CEntity))){++rejected;continue;}
            Entity(out,e,center,entities,rejected);
        }
    }
}
bool RefreshCollision(const CVector& center,bool force) {
    DWORD now=GetTickCount();
    float dx=center.x-lastCenter.x,dy=center.y-lastCenter.y;
    if(!force && now-lastFrame<250 && dx*dx+dy*dy<25.f) return !previous.empty();
    std::vector<SM64Surface> next; next.reserve(20000);
    unsigned entities=0,rejected=0;
    // Allocation pools also contain entities removed from the world (closed doors,
    // hidden interiors). Use the very same active sector lists as GTA's collision queries.
    std::unordered_set<CEntity*> seen;
    int x0=std::clamp(CWorld::GetSectorIndexX(center.x-Radius),0,NUMSECTORS_X-1);
    int x1=std::clamp(CWorld::GetSectorIndexX(center.x+Radius),0,NUMSECTORS_X-1);
    int y0=std::clamp(CWorld::GetSectorIndexY(center.y-Radius),0,NUMSECTORS_Y-1);
    int y1=std::clamp(CWorld::GetSectorIndexY(center.y+Radius),0,NUMSECTORS_Y-1);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){auto s=CWorld::GetSector(x,y);
        List(s->m_buildingList,seen,next,center,entities,rejected);
        List(s->m_buildingOverlapList,seen,next,center,entities,rejected);
        List(s->m_objectList,seen,next,center,entities,rejected);
        List(s->m_objectOverlapList,seen,next,center,entities,rejected);
        List(s->m_dummyList,seen,next,center,entities,rejected);
        List(s->m_dummyOverlapList,seen,next,center,entities,rejected);
    }
    for(auto& list:CWorld::ms_bigBuildingsList)List(list,seen,next,center,entities,rejected);
    lastFrame=now;lastCenter=center;
    if(next.size()>MaxSurfaces || rejected) {
        Log("collision REFUSED surfaces=%u rejected=%u",unsigned(next.size()),rejected);
        return false; // Never silently truncate a wall or continue simulation on incomplete geometry.
    }
    if(next.size()!=previous.size() || (!next.empty() && memcmp(next.data(),previous.data(),next.size()*sizeof(SM64Surface)))) {
        sm.static_surfaces_load(next.data(),static_cast<uint32_t>(next.size()));
        previous.swap(next);
        Log("collision resident entities=%u surfaces=%u",entities,unsigned(previous.size()));
        if(force){unsigned up=0,down=0;for(auto& s:previous){auto& a=s.vertices[0];auto& b=s.vertices[1];auto& c=s.vertices[2];
            int64_t ny=(int64_t(b[2])-a[2])*(int64_t(c[0])-b[0])-(int64_t(b[0])-a[0])*(int64_t(c[2])-b[2]);if(ny>0)++up;if(ny<0)++down;}
            Log("collision winding up=%u down=%u",up,down);}
    }
    return !previous.empty();
}
void ResetCollision(){previous.clear();lastFrame=0;lastCenter=CVector(1e9f,1e9f,1e9f);}
bool RefreshVehicleCollision(const CVector& center){
    // Refresh moving native vehicle primitives at each physics tick. Keep the world
    // mesh cached; do not rescan every building or reduce collision to a ground ray.
    std::vector<SM64Surface> dynamic;unsigned count=0,rejected=0;
    std::unordered_set<CEntity*> seen;
    int x0=std::clamp(CWorld::GetSectorIndexX(center.x-10.f),0,NUMSECTORS_X-1);
    int x1=std::clamp(CWorld::GetSectorIndexX(center.x+10.f),0,NUMSECTORS_X-1);
    int y0=std::clamp(CWorld::GetSectorIndexY(center.y-10.f),0,NUMSECTORS_Y-1);
    int y1=std::clamp(CWorld::GetSectorIndexY(center.y+10.f),0,NUMSECTORS_Y-1);
    auto vehicles=[&](CPtrList& list){unsigned guard=0;
        for(auto n=list.GetNode();n;n=n->m_pPrev){
            if(++guard>20000 || !Readable(n,sizeof(*n))){++rejected;return;}
            auto e=static_cast<CEntity*>(n->m_pVoid);
            if(!e || !seen.insert(e).second || !Readable(e,sizeof(CEntity)))continue;
            float dx=e->pos.x-center.x,dy=e->pos.y-center.y;
            if(dx*dx+dy*dy>100.f)continue;
            Entity(dynamic,e,center,count,rejected);
        }};
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){auto s=CWorld::GetSector(x,y);vehicles(s->m_vehicleList);vehicles(s->m_vehicleOverlapList);}
    if(rejected || previous.size()+dynamic.size()>MaxSurfaces){Log("moving collision refused count=%u",unsigned(dynamic.size()));return false;}
    static std::vector<SM64Surface> combined;
    combined=previous;combined.insert(combined.end(),dynamic.begin(),dynamic.end());
    sm.static_surfaces_load(combined.data(),unsigned(combined.size()));
    static DWORD last=0;if(GetTickCount()-last>2000){last=GetTickCount();Log("moving native vehicles=%u surfaces=%u",count,unsigned(dynamic.size()));}
    return true;
}
}
