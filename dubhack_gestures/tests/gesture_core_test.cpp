#include "GestureCore.h"
#include <iostream>
#include <cstdlib>
using namespace GestureBridgeCore;
int checks=0;
void Check(bool b,const char* name){++checks;if(!b){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
std::vector<Binding> Config(){return {{"MoveForward","W","hold",0,true},{"MoveBackward","S","hold",0,true},{"Shield","LeftShift","hold",0,true},{"Fire","F","press",500,true},{"Wave","Q","press",700,true},{"Laser","LeftMouseButton","hold",0,true},{"TurnLeft","Left","hold",0,true},{"TurnRight","Right","hold",0,true}};}
State New(){State s;s.Configure(Config());s.Tick(0);return s;}
Packet Make(Id f=0,Id t=1000,std::string session="a"){Packet p;p.session=session;p.frame=f;p.timestamp=t;return p;}
bool Has(const std::vector<Event>& ev,Phase p,std::string a){for(const auto& e:ev)if(e.phase==p && e.action.binding.action==a)return true;return false;}
int main(){
 auto s=New();auto p=Make();p.held={"MoveForward","TurnLeft","Shield"};p.move=1;p.turn=-.6;
 Check(s.Accept(p,0),"accept concurrent");auto ev=s.Tick(0);
 Check(s.Move()==1 && s.Turn()==-.6,"concurrent axes");
 for(auto a:p.held){Check(Has(ev,Phase::Pressed,a),"concurrent press");Check(Has(ev,Phase::Held,a),"concurrent hold");}
 ev=s.Tick(.02);Check(!Has(ev,Phase::Pressed,"Shield") && Has(ev,Phase::Held,"Shield"),"no repeated press");
 Check(s.IsActionHeld("shield"),"case insensitive query");
 Check(!s.Accept(p,.03),"duplicate frame");p.frame=-1;Check(!s.Accept(p,.03),"negative frame");
 p=Make(1,999);Check(!s.Accept(p,.03),"backward timestamp");
 ev=s.Tick(.401);Check(!s.IsConnected() && s.Move()==0 && s.Turn()==0,"timeout zeros axes");
 for(auto a:{"MoveForward","TurnLeft","Shield"})Check(Has(ev,Phase::Released,a),"timeout release all");
 s=New();p=Make();p.held={"Shield"};s.Accept(p,0);s.Tick(0);
 p=Make(1);p.events={{1,1000,"Fire"}};s.Accept(p,.1);ev=s.Tick(.1);
 Check(ev.front().phase==Phase::Released && ev.front().action.binding.action=="Shield","release shield before fire");
 Check(Has(ev,Phase::Pressed,"Fire") && Has(ev,Phase::Released,"Fire"),"pulse edges same tick");
 Check(!s.IsActionHeld("Fire"),"pulse is not hold");
 p.frame=2;s.Accept(p,.2);Check(s.Tick(.2).empty(),"repeat pulse deduplicated");
 p.frame=3;p.events={{2,1000,"Fire"}};s.Accept(p,.3);Check(s.Tick(.3).empty(),"cooldown");
 p.frame=4;p.events={{3,1000,"Fire"},{4,1000,"Wave"}};s.Accept(p,.7);ev=s.Tick(.7);
 Check(Has(ev,Phase::Pressed,"Fire") && Has(ev,Phase::Pressed,"Wave"),"two different retained events");
 p=Make(5,2000);p.events={{5,1000,"Fire"}};s.Accept(p,.8);Check(s.Tick(.8).empty(),"expired event");
 p=Make(6,2100);p.events={{6,2200,"Wave"}};s.Accept(p,.9);Check(s.Tick(.9).empty(),"future event");
 s=New();p=Make();p.held={"Laser"};s.Accept(p,0);s.Tick(0);
 p=Make(0,0,"b");p.events={{1,0,"Fire"}};s.Accept(p,.1);
 Check(Has(s.Tick(.1),Phase::Released,"Laser"),"restart release");
 Check(Has(s.Tick(.12),Phase::Pressed,"Fire"),"restart preserves first event");
 Check(!s.Accept(Make(99,9999,"a"),.13),"old session retired");
 s=New();p=Make();p.held={"MoveForward","TurnRight"};p.move=1;p.turn=.5;s.Accept(p,0);s.Tick(0);
 auto cfg=Config();cfg[0].key="Up";s.Configure(cfg);ev=s.Tick(.01);
 Check(Has(ev,Phase::Released,"MoveForward") && s.Move()==0,"rebind release");
 ev=s.Tick(.02);Check(Has(ev,Phase::Pressed,"MoveForward") && s.Move()==1,"rebind resume");
 cfg[0].enabled=false;s.Configure(cfg);s.Tick(.03);s.Tick(.04);
 Check(s.Move()==0 && s.Turn()==.5,"disable movement only");
 s.Disconnect();ev=s.Tick(.05);Check(Has(ev,Phase::Released,"TurnRight") && s.Turn()==0,"disconnect");
 Check(!s.Accept(p,.06),"disconnect retains old frame guard");
 s=New();p=Make();p.version=2;Check(!s.Accept(p,0),"reject v2");
 p=Make();p.held={"Grab"};Check(!s.Accept(p,0),"reject outdated ability");
 p.held={"Fire"};Check(!s.Accept(p,0),"reject pulse in held");
 p.held={"MoveForward","MoveBackward"};Check(!s.Accept(p,0),"reject conflicting movement");
 p.held={"Shield","Laser"};Check(!s.Accept(p,0),"reject conflicting right hand");
 p=Make();p.turn=2;Check(!s.Accept(p,0),"reject invalid axis");
 p=Make();p.events={{1,1000,"Ice"}};Check(!s.Accept(p,0),"only supported pulse actions");
 for(const auto& b:Config()){
  s=New();p=Make();if(b.mode=="hold")p.held={b.action};else p.events={{1,1000,b.action}};
  Check(s.Accept(p,0),"accept each action");ev=s.Tick(0);Check(Has(ev,Phase::Pressed,b.action),"every action press");
  if(b.mode=="hold"){p=Make(1);s.Accept(p,.1);Check(Has(s.Tick(.1),Phase::Released,b.action),"every hold releases");}
 }
 std::cout<<"PASS: "<<checks<<" C++ assertions\n";
}
