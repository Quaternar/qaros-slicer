#include "qarvtk/gl.hpp"
#include "qarvtk/renderer.hpp"
#include "qarvtk/gl_composite.hpp"
#include <vtkActor.h>
#include <vtkBoundingBox.h>
#include <vtkCellData.h>
#include <vtkColorTransferFunction.h>
#include <vtkCompositeDataIterator.h>
#include <vtkDataArray.h>
#include <vtkFieldData.h>
#include <vtkImageData.h>
#include <vtkLightKit.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkOpenGLGPUVolumeRayCastMapper.h>
#include <vtkPiecewiseFunction.h>
#include <vtkPointData.h>
#include <vtkPolyDataNormals.h>
#include <vtkProp3D.h>
#include <vtkVolume.h>
#include <vtkVolumeProperty.h>
#include <vtkXMLMultiBlockDataReader.h>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkCubeSource.h>
#include <vtkCullerCollection.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtkOpenGLFramebufferObject.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkTextureObject.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkVersion.h>
#include <vtkXMLPolyDataReader.h>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace qarvtk { namespace {
vtkDataArray* field(vtkDataObject* data,const char* name,int components,int tuples=0){
  auto* a=data->GetFieldData()->GetArray((std::string("qarvtk.")+name).c_str());
  if(!a||a->GetNumberOfComponents()!=components||a->GetNumberOfTuples()==0||
      (tuples&&a->GetNumberOfTuples()!=tuples))throw std::runtime_error(std::string("Missing/invalid scene property: ")+name);
  for(vtkIdType i=0;i<a->GetNumberOfTuples();++i)for(int j=0;j<components;++j)
    if(!std::isfinite(a->GetComponent(i,j)))throw std::runtime_error("Scene property is not finite");
  return a;
}
vtkSmartPointer<vtkMatrix4x4> basis(double scale,bool ras){
  if(!std::isfinite(scale)||scale<=0)throw std::invalid_argument("meters-per-unit must be positive and finite");
  auto m=vtkSmartPointer<vtkMatrix4x4>::New();m->Zero();m->SetElement(3,3,1);m->SetElement(0,0,scale);
  if(ras){m->SetElement(1,2,scale);m->SetElement(2,1,-scale);}
  else{m->SetElement(1,1,scale);m->SetElement(2,2,scale);}
  return m;
}
void material(vtkProperty* p,const double* v){
  p->SetColor(v[0],v[1],v[2]);p->SetOpacity(v[3]);p->SetAmbient(v[4]);p->SetDiffuse(v[5]);
  p->SetSpecular(v[6]);p->SetSpecularPower(v[7]);p->LightingOn();p->SetInterpolationToPhong();
}
}
struct Renderer::Impl {
  GLFWwindow* main{}; GLFWwindow* sdk{}; bool glfw_started{};
  vtkSmartPointer<vtkGenericOpenGLRenderWindow> window;
  vtkSmartPointer<vtkRenderer> renderer;
  vtkSmartPointer<vtkPolyDataMapper> mapper;
  vtkSmartPointer<vtkActor> actor;
  vtkSmartPointer<vtkLightKit> lights;
  vtkSmartPointer<vtkOpenGLGPUVolumeRayCastMapper> volume_mapper;
  std::unique_ptr<GlComposite> composite;
  unsigned presented_fbo{};
  ~Impl() {
    if(main) glfwMakeContextCurrent(main);
    composite.reset();
    if(window) { window->Finalize(); window=nullptr; }
    volume_mapper=nullptr;lights=nullptr;
    actor=nullptr; mapper=nullptr; renderer=nullptr;
    if(sdk) glfwDestroyWindow(sdk);
    if(main) glfwDestroyWindow(main);
    if(glfw_started) glfwTerminate();
  }
};
Renderer::Renderer(bool visible):impl_(std::make_unique<Impl>()) {
  glfwSetErrorCallback([](int code,const char* message){std::cerr<<"GLFW "<<code<<": "<<message<<'\n';});
  if(!glfwInit()) throw std::runtime_error("Cannot initialize GLFW. Linux requires an X11 display (use xvfb-run for tests).");
  impl_->glfw_started=true;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_VISIBLE,visible?GLFW_TRUE:GLFW_FALSE);
  impl_->main=glfwCreateWindow(640,640,"QAROS VTK preview",nullptr,nullptr);
  if(!impl_->main) throw std::runtime_error("Cannot create OpenGL 3.3 render context");
  glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
  impl_->sdk=glfwCreateWindow(32,32,"QAROS shared context",nullptr,impl_->main);
  if(!impl_->sdk) throw std::runtime_error("Cannot create shared SDK context");
  make_current();glfwSwapInterval(0);
  auto& w=impl_->window;w=vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  w->SetOwnContext(0);w->SetMultiSamples(0);w->SetSize(640,640);
  w->SetFrameBlitMode(vtkOpenGLRenderWindow::NoBlit);
  w->SetReadyForRendering(true);
  vtkNew<vtkCallbackCommand> observer;
  observer->SetClientData(impl_.get());
  observer->SetCallback([](vtkObject*,unsigned long event,void* data,void* call){
    auto* p=static_cast<Impl*>(data);
    if(event==vtkCommand::WindowMakeCurrentEvent) glfwMakeContextCurrent(p->main);
    else if(event==vtkCommand::WindowIsCurrentEvent) *static_cast<bool*>(call)=glfwGetCurrentContext()==p->main;
    else if(event==vtkCommand::WindowSupportsOpenGLEvent) *static_cast<int*>(call)=1;
    else if(event==vtkCommand::WindowIsDirectEvent) *static_cast<int*>(call)=1;
  });
  for(auto e:{vtkCommand::WindowMakeCurrentEvent,vtkCommand::WindowIsCurrentEvent,
              vtkCommand::WindowSupportsOpenGLEvent,vtkCommand::WindowIsDirectEvent}) w->AddObserver(e,observer);
  impl_->renderer=vtkSmartPointer<vtkRenderer>::New();
  // Always execute the ray caster, including when the volume is out of view:
  // its offscreen textures must be cleared for each eye, never reused stale.
  impl_->renderer->GetCullers()->RemoveAllItems();
  impl_->renderer->SetBackground(0,0,0);impl_->renderer->SetBackgroundAlpha(0);
  w->AddRenderer(impl_->renderer);
  impl_->mapper=vtkSmartPointer<vtkPolyDataMapper>::New();
  impl_->mapper->ScalarVisibilityOff();
  impl_->actor=vtkSmartPointer<vtkActor>::New();impl_->actor->SetMapper(impl_->mapper);
  const double default_material[]{.15,.8,.35,1,.2,.75,.25,30};
  material(impl_->actor->GetProperty(),default_material);
  impl_->lights=vtkSmartPointer<vtkLightKit>::New();
  impl_->lights->SetKeyLightIntensity(.8);
  impl_->lights->AddLightsToRenderer(impl_->renderer);
  impl_->renderer->AddActor(impl_->actor);
  vtkNew<vtkCubeSource> cube;cube->SetCenter(0,0,0);cube->SetXLength(.4);cube->SetYLength(.4);cube->SetZLength(.4);cube->Update();
  impl_->mapper->SetInputData(cube->GetOutput());
  Pose initial; initial.position.z=2;
  render(initial, {}, {}, 640,640); // Initialize VTK's GL loader and render targets.
}
Renderer::~Renderer()=default;
void Renderer::make_current(){glfwMakeContextCurrent(impl_->main);}
GLFWwindow* Renderer::main_window()const{return impl_->main;}
GLFWwindow* Renderer::sdk_window()const{return impl_->sdk;}
void Renderer::poll(){glfwPollEvents();}
bool Renderer::closing()const{return glfwWindowShouldClose(impl_->main)!=0;}
void Renderer::set_polydata(vtkPolyData* data,double scale,bool ras){
  if(!data||data->GetNumberOfPoints()==0)throw std::invalid_argument("Scene is empty");
  if(!std::isfinite(scale)||scale<=0)throw std::invalid_argument("meters-per-unit must be positive and finite");
  vtkNew<vtkMatrix4x4> m;m->Zero();m->SetElement(3,3,1);
  m->SetElement(0,0,scale);
  if(ras){m->SetElement(1,2,scale);m->SetElement(2,1,-scale);}
  else{m->SetElement(1,1,scale);m->SetElement(2,2,scale);}
  vtkNew<vtkTransform> t;t->SetMatrix(m);
  vtkNew<vtkTransformPolyDataFilter> filter;filter->SetTransform(t);filter->SetInputData(data);filter->Update();
  // Deep-copy a complete scene before swapping it into the active mapper.
  vtkNew<vtkPolyData> copy;copy->DeepCopy(filter->GetOutput());
  make_current();
  impl_->renderer->RemoveAllViewProps();impl_->volume_mapper=nullptr;
  impl_->renderer->AddActor(impl_->actor);
  impl_->mapper->SetInputData(copy);
}
void Renderer::load_vtp(const std::string& path,double scale,bool ras){
  vtkNew<vtkXMLPolyDataReader> reader;
  if(!reader->CanReadFile(path.c_str()))throw std::runtime_error("Cannot read VTP scene: "+path);
  reader->SetFileName(path.c_str());reader->Update();set_polydata(reader->GetOutput(),scale,ras);
}
void Renderer::load_scene(const std::string& path,double scale,bool ras){
  if(std::filesystem::path(path).extension()==".vtp"){load_vtp(path,scale,ras);return;}
  if(std::filesystem::path(path).extension()!=".vtm")throw std::invalid_argument("Scene must be .vtp or .vtm");
  auto conversion=basis(scale,ras);
  vtkNew<vtkXMLMultiBlockDataReader> reader;reader->SetFileName(path.c_str());reader->Update();
  if(reader->GetErrorCode())throw std::runtime_error("Cannot read scene: "+path);
  vtkSmartPointer<vtkCompositeDataIterator> iterator;
  iterator.TakeReference(reader->GetOutput()->NewIterator());iterator->SkipEmptyNodesOff();
  std::vector<vtkSmartPointer<vtkProp3D>> props;
  vtkSmartPointer<vtkOpenGLGPUVolumeRayCastMapper> volume_mapper;
  vtkBoundingBox bounds;double fit_size=-1;
  for(iterator->InitTraversal();!iterator->IsDoneWithTraversal();iterator->GoToNextItem()){
    auto* data=iterator->GetCurrentDataObject();
    if(!data)throw std::runtime_error("Scene contains a missing data block");
    if(field(data,"version",1,1)->GetComponent(0,0)!=2)throw std::runtime_error("Unsupported scene version");
    const double fit=field(data,"fit_size_m",1,1)->GetComponent(0,0);
    if(fit<0||(fit_size>=0&&fit!=fit_size))throw std::runtime_error("Inconsistent scene fitting");
    fit_size=fit;
    if(auto* poly=vtkPolyData::SafeDownCast(data)){
      if(!poly->GetNumberOfPolys())throw std::runtime_error("Scene surface is empty");
      auto mapper=vtkSmartPointer<vtkPolyDataMapper>::New();mapper->SetInputData(poly);
      const int mode=static_cast<int>(field(data,"scalar_mode",1,1)->GetComponent(0,0));
      if(mode==0)mapper->ScalarVisibilityOff();
      else if(mode==1||mode==2){
        auto* colors=mode==1?poly->GetPointData()->GetScalars():poly->GetCellData()->GetScalars();
        if(!colors||colors->GetDataType()!=VTK_UNSIGNED_CHAR||colors->GetNumberOfComponents()!=4)
          throw std::runtime_error("Scene scalar colors must be RGBA bytes");
        mapper->ScalarVisibilityOn();mapper->SetColorModeToDirectScalars();
        if(mode==1)mapper->SetScalarModeToUsePointData();else mapper->SetScalarModeToUseCellData();
      }else throw std::runtime_error("Unsupported surface color mode");
      auto actor=vtkSmartPointer<vtkActor>::New();actor->SetMapper(mapper);
      double values[8];field(data,"material",8,1)->GetTuple(0,values);material(actor->GetProperty(),values);
      props.push_back(actor);
    }else if(auto* image=vtkImageData::SafeDownCast(data)){
      if(volume_mapper)throw std::runtime_error("Only one rendered volume per scene is supported");
      if(!image->GetNumberOfPoints()||!image->GetPointData()->GetScalars()||image->GetNumberOfScalarComponents()!=1)
        throw std::runtime_error("Expected a non-empty scalar volume");
      int dims[3];image->GetDimensions(dims);
      for(int i=0;i<3;++i)if(dims[i]<2||image->GetSpacing()[i]<=0)throw std::runtime_error("Volume must have three nonempty spatial dimensions");
      auto actor=vtkSmartPointer<vtkVolume>::New();
      vtkNew<vtkMatrix4x4> matrix;auto* transform=field(data,"matrix",16,1);
      for(int r=0;r<4;++r)for(int c=0;c<4;++c)matrix->SetElement(r,c,transform->GetComponent(0,r*4+c));
      if(std::abs(matrix->Determinant())<1e-15)throw std::runtime_error("Volume transform is singular");
      actor->SetUserMatrix(matrix);
      vtkNew<vtkColorTransferFunction> color;
      auto* colors=field(data,"color",6);
      for(vtkIdType i=0;i<colors->GetNumberOfTuples();++i){double n[6];colors->GetTuple(i,n);color->AddRGBPoint(n[0],n[1],n[2],n[3],n[4],n[5]);}
      vtkNew<vtkPiecewiseFunction> opacity,gradient;
      auto* alphas=field(data,"opacity",4);
      for(vtkIdType i=0;i<alphas->GetNumberOfTuples();++i){double n[4];alphas->GetTuple(i,n);opacity->AddPoint(n[0],n[1],n[2],n[3]);}
      if(data->GetFieldData()->HasArray("qarvtk.gradient")){
        auto* gradients=field(data,"gradient",4);
        for(vtkIdType i=0;i<gradients->GetNumberOfTuples();++i){double n[4];gradients->GetTuple(i,n);gradient->AddPoint(n[0],n[1],n[2],n[3]);}
      }
      double v[9];field(data,"volume",9,1)->GetTuple(0,v);
      if(v[0]<=0||v[8]!=3)throw std::runtime_error("Invalid volume opacity distance or color function");
      vtkNew<vtkVolumeProperty> property;property->SetColor(color);property->SetScalarOpacity(opacity);
      if(gradient->GetSize())property->SetGradientOpacity(gradient);
      property->SetScalarOpacityUnitDistance(v[0]);property->SetShade(static_cast<int>(v[1]));
      property->SetAmbient(v[2]);property->SetDiffuse(v[3]);property->SetSpecular(v[4]);property->SetSpecularPower(v[5]);
      property->SetInterpolationType(static_cast<int>(v[6]));property->SetDisableGradientOpacity(static_cast<int>(v[7]));
      volume_mapper=vtkSmartPointer<vtkOpenGLGPUVolumeRayCastMapper>::New();
      volume_mapper->SetInputData(image);volume_mapper->SetBlendModeToComposite();
      volume_mapper->AutoAdjustSampleDistancesOff();volume_mapper->SetImageSampleDistance(1);
      volume_mapper->LockSampleDistanceToInputSpacingOn();volume_mapper->RenderToImageOn();
      volume_mapper->ClampDepthToBackfaceOff();volume_mapper->UseJitteringOff();
      actor->SetMapper(volume_mapper);actor->SetProperty(property);props.push_back(actor);
    }else throw std::runtime_error("Unsupported scene data type");
    double b[6];props.back()->GetBounds(b);
    for(double x:b)if(!std::isfinite(x))throw std::runtime_error("Scene bounds are not finite");
    bounds.AddBounds(b);
  }
  if(props.empty()||!bounds.IsValid())throw std::runtime_error("Scene is empty");
  if(fit_size>0){
    double center[3],lengths[3];bounds.GetCenter(center);bounds.GetLengths(lengths);
    const double extent=std::max({lengths[0],lengths[1],lengths[2]});
    if(extent<=0)throw std::runtime_error("Scene has no spatial extent");
    conversion=basis(fit_size/extent,ras);
    for(int r=0;r<3;++r){double offset=0;for(int c=0;c<3;++c)offset-=conversion->GetElement(r,c)*center[c];conversion->SetElement(r,3,offset);}
  }
  for(auto& prop:props){
    vtkNew<vtkMatrix4x4> matrix;vtkMatrix4x4::Multiply4x4(conversion,prop->GetMatrix(),matrix);prop->SetUserMatrix(matrix);
  }
  // All validation and construction precede the swap: a failed update retains
  // the previous complete scene. GPU resources are released in their context.
  make_current();impl_->renderer->RemoveAllViewProps();impl_->volume_mapper=volume_mapper;
  for(auto& prop:props)impl_->renderer->AddViewProp(prop);
  std::cout<<"Loaded scene: "<<props.size()<<" objects, "<<(volume_mapper?1:0)<<" volume; fit="<<fit_size<<" m\n";
}
RenderedEye Renderer::render(Pose pose,Fov fov,Clip clip,unsigned width,unsigned height){
  validate(pose);const auto matrix=projection(fov,clip);
  if(!width||!height||width>16384||height>16384)throw std::invalid_argument("Invalid render dimensions");
  make_current();
  auto* c=impl_->renderer->GetActiveCamera();
  const auto forward=rotate(pose.orientation,{0,0,-1}),up=rotate(pose.orientation,{0,1,0});
  c->SetPosition(pose.position.x,pose.position.y,pose.position.z);
  c->SetFocalPoint(pose.position.x+forward.x,pose.position.y+forward.y,pose.position.z+forward.z);
  c->SetViewUp(up.x,up.y,up.z);c->SetClippingRange(clip.near_plane,clip.far_plane);
  vtkNew<vtkMatrix4x4> p;for(int i=0;i<4;++i)for(int j=0;j<4;++j)p->SetElement(i,j,matrix[i*4+j]);
  c->SetExplicitProjectionTransformMatrix(p);c->SetUseExplicitProjectionTransformMatrix(true);
  impl_->window->SetSize(static_cast<int>(width),static_cast<int>(height));impl_->window->Render();
  auto* fbo=impl_->window->GetRenderFramebuffer();
  auto* color=fbo?fbo->GetColorAttachmentAsTextureObject(0):nullptr;
  auto* depth=fbo?fbo->GetDepthAttachmentAsTextureObject():nullptr;
  if(!color||!depth)throw std::runtime_error("VTK did not expose color/depth textures; MSAA must be disabled");
  RenderedEye result{color->GetHandle(),depth->GetHandle(),width,height};
  impl_->presented_fbo=fbo->GetFBOIndex();
  if(impl_->volume_mapper){
    auto* vc=impl_->volume_mapper->GetColorTexture();auto* vd=impl_->volume_mapper->GetDepthTexture();
    if(!vc||!vd)throw std::runtime_error("Volume renderer did not expose its color/depth textures");
    if(!impl_->composite)impl_->composite=std::make_unique<GlComposite>();
    result=impl_->composite->combine(result,vc->GetHandle(),vd->GetHandle());
    impl_->presented_fbo=impl_->composite->framebuffer();
  }
  return result;
}
void Renderer::present(){
  make_current();GLint previousRead=0,previousDraw=0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previousDraw);
  auto* fbo=impl_->window->GetRenderFramebuffer();const auto* size=impl_->window->GetSize();
  int w=0,h=0;glfwGetFramebufferSize(impl_->main,&w,&h);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,impl_->presented_fbo);glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
  glBlitFramebuffer(0,0,size[0],size[1],0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER,previousRead);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previousDraw);
  glfwSwapBuffers(impl_->main);
}
std::string Renderer::capabilities()const{
  std::ostringstream o;o<<"VTK "<<vtkVersion::GetVTKVersion()<<"; GL "<<glGetString(GL_VERSION)
    <<"; vendor "<<glGetString(GL_VENDOR)<<"; renderer "<<glGetString(GL_RENDERER);return o.str();
}
}
