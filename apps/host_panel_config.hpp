#pragma once
#include <algorithm>
#include <cstring>
#include <qar_streaming.h>
#include <vector>

namespace qarvtk {
inline bool host_panel_frame_wait(QarResult result) {
  // Status 602 is absent from some supported legacy enum declarations.
  return qar_result_has_code(result, static_cast<QarStatusCode>(602)) ||
         qar_result_has_code(result, QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS) ||
         qar_result_has_code(result, QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING);
}

// get_or_create may return a retained panel without applying init.size.
// Apply the requested physical size once at stream startup, including reuse.
// Frame updates, visibility changes and target joins must not call this:
// subsequent manual resizing belongs to the user.
inline QarResult register_host_panel(QarSession *session, const QarGuiPanelInit &init,
                                     QarGuiPanelId &id, bool &registered,
                                     const QarStreamId *stream = nullptr) {
#if QARVTK_HOST_ONBOARDING_API
  auto result = qar_gui_panels_get_or_create(session, &init, &id);
#else
  auto result = qar_gui_panels_add_panel(session, &init, &id);
#endif
  if (!qar_result_is_success(result))
    return result;
  registered = true; // Also release the panel if applying its size fails.
  // GetOrCreate preserves a retained video panel's old stream and state.
  // A sender belongs to this run: explicitly bind it even when the panel's
  // stable common name already exists after a crash or disconnected session.
  if (stream) {
    result = qar_gui_panels_set_stream(session, &id, stream);
    if (!qar_result_is_success(result))
      return result;
  }
  result = qar_gui_panels_change_size(session, &id, &init.size);
  if (!qar_result_is_success(result))
    return result;
  return stream ? qar_gui_panels_set_state(session, &id, QAR_GUI_PANEL_STATE_VISIBLE) : result;
}

// At startup only: replace a retained panel's stale audience before sending
// any pixels. Keep a non-display source observer in desired, so removal can
// never turn the list into QAROS's broadcast-to-everyone empty audience.
inline QarResult apply_host_panel_audience(QarSession *session, const QarGuiPanelId &id,
                                           const std::vector<QarPeerId> &desired) {
  if (desired.empty())
    return {QAR_STATUS_LOGIC_ERROR, 0};
  std::vector<const QarPeerId *> additions;
  for (const auto &peer : desired)
    additions.push_back(&peer);
  auto result = qar_gui_panels_update_visible_to(session, &id, additions.data(), additions.size(),
                                                 nullptr, 0);
  if (!qar_result_is_success(result))
    return result;
  size_t count{};
  result = qar_query_gui_panels_count(session, &count);
  if (!qar_result_is_success(result))
    return result;
  if (count > 4096)
    return {QAR_STATUS_LOGIC_ERROR, 0};
  struct Handles {
    std::vector<QarGuiPanel *> values;
    ~Handles() {
      for (auto *p : values)
        if (p)
          qar_gui_panel_handle_destroy(p);
    }
  };
  Handles handles{std::vector<QarGuiPanel *>(count + 16, nullptr)};
  size_t written{};
  result = qar_query_gui_panels(session, handles.values.data(), handles.values.size(), &written);
  if (!qar_result_is_success(result))
    return result;
  if (written > handles.values.size())
    return {QAR_STATUS_LOGIC_ERROR, 0};
  for (size_t i = 0; i < written; ++i) {
    QarGuiPanelId candidate{};
    result = qar_gui_panel_get_id(handles.values[i], &candidate);
    if (!qar_result_is_success(result))
      return result;
    if (std::memcmp(candidate.data, id.data, sizeof(id.data)))
      continue;
    size_t peers_count{};
    result = qar_gui_panel_get_visible_to_peers_count(handles.values[i], &peers_count);
    if (!qar_result_is_success(result))
      return result;
    if (peers_count > 4096)
      return {QAR_STATUS_LOGIC_ERROR, 0};
    std::vector<QarPeerId> previous(peers_count + 16);
    size_t peers_written{};
    result = qar_gui_panel_get_visible_to_peers(handles.values[i], previous.data(), previous.size(),
                                                &peers_written);
    if (!qar_result_is_success(result))
      return result;
    if (peers_written > previous.size())
      return {QAR_STATUS_LOGIC_ERROR, 0};
    std::vector<const QarPeerId *> removals;
    for (size_t p = 0; p < peers_written; ++p)
      if (std::none_of(desired.begin(), desired.end(), [&](const auto &peer) {
            return !std::memcmp(peer.data, previous[p].data, sizeof(peer.data));
          }))
        removals.push_back(&previous[p]);
    return removals.empty() ? result
                            : qar_gui_panels_update_visible_to(session, &id, nullptr, 0,
                                                               removals.data(), removals.size());
  }
  return {QAR_STATUS_GUI_PANEL_INVALID_ID, 0};
}
} // namespace qarvtk
