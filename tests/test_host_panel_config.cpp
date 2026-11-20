#if !defined(_WIN32) && defined(__GNUC__)
#define __declspec(annotation)
#endif
#include "../apps/host_panel_config.hpp"
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

QAR_DEFINE_MODULE_STORAGE(GuiPanels, gui_panels);
QAR_DEFINE_MODULE_STORAGE(Result, result);
namespace {
QarGuiPanelSize stored{};
bool exists{}, fail_registration{}, fail_size{};
bool fail_stream{},fail_state{};
QarStreamId stored_stream{};
QarGuiPanelState stored_state{};
std::vector<int> calls;
QarResult get_or_create(QarSession*, const QarGuiPanelInit* init, QarGuiPanelId* id) {
  calls.push_back(1);
  if (fail_registration) return {QAR_STATUS_LOGIC_ERROR, 0};
  if (!exists) { stored = init->size; exists = true; }
  // SDK contract: init is ignored when this stable identity already exists.
  *id = qar_gui_panel_id_default(); id->data[0] = 7;
  return {QAR_STATUS_SUCCESS, 0};
}
QarResult change_size(QarSession*, const QarGuiPanelId* id, const QarGuiPanelSize* size) {
  assert(exists && id->data[0] == 7);
  calls.push_back(2);
  if (fail_size) return {QAR_STATUS_LOGIC_ERROR, 0};
  stored = *size;
  return {QAR_STATUS_SUCCESS, 0};
}
QarResult set_stream(QarSession*,const QarGuiPanelId* id,const QarStreamId* stream){
  assert(exists&&id->data[0]==7);calls.push_back(3);
  if(fail_stream)return {QAR_STATUS_LOGIC_ERROR,0};
  stored_stream=*stream;return {QAR_STATUS_SUCCESS,0};
}
QarResult set_state(QarSession*,const QarGuiPanelId* id,QarGuiPanelState state){
  assert(exists&&id->data[0]==7);calls.push_back(4);
  if(fail_state)return {QAR_STATUS_LOGIC_ERROR,0};
  stored_state=state;return {QAR_STATUS_SUCCESS,0};
}
}
int main() {
  g_qar_result_api.result_is_success = [](QarResult result) { return result.code == QAR_STATUS_SUCCESS; };
  g_qar_gui_panels_api.gui_panels_get_or_create = get_or_create;
  g_qar_gui_panels_api.gui_panels_change_size = change_size;
  g_qar_gui_panels_api.gui_panels_set_stream = set_stream;
  g_qar_gui_panels_api.gui_panels_set_state = set_state;
  for (double aspect : {16./9., 1., 9./16.}) {
    auto init = qar_gui_panel_init_default(); init.common_name = "slicer-test-panel-0";
    const double height = .3556 / std::hypot(aspect, 1.);
    init.size = {float(height*aspect), float(height), 1.f};
    for (bool retained : {false, true}) {
      exists = retained; stored = {.05f, .04f, 2.f}; calls.clear();
      auto id = qar_gui_panel_id_default(); bool registered = false;
      assert(qar_result_is_success(qarvtk::register_host_panel(nullptr, init, id, registered)));
      assert(registered && calls == std::vector<int>({1, 2}));
      assert(std::abs(std::hypot(stored.width_meters, stored.height_meters)/.0254 - 14.) < 1e-5);
      assert(std::abs(stored.width_meters/stored.height_meters - aspect) < 1e-6);
      assert(stored.content_scale == 1.f);
    }
  }
  auto init = qar_gui_panel_init_default(); auto id = qar_gui_panel_id_default();
  bool registered = false; fail_registration = true; calls.clear();
  assert(!qar_result_is_success(qarvtk::register_host_panel(nullptr, init, id, registered)));
  assert(!registered && calls == std::vector<int>({1}));
  fail_registration = false; fail_size = true; calls.clear();
  assert(!qar_result_is_success(qarvtk::register_host_panel(nullptr, init, id, registered)));
  assert(registered && calls == std::vector<int>({1, 2})); // Cleanup must close it.
  fail_size=false;
  auto stream=qar_stream_id_default();stream.data[0]=42;
  for(bool retained:{false,true}){
    exists=retained;stored_stream=qar_stream_id_default();stored_stream.data[0]=9;
    stored_state=QAR_GUI_PANEL_STATE_HIDDEN;calls.clear();registered=false;
    assert(qar_result_is_success(qarvtk::register_host_panel(nullptr,init,id,registered,&stream)));
    assert(registered&&stored_stream.data[0]==42&&stored_state==QAR_GUI_PANEL_STATE_VISIBLE);
    assert(calls==std::vector<int>({1,3,2,4}));
  }
  fail_stream=true;calls.clear();registered=false;
  assert(!qar_result_is_success(qarvtk::register_host_panel(nullptr,init,id,registered,&stream)));
  assert(registered&&calls==std::vector<int>({1,3}));
  fail_stream=false;fail_state=true;calls.clear();registered=false;
  assert(!qar_result_is_success(qarvtk::register_host_panel(nullptr,init,id,registered,&stream)));
  assert(registered&&calls==std::vector<int>({1,3,2,4}));
  std::cout << "PASS: fresh and retained panels get 14-inch physical size; aspect ratio; registration/resize failures\n";
}
