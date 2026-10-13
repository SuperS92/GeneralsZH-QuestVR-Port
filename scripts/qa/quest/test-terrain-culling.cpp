// SPDX-License-Identifier: GPL-3.0-or-later
#include "VRDevice/QuestTerrainCulling.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>
using namespace QuestXR;
static void require(bool value,const char *s) { if (!value) { std::fprintf(stderr,"FAIL %s\n",s); std::exit(1); } }
static TerrainPoint add(TerrainPoint a,TerrainPoint b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
static TerrainPoint sub(TerrainPoint a,TerrainPoint b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
static TerrainPoint scale(TerrainPoint a,double s) { return {a.x*s,a.y*s,a.z*s}; }
static double dot(TerrainPoint a,TerrainPoint b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static TerrainPoint cross(TerrainPoint a,TerrainPoint b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
struct CameraFixture { std::array<TerrainPoint,8> corners; std::array<TerrainPlane,6> planes; TerrainFrustum frustum; };
static CameraFixture camera(double yaw,double pitch,double eye,double farDistance,double offset=0) {
    CameraFixture c;
    TerrainPoint right{std::cos(yaw),std::sin(yaw),0};
    TerrainPoint forward{-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
    TerrainPoint up=cross(right,forward), origin{300+eye,220,250};
    for (unsigned i=0;i<8;++i) {
        double z=i<4?1.0:farDistance;
        double x=((i%4==0 || i%4==2)?-0.83:1.13)+offset;
        double y=(i%4<2?0.71:-0.94);
        c.corners[i]=add(origin,scale(add(forward,add(scale(right,x),scale(up,y))),z));
        // Emulate W3D float inputs instead of silently testing perfect doubles.
        c.corners[i]={float(c.corners[i].x),float(c.corners[i].y),float(c.corners[i].z)};
    }
    constexpr unsigned faces[6][3]={{0,3,1},{0,5,4},{0,6,2},{2,7,3},{1,7,5},{4,7,6}};
    TerrainPoint center{}; for (auto p:c.corners) center=add(center,scale(p,1.0/8));
    for (unsigned i=0;i<6;++i) {
        auto a=c.corners[faces[i][0]],b=c.corners[faces[i][1]],d=c.corners[faces[i][2]];
        TerrainPoint n=cross(sub(b,a),sub(d,a)); n=scale(n,1/std::sqrt(dot(n,n)));
        double distance=dot(n,a);
        if (dot(n,center)>distance) { n=scale(n,-1); distance=-distance; }
        c.planes[i]={{float(n.x),float(n.y),float(n.z)},float(distance)};
    }
    c.frustum=PrepareTerrainFrustum(c.planes,c.corners,true);
    return c;
}
// Independent oracle clips an actual triangle polygon against every plane.
// Any surviving polygon means that skipping its terrain group is forbidden.
static bool triangleVisible(std::vector<TerrainPoint> polygon,const CameraFixture &c) {
    for (auto plane:c.planes) {
        std::vector<TerrainPoint> next;
        if (polygon.empty()) return false;
        TerrainPoint a=polygon.back(); double da=dot(plane.normal,a)-plane.distance;
        for (auto b:polygon) {
            double db=dot(plane.normal,b)-plane.distance;
            if ((da<=0)!=(db<=0)) next.push_back(add(a,scale(sub(b,a),da/(da-db))));
            if (db<=0) next.push_back(b);
            a=b; da=db;
        }
        polygon=std::move(next);
    }
    return !polygon.empty();
}
int main() {
    TerrainPatchBounds patch;
    std::array<TerrainPoint,4> quad{{{0,0,0},{1,0,0},{1,1,0},{0,1,0}}};
    for (unsigned i=0;i<1023;++i) patch.Observe(i,quad);
    require(!patch.Complete(),"Uninitialized last cell fails open");
    patch.Observe(0,quad); require(patch.cells==1023,"Partial duplicate writes cannot fake coverage");
    patch.Observe(1023,quad); require(patch.Complete(),"Complete write coverage");
    quad[2].z=900; patch.Observe(512,quad);
    quad[2].z=0; patch.Observe(512,quad);
    require(patch.bounds.high.z==900,"Interior peak and historical bounds retained after partial rewrite");
    TerrainBounds group; require(TerrainGroupBounds(&patch,1,0,1,group),"Final partial group valid");
    require(!TerrainGroupBounds(&patch,1,0,2,group),"Never read beyond group resource");
    patch.Observe(1024,quad); require(!patch.Complete(),"Invalid cell permanently fails open");
    patch={}; quad[0].x=std::numeric_limits<double>::quiet_NaN(); patch.Observe(0,quad);
    require(patch.unsafe,"Nonfinite geometry fails open");
    auto c=camera(0,-0.8,0,120000);
    require(c.frustum.valid,"Reference asymmetric float camera valid");
    auto planes=c.planes; planes[0].normal={};
    require(!PrepareTerrainFrustum(planes,c.corners,true).valid,"Degenerate plane fails open");
    planes=c.planes; planes[0].distance=std::numeric_limits<double>::infinity();
    require(!PrepareTerrainFrustum(planes,c.corners,true).valid,"Nonfinite plane fails open");
    planes=c.planes; planes[0].normal=scale(planes[0].normal,-1); planes[0].distance=-planes[0].distance;
    require(!PrepareTerrainFrustum(planes,c.corners,true).valid,"Reversed plane fails open");
    require(!PrepareTerrainFrustum(c.planes,c.corners,false).valid,"Ineligible passes fail open");
    TerrainBounds touching; touching.Include(c.corners[0]); touching.Include(c.corners[0]);
    require(!TerrainOutside(touching,c.frustum),"Frustum edge retained despite float plane rounding");
    std::mt19937 random(70813); std::uniform_real_distribution<double> xy(-800,1300), z(-100,800);
    unsigned tested=0,culled=0,eyeDifferences=0,fallbackViews=0;
    for (unsigned view=0;view<32;++view) {
        auto left=camera(view*0.196,-0.25-view*0.02,-5,view%2?1800:120000,-0.06);
        auto right=camera(view*0.196,-0.25-view*0.02,5,view%2?1800:120000,0.06);
        fallbackViews+=!left.frustum.valid; fallbackViews+=!right.frustum.valid;
        for (unsigned g=0;g<200;++g) {
            std::vector<TerrainPatchBounds> patches(1+g%10);
            std::vector<std::array<TerrainPoint,4>> quads;
            for (auto &p:patches) {
                double x=xy(random),y=xy(random),height=z(random);
                for (unsigned cell=0;cell<1024;++cell) {
                    double a=x+cell%32,b=y+cell/32;
                    std::array<TerrainPoint,4> q{{{a,b,height},{a+1,b,height+0.2},
                        {a+1,b+1,height+(cell==515?100:0.4)},{a,b+1,height+0.1}}};
                    p.Observe(cell,q); quads.push_back(q);
                }
            }
            require(TerrainGroupBounds(patches.data(),patches.size(),0,patches.size(),group),"Complete group");
            bool outside[2]={TerrainOutside(group,left.frustum),TerrainOutside(group,right.frustum)};
            eyeDifferences+=outside[0]!=outside[1];
            for (unsigned eye=0;eye<2;++eye) {
                ++tested; if (!outside[eye]) continue; ++culled;
                for (auto q:quads) {
                    require(!triangleVisible({q[0],q[2],q[3]},eye?right:left),"Culled triangle cannot intersect eye frustum");
                    require(!triangleVisible({q[0],q[1],q[2]},eye?right:left),"Second triangle invisible in the same eye");
                }
            }
        }
    }
    require(culled>100 && eyeDifferences>0,"Exercise actual culling and different-eye results");
    require(!QuestXR_SuppressTerrainDraw(),"No draw suppression outside terrain scope");
    { TerrainCullDrawScope a(true); require(QuestXR_SuppressTerrainDraw(),"Scoped draw suppression");
        { TerrainCullDrawScope b(false); require(!QuestXR_SuppressTerrainDraw(),"Nested normal draw restores inherited path"); }
        require(QuestXR_SuppressTerrainDraw(),"Nested restoration"); }
    require(!QuestXR_SuppressTerrainDraw(),"Final restoration");
    std::printf("PASS %u grouped eye decisions, %u culled; %u different-eye results, %u rounding fallback views; independent triangle clipping, coverage, peaks, invalid/edge cases and draw scope\n",tested,culled,eyeDifferences,fallbackViews);
}
