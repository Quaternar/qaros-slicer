#include "qarvtk/gl.hpp"
#include "qarvtk/renderer.hpp"
#include <vtkActor.h>
#include <vtkCubeSource.h>
#include <vtkDoubleArray.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSphereSource.h>
#include <vtkXMLMultiBlockDataWriter.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace qarvtk;
void check(bool good,const char* message){if(!good)throw std::runtime_error(message);}
void field(vtkDataObject* data,const char* name,int components,std::initializer_list<double> values){
  vtkNew<vtkDoubleArray> a;a->SetName((std::string("qarvtk.")+name).c_str());a->SetNumberOfComponents(components);
  for(auto v:values)a->InsertNextValue(v);data->GetFieldData()->AddArray(a);
}
void common(vtkDataObject* d){field(d,"version",1,{2});field(d,"fit_size_m",1,{0});}
struct Pixels {
  std::vector<unsigned char> color;std::vector<float> depth;unsigned width{},height{};
  Pixels(RenderedEye eye):color(eye.width*eye.height*4),depth(eye.width*eye.height),width(eye.width),height(eye.height){
    glBindTexture(GL_TEXTURE_2D,eye.color);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,color.data());
    glBindTexture(GL_TEXTURE_2D,eye.depth);glGetTexImage(GL_TEXTURE_2D,0,GL_RED,GL_FLOAT,depth.data());
    glBindTexture(GL_TEXTURE_2D,0);
  }
  unsigned center()const{return (height/2)*width+width/2;}
};
struct Temporary {
  std::filesystem::path dir=std::filesystem::temp_directory_path()/("qarvtk-volume-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temporary(){std::filesystem::create_directory(dir);}
  ~Temporary(){std::error_code ec;std::filesystem::remove_all(dir,ec);}
};
int main(){try{
  Renderer renderer;Temporary temp;
  vtkNew<vtkImageData> image;image->SetDimensions(33,33,33);image->SetOrigin(-16,-16,-16);image->AllocateScalars(VTK_FLOAT,1);
  for(int z=0;z<33;++z)for(int y=0;y<33;++y)for(int x=0;x<33;++x)
    *static_cast<float*>(image->GetScalarPointer(x,y,z))=((x-16)*(x-16)+(y-16)*(y-16)+(z-16)*(z-16)<100)?1.F:0.F;
  common(image);field(image,"matrix",16,{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});
  field(image,"color",6,{0,0,0,1,.5,0,1,0,0,1,.5,0});
  field(image,"opacity",4,{0,0,.5,0,.5,0,.5,0,1,1,.5,0});
  field(image,"volume",9,{1,0,.2,.75,.25,30,1,1,3});
  vtkNew<vtkMultiBlockDataSet> scene;scene->SetBlock(0,image);
  const auto path=(temp.dir/"scene.vtm").string();
  auto load=[&](){vtkNew<vtkXMLMultiBlockDataWriter> writer;writer->SetFileName(path.c_str());writer->SetInputData(scene);writer->SetDataModeToBinary();
    check(writer->Write()==1,"Cannot write volume test scene");renderer.load_scene(path,.03,false);};
  load();Pose pose;pose.position.z=2;Clip clip{.1,10};Fov fov{-.5,.5,.5,-.5};
  Pixels blue(renderer.render(pose,fov,clip,192,192));auto center=blue.center();
  check(blue.color[center*4+2]>180&&blue.color[center*4]<30,"Volume transfer-function color missing");
  const double distance=.1*10/(10-blue.depth[center]*(10-.1));
  std::cout<<"Tissue distance: "<<distance<<" m\n";
  check(distance>1.60&&distance<1.76,"Volume depth must describe visible tissue, not the volume bounding box");
  check(blue.depth[0]>.999&&blue.color[3]==0,"Transparent volume background must have far depth and zero alpha");
  Pose outside=pose;outside.position.x=10;
  Pixels absent(renderer.render(outside,fov,clip,192,192));
  check(absent.color[absent.center()*4+3]==0&&absent.depth[absent.center()]>.999,"Out-of-view volume reused a stale eye image");
  std::array<double,2> centroid{};
  for(int eye=0;eye<2;++eye){
    Pose stereo=pose;stereo.position.x=eye?.04:-.04;Pixels p(renderer.render(stereo,fov,clip,192,192));
    double sum=0,count=0;for(unsigned i=0;i<p.depth.size();++i)if(p.color[i*4+3]>128){sum+=i%p.width;++count;}
    check(count>50,"Volume missing in stereo eye");centroid[eye]=sum/count;
  }
  check(centroid[0]>centroid[1]+3,"Volume stereo disparity is reversed or missing");
  field(image,"color",6,{0,1,0,0,.5,0,1,1,0,0,.5,0});load();
  Pixels red(renderer.render(pose,fov,clip,160,128));center=red.center();
  check(red.color[center*4]>180&&red.color[center*4+2]<30,"Updated transfer function or resize was not applied");
  field(image,"opacity",4,{0,0,.5,0,1,0,.5,0});load();
  Pixels empty(renderer.render(pose,fov,clip,160,128));
  check(empty.color[center*4+3]==0&&empty.depth[center]>.999,"Transparent tissue must not leave phantom depth");
  field(image,"opacity",4,{0,0,.5,0,.5,0,.5,0,1,1,.5,0});
  vtkNew<vtkCubeSource> cube;cube->SetCenter(0,0,20);cube->SetXLength(8);cube->SetYLength(8);cube->SetZLength(8);cube->Update();
  auto* poly=cube->GetOutput();common(poly);field(poly,"scalar_mode",1,{0});field(poly,"material",8,{0,1,0,1,.3,.7,0,30});
  scene->SetBlock(1,poly);load();Pixels occluded(renderer.render(pose,fov,clip,160,128));
  check(occluded.color[center*4+1]>120&&occluded.color[center*4]<30,"Opaque surface in front must occlude the volume");
  check(std::abs(occluded.depth[center]-depth_from_distance(1.28,clip))<.001,"Surface occlusion depth is wrong");
  // A failed reload must leave the last complete scene active.
  field(image,"version",1,{99});bool rejected=false;try{load();}catch(const std::exception&){rejected=true;}
  check(rejected,"Invalid scene version accepted");Pixels retained(renderer.render(pose,fov,clip,160,128));
  check(retained.color[center*4+1]>120,"Failed reload destroyed the previous scene");
  vtkNew<vtkSphereSource> sphere;sphere->SetRadius(.35);sphere->SetThetaResolution(48);sphere->SetPhiResolution(32);sphere->Update();
  renderer.set_polydata(sphere->GetOutput());auto lit=renderer.render(pose,fov,clip,192,192);
  std::vector<unsigned char> pixels(192*192*4);glBindTexture(GL_TEXTURE_2D,lit.color);glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  unsigned low=255,high=0;for(unsigned i=0;i<192*192;++i)if(pixels[i*4+3]>250){low=std::min(low,unsigned(pixels[i*4+1]));high=std::max(high,unsigned(pixels[i*4+1]));}
  check(high>150&&high-low>60,"Surface lighting does not reveal curved shape");
  check(glGetError()==GL_NO_ERROR,"Volume render left an OpenGL error");
  std::cout<<"PASS: GPU ray-cast color, tissue depth, transparent background, preset update, resize, surface occlusion and atomic reload\n";
  return 0;
}catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}}
