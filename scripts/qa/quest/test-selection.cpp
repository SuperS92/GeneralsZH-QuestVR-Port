// SPDX-License-Identifier: GPL-3.0-or-later
// Production selection functions are included by the runner; dependencies below are test doubles.
#include "VRDevice/QuestXRInterface.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <list>
#include <set>
#include <vector>
using Bool=bool;using Int=int;using Real=float;using ObjectID=unsigned;
constexpr Bool TRUE=true,FALSE=false;constexpr ObjectID INVALID_ID=0;constexpr int KINDOF_STRUCTURE=1;
#define MIN(a,b) std::min(a,b)
#define MAX(a,b) std::max(a,b)
struct Coord3D{float x=0,y=0,z=0;};struct Region3D{Coord3D lo,hi;};
struct Vector3{float X,Y,Z;Vector3(float x=0,float y=0,float z=0):X(x),Y(y),Z(z){}Vector3 operator+(Vector3 b)const{return {X+b.X,Y+b.Y,Z+b.Z};}Vector3 operator*(float f)const{return {X*f,Y*f,Z*f};}static float Dot_Product(Vector3 a,Vector3 b){return a.X*b.X+a.Y*b.Y+a.Z*b.Z;}};
struct Object{unsigned id=0;bool owned=true,structure=false,contained=false;bool isLocallyControlled()const{return owned;}bool isKindOf(int)const{return structure;}Object*getContainedBy(){return contained?this:nullptr;}unsigned getID()const{return id;}};
struct Drawable{Object*object=nullptr;bool selectable=true,selected=false;Coord3D pos{2,2,0};Object*getObject(){return object;}bool isSelectable(){return selectable;}bool isSelected(){return selected;}bool isMassSelectable(){return false;}const Coord3D*getPosition(){return &pos;}};
using DrawableList=std::list<Drawable*>;
struct Ui{DrawableList selected;int limit=-1;const DrawableList*getAllSelectedDrawables(){return &selected;}int getSelectCount(){return int(selected.size());}int getMaxSelectCount(){return limit;}void deselectAllDrawables(){for(auto*d:selected)d->selected=false;selected.clear();}void selectDrawable(Drawable*d){if(!d->selected){d->selected=true;selected.push_back(d);}}void selectMatchingAcrossScreen(){assert(false);}};
struct GameMessage{enum Type{MSG_DESTROY_SELECTED_GROUP,MSG_CREATE_SELECTED_GROUP,MSG_INVALID,MSG_DO_MOVETO,MSG_DO_ATTACK_OBJECT};Type type;bool replace=false;std::vector<unsigned>ids;void appendBooleanArgument(bool v){replace=v;}void appendObjectIDArgument(unsigned v){ids.push_back(v);}};
struct Stream{std::list<GameMessage>messages;GameMessage*appendMessage(GameMessage::Type t){messages.push_back({t});return &messages.back();}};
struct CommandTranslator{enum{EVALUATE_ONLY};};
struct Client{std::vector<Drawable*>world;int evaluations=0;void iterateDrawablesInRegion(Region3D*r,void(*cb)(Drawable*,void*),void*p){for(auto*d:world)if(d&&d->pos.x>=r->lo.x&&d->pos.x<=r->hi.x&&d->pos.y>=r->lo.y&&d->pos.y<=r->hi.y)cb(d,p);}GameMessage::Type evaluateContextCommand(Drawable*,const Coord3D*,int){++evaluations;return GameMessage::MSG_DO_ATTACK_OBJECT;}};
Ui ui;Stream stream;Client client;Ui*TheInGameUI=&ui;Stream*TheMessageStream=&stream;Client*TheGameClient=&client;
struct Global{int m_doubleClickTimeMS=300;}global;Global*TheGlobalData=&global;
ObjectID g_lastSingleSelection=INVALID_ID;std::chrono::steady_clock::time_point g_lastSingleSelectionTime{};
Vector3 g_boxRight{1,0,0},g_boxForward{0,1,0};struct{float worldUnitsPerMeter=100;}g_worldTransform;
Drawable*picked=nullptr;Coord3D traced{};bool ground=true;int orders=0;
Drawable*pickDrawable(const Vector3&,const Vector3&){return picked;}bool traceTerrain(const Vector3&,const Vector3&,Coord3D&c){c=traced;return ground;}
void commandUnderRay(const Vector3&,const Vector3&,bool){++orders;}void getHeadHorizontalBasis(Vector3&r,Vector3&f){r={1,0,0};f={0,1,0};}
constexpr int ANDROID_LOG_INFO=4;int __android_log_print(int,const char*,const char*,...){return 0;}
#include "selection-production.inc"
std::array<Object,300>objects;std::array<Drawable,300>draws;unsigned cases=0;
void reset(){ui.deselectAllDrawables();ui.limit=-1;stream.messages.clear();client.world.clear();client.evaluations=0;orders=0;picked=nullptr;g_lastSingleSelection=INVALID_ID;g_boxSelection={};ground=true;traced={};for(unsigned i=0;i<draws.size();++i){objects[i]={i+1,true,false,false};draws[i]={&objects[i],true,false,{2,2,0}};}}
void seed(unsigned n){for(unsigned i=0;i<n;++i)ui.selectDrawable(&draws[i]);}
void equalGroups(std::set<unsigned>expected,std::set<unsigned>network){for(const auto&m:stream.messages){if(m.type==GameMessage::MSG_DESTROY_SELECTED_GROUP)network.clear();else if(!m.ids.empty()){if(m.replace)network.clear();network.insert(m.ids.begin(),m.ids.end());}}std::set<unsigned>visual;for(auto*d:ui.selected)visual.insert(d->object->id);assert(visual==expected&&network==expected);++cases;}
int main(){
 Vector3 ray;const Coord3D from{0,0,0},to{10,10,0};
 reset();seed(5);picked=&draws[5];smartTriggerUnderRay(ray,ray,FALSE,TRUE);equalGroups({1,2,3,4,5,6},{1,2,3,4,5});assert(orders==0&&client.evaluations==0&&!stream.messages.back().replace);
 // Adding the same unit twice is idempotent and does not toggle it off.
 const auto messages=stream.messages.size();smartTriggerUnderRay(ray,ray,FALSE,TRUE);assert(stream.messages.size()==messages);equalGroups({1,2,3,4,5,6},{1,2,3,4,5});
 for(int invalid=0;invalid<6;++invalid){reset();seed(1);picked=invalid?&draws[1]:nullptr;if(invalid==1)objects[1].owned=false;if(invalid==2)objects[1].structure=true;if(invalid==3)objects[1].contained=true;if(invalid==4)draws[1].selectable=false;if(invalid==5)draws[1].object=nullptr;smartTriggerUnderRay(ray,ray,FALSE,TRUE);equalGroups({1},{1});assert(stream.messages.empty()&&orders==0&&client.evaluations==0);}
 for(bool building:{false,true}){reset();seed(1);objects[0].structure=building;objects[0].owned=building;picked=&draws[1];selectUnderRay(ray,ray,TRUE);client.world={&draws[1]};selectBox(from,to,TRUE);equalGroups({1},{1});assert(stream.messages.empty());}
 reset();seed(1);picked=&draws[1];selectUnderRay(ray,ray,FALSE);equalGroups({2},{1});assert(stream.messages.back().replace);
 reset();seed(1);selectUnderRay(ray,ray,FALSE);equalGroups({}, {1});
 reset();seed(2);client.world={&draws[0],&draws[2],&draws[2],&draws[3]};selectBox(from,to,TRUE);equalGroups({1,2,3,4},{1,2});assert(stream.messages.size()==1&&!stream.messages.back().replace&&stream.messages.back().ids.size()==2);
 reset();seed(1);client.world={&draws[0]};selectBox(from,to,TRUE);equalGroups({1},{1});assert(stream.messages.empty());
 reset();seed(1);selectBox(from,to,FALSE);equalGroups({}, {1});assert(stream.messages.size()==1&&stream.messages.back().type==GameMessage::MSG_DESTROY_SELECTED_GROUP);
 reset();seed(1);client.world={&draws[1]};selectBox(to,from,TRUE);equalGroups({1,2},{1});
 // Existing and new units share the same configured/capped budget.
 for(int configured:{-1,1,2,5,255,256,300})for(unsigned count:{0u,1u,4u,254u,255u,256u}){
  reset();seed(count);ui.limit=configured;for(unsigned i=0;i<300;++i)client.world.push_back(&draws[i]);selectBox(from,to,TRUE);
  const unsigned limit=configured>0?unsigned(std::min(configured,256)):256;const unsigned size=std::max(count,limit);std::set<unsigned>expected,network;for(unsigned i=0;i<size;++i)expected.insert(i+1);for(unsigned i=0;i<count;++i)network.insert(i+1);equalGroups(expected,network);
 }
 reset();seed(1);ui.limit=1;picked=&draws[1];selectUnderRay(ray,ray,TRUE);equalGroups({1},{1});assert(stream.messages.empty());
 // Modifier ownership: only the connected left grip, never both grips or left X/Y.
 for(unsigned buttons=0;buttons<64;++buttons)for(bool haveLeft:{false,true})for(bool connected:{false,true})for(bool rightGrip:{false,true}){
  QuestXRControllerState l{},r{};l.buttons=buttons;l.connected=connected;r.buttons=rightGrip?QUEST_XR_BUTTON_GRIP:0;
  assert(bool(additiveSelectionHeld(l,r,haveLeft))==(haveLeft&&connected&&(buttons&QUEST_XR_BUTTON_GRIP)&&!rightGrip));++cases;
 }
 // Release the grip during a drag: the trigger-down choice still owns that drag.
 reset();seed(1);client.world={&draws[1]};QuestXRControllerState c{};c.connected=1;c.buttons=c.pressed=QUEST_XR_BUTTON_TRIGGER;updateBoxSelection(c,ray,ray,TRUE);assert(g_boxSelection.additive);
 g_boxSelection.pressedAt-=std::chrono::milliseconds(300);traced={10,10,0};c.pressed=0;updateBoxSelection(c,ray,ray,FALSE);assert(g_boxSelection.boxing&&g_boxSelection.additive);c.buttons=0;c.released=QUEST_XR_BUTTON_TRIGGER;assert(updateBoxSelection(c,ray,ray,FALSE));equalGroups({1,2},{1});assert(!g_boxSelection.armed);
 std::printf("PASS %u production selection fixture cases; not full engine or live crossplay\n",cases);
}
