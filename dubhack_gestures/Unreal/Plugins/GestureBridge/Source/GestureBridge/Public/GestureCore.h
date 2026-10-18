#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace GestureBridgeCore {
using Id=std::int64_t;
struct Binding {std::string action,key,mode;int cooldownMs=0;bool enabled=true;};
struct Pulse {Id id=0,timestamp=0;std::string action;};
struct Packet {
 int version=3;std::string session;Id frame=-1,timestamp=0;
 std::vector<std::string> held;std::vector<Pulse> events;
 double move=0,turn=0;
};
struct Action {Binding binding;Id eventId=0;};
enum class Phase {Pressed,Released,Held};
struct Event {Phase phase;Action action;};
class State {
public:
 static bool IsPulse(const std::string& a){return a=="Fire" || a=="Wave" || a=="Scan" || a=="Hint" || a=="Repeat" || a=="Retry";}
 static bool IsHold(const std::string& a){return a=="MoveForward" || a=="MoveBackward" || a=="Shield" || a=="Laser" || a=="TurnLeft" || a=="TurnRight";}
 static bool Known(const std::string& a){return IsPulse(a)||IsHold(a);}
 static std::string ActionId(std::string a){std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return a;}
 void Configure(const std::vector<Binding>& b,int timeout=400,int age=500){bindings=b;timeoutMs=timeout;maxAge=age;releaseOnce=true;fired.clear();}
 bool Accept(const Packet& p,double now){
  if(p.version!=3 || p.session.empty() || p.session.size()>80 || p.frame<0 || p.timestamp<0
   || !std::isfinite(now) || !std::isfinite(p.move) || !std::isfinite(p.turn)
   || std::abs(p.move)>1 || std::abs(p.turn)>1 || p.held.size()>6 || p.events.size()>16)return false;
  std::set<std::string> unique;
  for(const auto& a:p.held)if(!IsHold(a)||!unique.insert(a).second)return false;
  if((unique.count("MoveForward") && unique.count("MoveBackward"))
   || (unique.count("TurnLeft") && unique.count("TurnRight"))
   || (unique.count("Shield") && unique.count("Laser")))return false;
  for(const auto& e:p.events)if(e.id<=0 || e.timestamp<0 || !IsPulse(e.action))return false;
  if(p.session!=session){
   if(retired.count(p.session))return false;
   if(!session.empty())retired.insert(session);
   session=p.session;frame=-1;timestamp=-1;consumed=0;fired.clear();
   if(!active.empty())releaseOnce=true;
  }
  if(p.frame<=frame || p.timestamp<timestamp)return false;
  frame=p.frame;timestamp=p.timestamp;latest=p;received=now;hasPacket=true;return true;
 }
 void Disconnect(){hasPacket=false;connected=false;move=turn=0;}
 std::vector<Event> Tick(double now){
  std::vector<Event> result,pulseEvents;connected=hasPacket && now>=received && (now-received)*1000<=timeoutMs;
  bool suppress=releaseOnce;releaseOnce=false;
  move=turn=0;
  std::map<std::string,Action> desired;
  if(connected && !suppress){
   // Disabled movement/look bindings also disable the corresponding axis direction.
   for(const auto& b:bindings)if(b.enabled && b.mode=="hold" && std::find(latest.held.begin(),latest.held.end(),b.action)!=latest.held.end()){
    desired.emplace(ActionId(b.action),Action{b,0});
    if(b.action=="MoveForward" && latest.move>0)move=latest.move;
    if(b.action=="MoveBackward" && latest.move<0)move=latest.move;
    if(b.action=="TurnLeft" && latest.turn<0)turn=latest.turn;
    if(b.action=="TurnRight" && latest.turn>0)turn=latest.turn;
   }
   auto pulses=latest.events;std::sort(pulses.begin(),pulses.end(),[](const Pulse& a,const Pulse& b){return a.id<b.id;});
   for(const auto& e:pulses){
    if(e.id<=consumed)continue;
    consumed=e.id;Id age=latest.timestamp-e.timestamp;
    if(age<0 || age>maxAge)continue;
    for(const auto& b:bindings)if(b.enabled && b.mode=="press" && b.action==e.action){
     auto it=fired.find(b.action);
     if(it==fired.end() || (now-it->second)*1000>=b.cooldownMs){
      // Pulse edges are emitted for EACH event even if several arrive in one frame.
      pulseEvents.push_back({Phase::Pressed,Action{b,e.id}});
      pulseEvents.push_back({Phase::Released,Action{b,e.id}});
      fired[b.action]=now;
     }
    }
   }
  }
  for(const auto& x:active){auto it=desired.find(x.first);if(it==desired.end() || it->second.binding.key!=x.second.binding.key)result.push_back({Phase::Released,x.second});}
  result.insert(result.end(),pulseEvents.begin(),pulseEvents.end());
  for(const auto& x:desired){auto it=active.find(x.first);if(it==active.end() || it->second.binding.key!=x.second.binding.key)result.push_back({Phase::Pressed,x.second});result.push_back({Phase::Held,x.second});}
  active=desired;return result;
 }
 bool IsConnected() const{return connected;}
 bool IsActionHeld(const std::string& a)const{return active.count(ActionId(a))!=0;}
 double Move()const{return move;}double Turn()const{return turn;}
private:
 std::vector<Binding> bindings;std::map<std::string,Action> active;std::map<std::string,double> fired;
 std::set<std::string> retired;std::string session;Packet latest;
 Id frame=-1,timestamp=-1,consumed=0;double received=0,move=0,turn=0;
 int timeoutMs=400,maxAge=500;bool hasPacket=false,connected=false,releaseOnce=false;
};
}
