#pragma once
#include <qar_streaming.h>

namespace qarvtk {
// get_or_create may return a retained panel without applying init.size.
// Apply the requested physical size once at stream startup, including reuse.
// Frame updates, visibility changes and target joins must not call this:
// subsequent manual resizing belongs to the user.
inline QarResult register_host_panel(QarSession* session, const QarGuiPanelInit& init,
                                    QarGuiPanelId& id, bool& registered) {
#if QARVTK_HOST_ONBOARDING_API
  auto result = qar_gui_panels_get_or_create(session, &init, &id);
#else
  auto result = qar_gui_panels_add_panel(session, &init, &id);
#endif
  if (!qar_result_is_success(result)) return result;
  registered = true; // Also release the panel if applying its size fails.
  return qar_gui_panels_change_size(session, &id, &init.size);
}
}
