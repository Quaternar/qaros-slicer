#include "qarvtk/host_bridge.h"
#include <QApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QPointer>
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
  bool flush_pending() { return flush(); }
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
class DirtyMonitor final : public QObject {
  QWidget *root_;
  bool &dirty_, &capturing_;

public:
  DirtyMonitor(QWidget *root, bool &dirty, bool &capturing)
      : root_(root), dirty_(dirty), capturing_(capturing) {
    qApp->installEventFilter(this);
  }
  ~DirtyMonitor() override { qApp->removeEventFilter(this); }
  bool eventFilter(QObject *object, QEvent *event) override {
    if (capturing_)
      return false;
    const auto type = event->type();
    if (type != QEvent::UpdateRequest && type != QEvent::Show && type != QEvent::Hide &&
        type != QEvent::Resize && type != QEvent::LayoutRequest && type != QEvent::EnabledChange)
      return false;
    for (auto *owner = object; owner; owner = owner->parent())
      if (owner == root_) {
        dirty_ = true;
        break;
      }
    return false;
  }
};
struct Controls {
  QarvtkHost *host;
  std::vector<Row> rows;
  std::unique_ptr<QWidget> widget;
  QGridLayout *layout{};
  std::shared_ptr<Factory> factory;
  std::unique_ptr<qtpanels::QtPanelStreamManager> manager;
  seam::PanelGestureMapper mapper;
  uint64_t epoch{};
  bool active = true;
  QPointer<QWidget> playback;
  bool playback_visible{};
  bool dirty = true, capturing{};
  double last_capture{};
  uint64_t captured_frames{};
  uint32_t last_pointer{};
  bool had_pointer{};
  std::unique_ptr<DirtyMonitor> monitor;
  explicit Controls(QarvtkHost *h, double x, double y, double z)
      : host(h), widget(std::make_unique<QWidget>()) {
    if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
      throw std::runtime_error("Slice controls need Slicer's QApplication");
    widget->setWindowTitle("Slice controls");
    widget->setFixedSize(640, 252);
    widget->setStyleSheet(
        "QWidget {background:#202735;color:white;font-size:18px;} QSlider::groove:horizontal "
        "{height:12px;background:#586477;} QSlider::handle:horizontal {width:44px;margin:-12px "
        "0;background:#e6eaf0;border-radius:7px;} QDoubleSpinBox {min-height:38px;}");
    layout = new QGridLayout(widget.get());
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setHorizontalSpacing(16);
    widget->setObjectName("qarvtkSliceControls");
    qtpanels::show_offscreen(widget.get(), 640, 252);
    monitor = std::make_unique<DirtyMonitor>(widget.get(), dirty, capturing);
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
  unsigned add_row(const char *name, const char *color) {
    if (rows.size() >= 8)
      throw std::invalid_argument("At most eight slice views are supported");
    const auto i = rows.size();
    if (playback)
      layout->removeWidget(playback);
    rows.emplace_back();
    auto &row = rows.back();
    row.name = new QLabel(QString::fromUtf8(name), widget.get());
    row.name->setStyleSheet(QString("color:%1;").arg(QString::fromUtf8(color)));
    row.slider = new QSlider(Qt::Horizontal, widget.get());
    row.slider->setRange(0, 10000);
    row.slider->setMinimumHeight(48);
    row.slider->setEnabled(false);
    row.number = new QDoubleSpinBox(widget.get());
    row.number->setDecimals(3);
    row.number->setSuffix(" mm");
    row.number->setFixedWidth(150);
    row.number->setEnabled(false);
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
    if (playback)
      layout->addWidget(playback, int(rows.size()), 0, 1, 3);
    resize();
    return unsigned(i);
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
  void attach_playback(QWidget *child) {
    if (!child || playback)
      throw std::invalid_argument("Expected one sequence controls widget");
    playback = child;
    layout->addWidget(child, int(rows.size()), 0, 1, 3);
    child->hide();
  }
  void show_playback(bool visible) {
    if (!playback || visible == playback_visible)
      return;
    playback_visible = visible;
    playback->setVisible(visible);
    resize();
  }
  void resize() {
    const int base = std::max(252, 72 + int(rows.size()) * 60);
    widget->setFixedHeight(
        std::min(768, base + (playback_visible && playback
                                  ? std::max(150, playback->sizeHint().height()) + 16
                                  : 0)));
    dirty = true;
  }
  std::string status() const {
    if (!factory->sender || !factory->sender->panel())
      return "Waiting for Qt panel";
    char status[2048]{};
    qarvtk_host_panel_status(host, factory->sender->panel(), status, sizeof(status));
    const auto stats = mapper.stats();
    const auto snapshot = manager->diagnostics();
    std::string result =
        std::string(status) + "; mapped=" + std::to_string(stats.events_received) +
        " missed=" + std::to_string(snapshot.input_samples_missed) +
        " input_size=" + std::to_string(factory->sender->input_slot.content_width) + "x" +
        std::to_string(factory->sender->input_slot.content_height);
    result += " sliders_down=" +
              std::to_string(std::count_if(rows.begin(), rows.end(), [](const auto &row) {
                return row.slider->isSliderDown();
              }));
    if (playback) {
      const auto sliders = playback->findChildren<QSlider *>();
      for (const auto *slider : sliders)
        result += " " + slider->objectName().toStdString() + "_down=" +
                  std::to_string(slider->isSliderDown());
    }
    if (had_pointer)
      result += " pointer_pressed=" +
                std::to_string(manager->pointer_state().is_pressed(last_pointer));
    if (!snapshot.panels.empty()) {
      const auto &panel = snapshot.panels.front();
      result += " Qt delivered=" + std::to_string(panel.input_events_delivered) +
                " dropped=" + std::to_string(panel.input_events_dropped);
      if (!panel.last_input_drop_reason.empty())
        result += " (" + panel.last_input_drop_reason + ")";
    }
    return result;
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
    dirty = true;
  }
  unsigned tick(bool enabled) {
    if (active != enabled) {
      std::vector<seam::PanelPointerSample> canceled;
      mapper.cancel_lifecycles_for_panel(panel_key, seconds(), canceled);
      for (const auto &sample : canceled)
        manager->handle_sample(sample);
      manager->set_panel_ar_visible(panel_key, enabled);
      active = enabled;
      dirty = true;
    }
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
      if (count)
        dirty = true;
      for (int i = 0; active && i < count; ++i) {
        const auto &raw = events[i];
        seam::PanelGestureEvent event;
        std::memcpy(event.peer.data, raw.peer, sizeof(raw.peer));
        event.gesture_id = raw.gesture_id;
        event.kind = static_cast<seam::PanelGestureKind>(raw.kind);
        event.phase = static_cast<seam::PanelGesturePhase>(raw.phase);
        event.hand = static_cast<seam::PointerHand>(raw.hand);
        last_pointer = seam::PanelGestureMapper::pointer_id_for(event.peer, event.hand);
        had_pointer = true;
        event.tap_count = uint8_t(raw.tap_count);
        event.x_pixels = raw.x;
        event.y_pixels = raw.y;
        seam::PanelPointerSample sample;
        if (mapper.offer(event, factory->sender->input_slot, seconds(), sample) ==
            seam::GestureOutcome::Sampled)
          manager->handle_sample(sample);
      }
    }
    unsigned submitted{};
    const auto now = seconds();
    if (dirty && now - last_capture >= 1. / 15.) {
      dirty = false;
      capturing = true;
      try {
        submitted = manager->tick();
      } catch (...) {
        capturing = false;
        throw;
      }
      capturing = false;
      last_capture = now;
      const auto snapshot = manager->diagnostics();
      const auto frames = snapshot.panels.empty() ? 0 : snapshot.panels.front().frames_captured;
      // The library has its own pacing gate. A tick it paces away must not
      // consume the only dirty notification (notably during a resize).
      if (!factory->sender || (active && frames == captured_frames) ||
          (!snapshot.panels.empty() && snapshot.panels.front().resize_pending))
        dirty = true;
      captured_frames = frames;
    }
    // A busy GPU upload retains only the latest image and is retried without repainting Qt.
    if (factory->sender && !factory->sender->flush_pending())
      throw std::runtime_error(factory->sender->last_error());
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
  if (!handle || index >= static_cast<Controls *>(handle)->rows.size() || !value)
    return 0;
  auto &row = static_cast<Controls *>(handle)->rows[index];
  if (!row.changed)
    return 0;
  *value = row.value;
  row.changed = false;
  return 1;
}
__declspec(dllexport) int qarvtk_qt_controls_add_slice(void *handle, const char *name,
                                                       const char *color, char *error,
                                                       uint32_t size) {
  try {
    return int(static_cast<Controls *>(handle)->add_row(name, color));
  } catch (const std::exception &e) {
    message(error, size, e.what());
    return -1;
  }
}
__declspec(dllexport) int qarvtk_qt_controls_attach_playback(void *handle, const char *object_name,
                                                             char *error, uint32_t size) {
  try {
    QWidget *found{};
    for (auto *widget : QApplication::allWidgets()) {
      if (widget->objectName() == QString::fromUtf8(object_name)) {
        if (found)
          throw std::invalid_argument("Sequence widget name is not unique");
        found = widget;
      }
    }
    static_cast<Controls *>(handle)->attach_playback(found);
    return 0;
  } catch (const std::exception &e) {
    message(error, size, e.what());
    return -1;
  }
}
__declspec(dllexport) void qarvtk_qt_controls_playback_visible(void *handle, uint32_t visible) {
  static_cast<Controls *>(handle)->show_playback(visible != 0);
}
__declspec(dllexport) void qarvtk_qt_controls_status(void *handle, char *text, uint32_t size) {
  message(text, size, static_cast<Controls *>(handle)->status().c_str());
}
__declspec(dllexport) void qarvtk_qt_controls_destroy(void *handle) {
  delete static_cast<Controls *>(handle);
}
}
