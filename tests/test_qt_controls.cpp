// Exercise the production Qt event path with copied SDK gestures and an in-memory sender.
#include "../src/qt_slice_controls.cpp"
#include "qarvtk/host_panel_contact.hpp"
#include "qarvtk/host_panel_input.hpp"
#include <QPushButton>
#include <QThread>
#include <deque>
#include <iostream>

struct QarvtkHostPanel {
  unsigned width{}, height{};
};
namespace {
std::deque<QarvtkPanelGesture> input;
uint64_t input_epoch = 1;
unsigned captured{};
qarvtk::HostPanelInput sdk_input;
qarvtk::HostPanelContact contact;
void require(bool condition, const char *text) {
  if (!condition)
    throw std::runtime_error(text);
}
} // namespace
extern "C" {
int qarvtk_host_control_panel_create(QarvtkHost *, const char *, uint32_t width, uint32_t height,
                                     double, double, double, double, double,
                                     QarvtkHostPanel **panel, char *, uint32_t) {
  *panel = new QarvtkHostPanel{width, height};
  return 0;
}
int qarvtk_host_panel_capture_rgba(QarvtkHost *, QarvtkHostPanel *, const uint8_t *, uint32_t,
                                   uint32_t, uint32_t, char *, uint32_t) {
  ++captured;
  return 1;
}
void qarvtk_host_panel_status(QarvtkHost *, QarvtkHostPanel *, char *text, uint32_t size) {
  message(text, size, "Live");
}
int qarvtk_host_panel_visible(QarvtkHost *, QarvtkHostPanel *, uint32_t) { return 0; }
void qarvtk_host_panel_close(QarvtkHost *, QarvtkHostPanel *panel) { delete panel; }
int qarvtk_host_panel_gestures(QarvtkHost *, QarvtkHostPanel *, QarvtkPanelGesture *out,
                               uint32_t capacity, uint64_t *epoch) {
  *epoch = input_epoch;
  const auto count = std::min<std::size_t>(capacity, input.size());
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = input.front();
    input.pop_front();
  }
  return int(count);
}
}
#if defined(QARVTK_QT_CONTROLS_FIXTURE)
extern "C" __declspec(dllexport) void qarvtk_test_panel_gesture(int x, int y, unsigned kind,
                                                                unsigned phase, uint64_t id,
                                                                int origin_x, int origin_y) {
  QarvtkPanelGesture event{};
  event.peer[0] = 1;
  event.gesture_id = id;
  event.kind = kind;
  event.phase = phase;
  event.hand = 2;
  event.tap_count = kind == 1 ? 1 : 0;
  event.x = x;
  event.y = y;
  const auto decision = contact.offer(event, seconds());
  if (decision == qarvtk::HostPanelContact::Decision::drop)
    return;
  if (decision == qarvtk::HostPanelContact::Decision::repress) {
    event.phase = 0;
    origin_x = x;
    origin_y = y;
  }
  const auto batch = sdk_input.offer(event, origin_x, origin_y, seconds());
  for (unsigned i = 0; i < batch.count; ++i)
    qarvtk::enqueue_panel_input(input, batch.events[i]);
}
extern "C" __declspec(dllexport) void qarvtk_test_contact_sample(int64_t timestamp,
                                                                 double distance) {
  uint8_t peer[16]{};
  peer[0] = 1;
  const auto canceled = contact.sample(peer, 2, timestamp, distance, seconds());
  if (canceled) {
    sdk_input.cancel(*canceled);
    qarvtk::enqueue_panel_input(input, *canceled);
  }
}
extern "C" __declspec(dllexport) void qarvtk_test_panel_tap(int x, int y) {
  QarvtkPanelGesture event{};
  event.peer[0] = 1;
  event.kind = 1;
  event.phase = 3;
  event.hand = 2;
  event.tap_count = 1;
  event.x = x;
  event.y = y;
  qarvtk::enqueue_panel_input(input, event);
}
#else
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  try {
    {
      std::deque<QarvtkPanelGesture> backlog;
      QarvtkPanelGesture drag{};
      drag.peer[0] = 1;
      drag.hand = 2;
      drag.kind = 3;
      drag.gesture_id = 17;
      qarvtk::enqueue_panel_input(backlog, drag);
      for (int i = 0; i < 1000; ++i) {
        drag.phase = 1;
        drag.x = i;
        auto hover = drag;
        hover.kind = 0;
        hover.gesture_id = 80;
        qarvtk::enqueue_panel_input(backlog, hover);
        qarvtk::enqueue_panel_input(backlog, drag);
        auto other = drag;
        other.peer[0] = 2;
        qarvtk::enqueue_panel_input(backlog, other);
        other = drag;
        other.hand = 1;
        qarvtk::enqueue_panel_input(backlog, other);
      }
      drag.phase = 2;
      qarvtk::enqueue_panel_input(backlog, drag);
      require(backlog.size() == 6, "Motion backlog grew with obsolete pointer positions");
      require(backlog.front().phase == 0 && backlog.back().phase == 2,
              "Coalescing discarded a drag lifecycle boundary");
      for (unsigned i = 1; i < 5; ++i)
        require(backlog[i].x == 999, "Coalescing mixed different peers, hands or gestures");
      drag.phase = 1;
      qarvtk::enqueue_panel_input(backlog, drag);
      require(backlog.size() == 7 && backlog[5].phase == 2,
              "Coalescing crossed a release boundary");
    }
    Controls controls(reinterpret_cast<QarvtkHost *>(1), 0, 0, 0);
    for (const auto *name : {"Red", "Yellow", "Green"})
      controls.add_row(name, "#ffffff");
    for (unsigned i = 0; i < 3; ++i)
      controls.update(i, -100, 100, 1, 0, true);
    auto pump = [&] {
      app.processEvents();
      controls.tick(true);
      app.processEvents();
    };
    for (int i = 0; i < 30; ++i) {
      pump();
      QThread::msleep(15);
    }
    require(captured > 0, "Qt panel did not produce a frame");
    auto offer = [&](QPoint point, unsigned kind, unsigned phase, QPoint origin = QPoint(-1, -1)) {
      QarvtkPanelGesture event{};
      event.peer[0] = 1;
      event.gesture_id = phase == 3 ? 0 : 1;
      event.kind = kind;
      event.phase = phase;
      event.hand = 2;
      event.tap_count = kind == 1 ? 1 : 0;
      const auto &slot = controls.factory->sender->input_slot;
      event.x = int(point.x() * double(slot.content_width) / controls.widget->width());
      event.y = int(point.y() * double(slot.content_height) / controls.widget->height());
      if (origin.x() < 0)
        origin = point;
      const auto batch = sdk_input.offer(
          event, int(origin.x() * double(slot.content_width) / controls.widget->width()),
          int(origin.y() * double(slot.content_height) / controls.widget->height()), seconds());
      for (unsigned i = 0; i < batch.count; ++i)
        qarvtk::enqueue_panel_input(input, batch.events[i]);
      pump();
    };
    for (unsigned i = 0; i < 3; ++i) {
      auto *slider = controls.rows[i].slider;
      const auto start = slider->mapTo(controls.widget.get(), slider->rect().center());
      offer(start, 0, 0);
      // Live QAROS starts after the fingertip has already left the narrow thumb.
      // Also recover a first update whose started event was not received.
      offer(start + QPoint(40, 0), 3, i == 1 ? 1 : 0, start);
      offer(start + QPoint(90, 0), 3, 1, start);
      offer(start + QPoint(90, 0), 0, 1);
      offer(start + QPoint(90, 0), 3, 2, start);
      require(controls.rows[i].changed && controls.rows[i].value > 20,
              "Copied drag did not move the Qt slider");
      require(!slider->isSliderDown(), "Drag release left the slider pressed");
      const auto released_value = controls.rows[i].value;
      offer(start + QPoint(130, 0), 0, 1);
      require(!slider->isSliderDown() && controls.rows[i].value == released_value,
              "Hover after release continued dragging the slider");
      offer(start + QPoint(130, 0), 0, 2);
    }
    auto *group = new QWidget;
    auto *layout = new QVBoxLayout(group);
    auto *stop = new QPushButton("Stop", group);
    bool clicked{};
    QObject::connect(stop, &QPushButton::clicked, [&] { clicked = true; });
    layout->addWidget(stop);
    controls.attach_playback(group);
    controls.show_playback(true);
    for (int i = 0; i < 140; ++i) {
      pump();
      QThread::msleep(15);
      if (controls.factory->sender->input_slot.content_height ==
          unsigned(controls.widget->height()))
        break;
    }
    require(controls.widget->height() > 252, "Playback widget was not included in the panel");
    offer(stop->mapTo(controls.widget.get(), stop->rect().center()), 1, 3);
    if (!clicked) {
      const auto p = stop->mapTo(controls.widget.get(), stop->rect().center());
      std::cerr << "Button at " << p.x() << ',' << p.y() << " source=" << controls.widget->width()
                << 'x' << controls.widget->height()
                << " input=" << controls.factory->sender->input_slot.content_width << 'x'
                << controls.factory->sender->input_slot.content_height
                << " visible=" << stop->isVisible() << " enabled=" << stop->isEnabled() << '\n';
      std::cerr << controls.manager->diagnostics().panels[0].last_geometry_summary << '\n';
    }
    require(clicked, "Copied tap did not reach the embedded native Qt button");
    controls.add_row("Red2", "#ff7777");
    controls.update(3, -100, 100, 1, 0, true);
    for (int i = 0; i < 140; ++i) {
      pump();
      QThread::msleep(15);
      if (controls.factory->sender->input_slot.content_height ==
              unsigned(controls.widget->height()) &&
          !controls.manager->diagnostics().panels.front().resize_pending)
        break;
    }
    require(controls.factory->sender->input_slot.content_height == unsigned(controls.widget->height()),
            "Further slice layout did not finish resizing its input geometry");
    auto *extra = controls.rows[3].slider;
    const auto extra_start = extra->mapTo(controls.widget.get(), extra->rect().center());
    offer(extra_start, 3, 0);
    offer(extra_start + QPoint(90, 0), 3, 1);
    offer(extra_start + QPoint(90, 0), 3, 2);
    require(controls.rows[3].value > 20, "A further slice view was not interactive");
    for (int i = 0; i < 10; ++i) {
      pump();
      QThread::msleep(15);
    }
    const auto idle_captures = captured;
    const auto idle_paints = controls.manager->diagnostics().panels.front().frames_captured;
    for (int i = 0; i < 30; ++i) {
      pump();
      QThread::msleep(15);
    }
    require(captured == idle_captures, "An unchanged Qt panel uploaded redundant frames");
    require(controls.manager->diagnostics().panels.front().frames_captured == idle_paints,
            "An unchanged Qt panel was repainted unnecessarily");
    const auto held = controls.rows[0].slider->mapTo(controls.widget.get(),
                                                     controls.rows[0].slider->rect().center());
    offer(held, 3, 0);
    controls.tick(false);
    require(!controls.rows[0].slider->isSliderDown(), "Pausing did not cancel Qt input");
    std::cout << "PASS: copied gestures drag all three sliders, tap an embedded Qt widget and "
                 "cancel on pause\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#endif
