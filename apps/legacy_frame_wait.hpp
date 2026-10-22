#pragma once
#include <qar_streaming.h>
#include <string_view>

namespace qarvtk {
enum class LegacyFrameWait { none, paused, first_pose };

// The session-022c40b6 C API omits statuses 602/801 and a cause iterator.
// Match the captured 803 wrappers precisely. Missing pose data is retried only
// before any frame has been submitted, inside the caller's fixed frame deadline.
// This does not prove that the receiver will eventually provide a valid pose.
inline LegacyFrameWait legacy_begin_frame_wait(QarResult result, bool first_frame) {
  if (result.code != QAR_STATUS_RENDERING_PRODUCER_UNABLE_TO_DO_BEGIN_FRAME ||
      !result.error_handle) return LegacyFrameWait::none;
  char message[4096]{};
  qar_result_message(result, message, sizeof(message));
  message[sizeof(message)-1] = '\0';
  const std::string_view detail(message);
  if (detail.find("wrapped error: Code: 602 Desc: MediaStreamingStreamIsPaused Msg:") !=
      std::string_view::npos) return LegacyFrameWait::paused;
  if (first_frame && detail.find(
      "wrapped error: Code: 801 Desc: RenderingProducerMissingPoseData Msg:") !=
      std::string_view::npos) return LegacyFrameWait::first_pose;
  return LegacyFrameWait::none;
}
} // namespace qarvtk
