#include "qarvtk/renderer.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
int main(int argc,char**argv){try{
  std::string scene;double scale=1;bool ras=false,hidden=false;int frames=0;
  for(int i=1;i<argc;++i){const std::string a=argv[i];
    auto next=[&](){if(++i>=argc)throw std::invalid_argument("Missing value for "+a);return std::string(argv[i]);};
    if(a=="--help"){std::cout<<"qarvtk-preview [--scene file.vtp|scene.vtm] [--meters-per-unit 1] [--ras] [--frames N] [--hidden]\n";return 0;}
    else if(a=="--scene")scene=next();else if(a=="--meters-per-unit")scale=std::stod(next());
    else if(a=="--frames")frames=std::stoi(next());else if(a=="--ras")ras=true;else if(a=="--hidden")hidden=true;
    else throw std::invalid_argument("Unknown argument: "+a);
  }
  if(frames<0||!std::isfinite(scale)||scale<=0)throw std::invalid_argument("Invalid frames or scale");
  if(hidden&&frames==0)throw std::invalid_argument("Hidden preview requires --frames N");
  qarvtk::Renderer renderer(!hidden);std::cout<<renderer.capabilities()<<'\n';
  if(!scene.empty())renderer.load_scene(scene,scale,ras);
  for(int n=0;!renderer.closing()&&(frames==0||n<frames);++n){
    qarvtk::Pose p; p.position.z=2; p.position.x=.06*std::sin(n*.025);
    renderer.render(p,{}, {},640,640);if(!hidden)renderer.present();renderer.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::cout<<"Preview completed. No QAROS runtime was used.\n";return 0;
}catch(const std::exception&e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
