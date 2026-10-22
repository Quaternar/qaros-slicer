#include "qarvtk/math.hpp"
#include "qarvtk/layout.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace qarvtk;
void require(bool b,const char*m){if(!b)throw std::runtime_error(m);}
void near(double a,double b){require(std::abs(a-b)<1e-9,"numeric mismatch");}
template<class F>void fails(F f){try{f();}catch(const std::invalid_argument&){return;}throw std::runtime_error("invalid input accepted");}
int main(){try{
  Clip c{.1,100}; near(depth_from_distance(.1,c),0);near(depth_from_distance(100,c),1);
  require(depth_from_distance(1,c)<depth_from_distance(2,c),"depth ordering");
  Fov f{-.3,.7,.5,-.2}; auto p=projection(f,c);
  auto ndc=[&](double x,double y,double z,int row){return (p[row*4]*x+p[row*4+1]*y+p[row*4+2]*z+p[row*4+3])/(-z);};
  near(ndc(std::tan(f.left),0,-1,0),-1);near(ndc(std::tan(f.right),0,-1,0),1);
  near(ndc(0,std::tan(f.up),-1,1),1);near(ndc(0,std::tan(f.down),-1,1),-1);
  near(ndc(0,0,-c.near_plane,2),-1);near(ndc(0,0,-c.far_plane,2),1);
  auto v=rotate({0,std::sqrt(.5),0,std::sqrt(.5)},{0,0,-1});near(v.x,-1);near(v.z,0);
  auto r=ras_mm_to_meters({1000,2000,3000});near(r.x,1);near(r.y,3);near(r.z,-2);
  fails([]{projection({}, {0,10});});fails([]{projection({}, {1,1});});
  fails([]{projection({1,-1,.4,-.4},{});});fails([]{validate(Pose{{},{0,0,0,0}});});
  fails([]{validate(Pose{{std::numeric_limits<double>::quiet_NaN(),0,0},{}});});
  Frame frame;for(unsigned i=0;i<4;++i)frame.textures.push_back({i+1,320,240,1});
  frame.views={{Eye::right,Plane::depth,3,0,240,320,0,0},{Eye::left,Plane::color,0,0,0,320,240,0},
               {Eye::right,Plane::color,2,0,0,320,240,0},{Eye::left,Plane::depth,1,320,240,0,0,0}};
  auto indices=validate_stereo(frame);require(indices[0].color==1&&indices[1].depth==0,"reordered views");
  auto bad=frame;bad.views[0].texture=4;fails([&]{validate_stereo(bad);});
  bad=frame;bad.views[1].x1=321;fails([&]{validate_stereo(bad);});
  bad=frame;bad.views[0].eye=Eye::left;fails([&]{validate_stereo(bad);});
  bad=frame;bad.textures[0].layers=2;fails([&]{validate_stereo(bad);});
  bad=frame;bad.views[2].texture=0;fails([&]{validate_stereo(bad);});
  std::cout<<"PASS: asymmetric projection, depth, quaternions, RAS and stereo layout\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
