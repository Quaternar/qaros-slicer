#include "qarvtk/host_bridge.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <qar_qt_streaming/offscreen_panel.hpp>
#include <qar_qt_streaming/qt_panel_stream_manager.hpp>
#include <qar_qt_streaming/seam/panel_gesture_mapper.hpp>
#include <stdexcept>

namespace qtpanels = qar_qt_streaming;
namespace seam = qtpanels::seam;
namespace {
constexpr const char *panel_key = "slice-controls";
double seconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void message(char *target, uint32_t capacity, const char *text) {
  if (target && capacity) {
    std::strncpy(target, text, capacity - 1);
    target[capacity - 1] = 0;
  }
}

// The existing host owns the session, exact receiver audience and SDK worker.
// Qt supplies pixels and consumes copied input through this narrow adapter.
class Sender final : public seam::IPanelSender {
  QarvtkHost *host_;
  QarvtkHostPanel *panel_{};
  std::string error_;
  std::vector<uint8_t> pending_;
  unsigned width_{}, height_{};
  bool flush() {
    if (pending_.empty())
      return true;
    char error[1024]{};
    const auto result = qarvtk_host_panel_capture_rgba(host_, panel_, pending_.data(), width_,
                                                       height_, width_ * 4, error, sizeof(error));
    if (result < 0) {
      error_ = error;
      return false;
    }
    if (result > 0)
      pending_.clear();
    return true;
  }

public:
  seam::GestureSlot input_slot;
  Sender(QarvtkHost *host, const seam::PanelDesc &desc) : host_(host) {
    input_slot = {panel_key, desc.content_width ? desc.content_width : desc.buffer_width,
                  desc.content_height ? desc.content_height : desc.buffer_height};
    char error[1024]{};
    const auto &p = desc.placement.pose.position;
    if (qarvtk_host_control_panel_create(
            host, desc.display_name.c_str(), desc.buffer_width, desc.buffer_height, p.x, p.y, p.z,
            desc.placement.size.width_meters, desc.placement.size.height_meters, &panel_, error,
            sizeof(error)) != 0)
      throw std::runtime_error(error);
  }
  ~Sender() override { close(false); }
  bool try_begin_frame(bool &ready) override {
    char status[1024]{};
    qarvtk_host_panel_status(host_, panel_, status, sizeof(status));
    if (std::string(status).rfind("Error:", 0) == 0) {
      error_ = status;
      return false;
    }
    ready = panel_ != nullptr;
    return flush();
  }
  bool submit_frame(const seam::PanelPixels &pixels) override {
    width_ = pixels.width;
    height_ = pixels.height;
    pending_.resize(std::size_t(width_) * height_ * 4);
    for (unsigned y = 0; y < height_; ++y)
      std::memcpy(pending_.data() + std::size_t(y) * width_ * 4,
                  pixels.data + std::size_t(y) * pixels.stride_bytes, width_ * 4);
    return flush();
  }
  bool set_placement(const seam::PanelPlacement &) override {
    // The widget is fixed-size. Placement is applied at creation and the AR
    // operator owns later movement and resizing, as with the slice previews.
    return true;
  }
  bool supports_state() const override { return true; }
  bool set_state(seam::PanelState state) override {
    return qarvtk_host_panel_visible(host_, panel_, state == seam::PanelState::Visible) == 0;
  }
  seam::PanelCloseReport close(bool) override {
    seam::PanelCloseReport report;
    if (panel_) {
      qarvtk_host_panel_close(host_, panel_);
      panel_ = nullptr;
      report.panel = seam::PanelCloseOutcome::Queued;
    }
    return report;
  }
  const std::string &last_error() const override { return error_; }
  QarvtkHostPanel *panel() const { return panel_; }
};
class Factory final : public seam::IPanelSenderFactory {
  QarvtkHost *host_;
  std::string error_;

public:
  std::shared_ptr<Sender> sender;
  explicit Factory(QarvtkHost *host) : host_(host) {}
  std::shared_ptr<seam::IPanelSender> create_panel_sender(const seam::PanelDesc &desc) override {
    try {
      sender = std::make_shared<Sender>(host_, desc);
      return sender;
    } catch (const std::exception &error) {
      error_ = error.what();
      return nullptr;
    }
  }
  const std::string &last_error() const override { return error_; }
};
struct Row {
  QLabel *name{};
  QSlider *slider{};
  QDoubleSpinBox *number{};
  double minimum{}, maximum{}, value{};
  bool changed{};
};
struct Controls {
  QarvtkHost *host;
  std::array<Row, 3> rows;
  std::unique_ptr<QWidget> widget;
  std::shared_ptr<Factory> factory;
  std::unique_ptr<qtpanels::QtPanelStreamManager> manager;
  seam::PanelGestureMapper mapper;
  uint64_t epoch{};
  bool active = true;
  explicit Controls(QarvtkHost *h, double x, double y, double z)
      : host(h), widget(std::make_unique<QWidget>()) {
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
      throw std::runtime_error("Slice controls need Slicer's QApplication");
    widget->setWindowTitle("Slice controls");
    widget->setFixedSize(640, 252);
    widget->setStyleSheet(
        "QWidget {background:#202735;color:white;font-size:18px;} QSlider::groove:horizontal "
        "{height:12px;background:#586477;} QSlider::handle:horizontal {width:28px;margin:-12px "
        "0;background:#e6eaf0;border-radius:7px;} QDoubleSpinBox {min-height:38px;}");
    auto *layout = new QGridLayout(widget.get());
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setHorizontalSpacing(16);
    const std::array<const char *, 3> names = {"Red", "Yellow", "Green"};
    const std::array<const char *, 3> colors = {"#ff7777", "#ffe066", "#75df9b"};
    for (std::size_t i = 0; i < rows.size(); ++i) {
      auto &row = rows[i];
      row.name = new QLabel(names[i], widget.get());
      row.name->setStyleSheet(QString("color:%1;").arg(colors[i]));
      row.slider = new QSlider(Qt::Horizontal, widget.get());
      row.slider->setRange(0, 10000);
      row.slider->setMinimumHeight(48);
      row.number = new QDoubleSpinBox(widget.get());
      row.number->setDecimals(3);
      row.number->setSuffix(" mm");
      row.number->setFixedWidth(150);
      row.slider->setObjectName(QString("sliceSlider%1").arg(i));
      layout->addWidget(row.name, int(i), 0);
      layout->addWidget(row.slider, int(i), 1);
      layout->addWidget(row.number, int(i), 2);
      QObject::connect(row.slider, &QSlider::valueChanged, widget.get(), [this, i](int tick) {
        auto &row = rows[i];
        row.value = row.minimum + (row.maximum - row.minimum) * tick / 10000.;
        row.changed = true;
        QSignalBlocker blocked(row.number);
        row.number->setValue(row.value);
      });
      QObject::connect(row.number, qOverload<double>(&QDoubleSpinBox::valueChanged), widget.get(),
                       [this, i](double value) {
                         auto &row = rows[i];
                         row.value = value;
                         row.changed = true;
                         QSignalBlocker blocked(row.slider);
                         row.slider->setValue(to_tick(row, value));
                       });
    }
    widget->setObjectName("qarvtkSliceControls");
    qtpanels::show_offscreen(widget.get(), 640, 252);
    factory = std::make_shared<Factory>(host);
    qtpanels::QtPanelStreamingConfig config;
    config.panel_sender_factory = factory;
    config.dialog_panels = false;
    manager = std::make_unique<qtpanels::QtPanelStreamManager>(config);
    qtpanels::PanelStreamOptions options;
    options.placement.pose.position = {float(x), float(y), float(z)};
    options.display_name = "Slice controls";
    options.render_scale = 1.0 / widget->devicePixelRatioF();
    options.meters_per_logical_px = .0008;
    options.max_fps = 15;
    options.input.delivery_mode = qtpanels::InputDeliveryMode::MouseOnly;
    const auto added = manager->add_panel(panel_key, widget.get(), options);
    if (!added.ok())
      throw std::runtime_error(added.message);
  }
  ~Controls() {
    manager.reset();
    factory.reset();
  }
  static int to_tick(const Row &row, double value) {
    if (row.maximum <= row.minimum)
      return 0;
    return int(std::lround(
        std::clamp((value - row.minimum) / (row.maximum - row.minimum) * 10000., 0., 10000.)));
  }
  void update(unsigned index, double minimum, double maximum, double step, double value,
              bool enabled) {
    if (index >= rows.size() || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        !std::isfinite(step) || !std::isfinite(value) || maximum < minimum || step <= 0)
      throw std::invalid_argument("Invalid slice slider range");
    auto &row = rows[index];
    row.minimum = minimum;
    row.maximum = maximum;
    row.slider->setEnabled(enabled);
    row.number->setEnabled(enabled);
    QSignalBlocker slider_blocked(row.slider), number_blocked(row.number);
    row.number->setRange(minimum, maximum);
    row.number->setSingleStep(step);
    row.number->setValue(value);
    row.value = row.number->value();
    row.slider->setValue(to_tick(row, row.value));
    row.slider->setSingleStep(
        maximum > minimum
            ? int(std::lround(std::clamp(step / (maximum - minimum) * 10000., 1., 10000.)))
            : 1);
  }
  unsigned tick(bool enabled) {
    if (active != enabled) {
      std::vector<seam::PanelPointerSample> canceled;
      mapper.cancel_lifecycles_for_panel(panel_key, seconds(), canceled);
      for (const auto &sample : canceled)
        manager->handle_sample(sample);
      manager->set_panel_ar_visible(panel_key, enabled);
      active = enabled;
    }
    if (!active)
      return manager->tick();
    if (factory->sender && factory->sender->panel()) {
      std::array<QarvtkPanelGesture, 128> events{};
      uint64_t current_epoch{};
      const auto count = qarvtk_host_panel_gestures(host, factory->sender->panel(), events.data(),
                                                    unsigned(events.size()), &current_epoch);
      if (count < 0)
        throw std::runtime_error("Cannot read Qt panel gestures");
      std::vector<seam::PanelPointerSample> canceled;
      if (current_epoch != epoch) {
        mapper.cancel_lifecycles_for_panel(panel_key, seconds(), canceled);
        epoch = current_epoch;
      }
      mapper.sweep_lifecycles(seconds(), canceled);
      for (const auto &sample : canceled)
        manager->handle_sample(sample);
      for (int i = 0; i < count; ++i) {
        const auto &raw = events[i];
        seam::PanelGestureEvent event;
        std::memcpy(event.peer.data, raw.peer, sizeof(raw.peer));
        event.gesture_id = raw.gesture_id;
        event.kind = static_cast<seam::PanelGestureKind>(raw.kind);
        event.phase = static_cast<seam::PanelGesturePhase>(raw.phase);
        event.hand = static_cast<seam::PointerHand>(raw.hand);
        event.tap_count = uint8_t(raw.tap_count);
        event.x_pixels = raw.x;
        event.y_pixels = raw.y;
        seam::PanelPointerSample sample;
        if (mapper.offer(event, factory->sender->input_slot, seconds(), sample) ==
            seam::GestureOutcome::Sampled)
          manager->handle_sample(sample);
      }
    }
    const auto submitted = manager->tick();
    if (!factory->last_error().empty())
      throw std::runtime_error(factory->last_error());
    if (factory->sender && !factory->sender->last_error().empty())
      throw std::runtime_error(factory->sender->last_error());
    return submitted;
  }
};
} // namespace
extern "C" {
__declspec(dllexport) const char *qarvtk_qt_version() { return qVersion(); }
__declspec(dllexport) void *qarvtk_qt_controls_create(QarvtkHost *host, double x, double y,
                                                      double z, char *error, uint32_t size) {
  try {
    return new Controls(host, x, y, z);
  } catch (const std::exception &e) {
    message(error, size, e.what());
    return nullptr;
  }
}
__declspec(dllexport) int qarvtk_qt_controls_update(void *handle, uint32_t index, double low,
                                                    double high, double step, double value,
                                                    uint32_t enabled, char *error, uint32_t size) {
  try {
    static_cast<Controls *>(handle)->update(index, low, high, step, value, enabled != 0);
    return 0;
  } catch (const std::exception &e) {
    message(error, size, e.what());
    return -1;
  }
}
__declspec(dllexport) int qarvtk_qt_controls_tick(void *handle, uint32_t active, char *error,
                                                  uint32_t size) {
  try {
    return int(static_cast<Controls *>(handle)->tick(active != 0));
  } catch (const std::exception &e) {
    message(error, size, e.what());
    return -1;
  }
}
__declspec(dllexport) int qarvtk_qt_controls_changed(void *handle, uint32_t index, double *value) {
  if (!handle || index >= 3 || !value)
    return 0;
  auto &row = static_cast<Controls *>(handle)->rows[index];
  if (!row.changed)
    return 0;
  *value = row.value;
  row.changed = false;
  return 1;
}
__declspec(dllexport) void qarvtk_qt_controls_destroy(void *handle) {
  delete static_cast<Controls *>(handle);
}
}
