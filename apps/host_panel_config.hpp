#pragma once
#include <qar_streaming.h>

namespace qarvtk {
// get_or_create may return a retained panel without applying init.size.
// Apply the requested physical size once at stream startup, including reuse.
// Frame updates, visibility changes and target joins must not call this:
// subsequent manual resizing belongs to the user.
inline QarResult register_host_panel(QarSession* session, const QarGuiPanelInit& init,
                                    QarGuiPanelId& id, bool& registered,
                                    const QarStreamId* stream = nullptr) {
#if QARVTK_HOST_ONBOARDING_API
  auto result = qar_gui_panels_get_or_create(session, &init, &id);
#else
  auto result = qar_gui_panels_add_panel(session, &init, &id);
#endif
  if (!qar_result_is_success(result)) return result;
  registered = true; // Also release the panel if applying its size fails.
  // GetOrCreate preserves a retained video panel's old stream and state.
  // A sender belongs to this run: explicitly bind it even when the panel's
  // stable common name already exists after a crash or disconnected session.
  if (stream) {
    result = qar_gui_panels_set_stream(session, &id, stream);
    if (!qar_result_is_success(result)) return result;
  }
  result = qar_gui_panels_change_size(session, &id, &init.size);
  if (!qar_result_is_success(result)) return result;
  return stream ? qar_gui_panels_set_state(session, &id, QAR_GUI_PANEL_STATE_VISIBLE) : result;
}
}
