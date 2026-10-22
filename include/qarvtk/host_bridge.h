#pragma once
#include <stdint.h>
#if defined(_WIN32) && defined(QARVTK_HOST_EXPORTS)
#define QARVTK_HOST_API __declspec(dllexport)
#else
#define QARVTK_HOST_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct QarvtkHost QarvtkHost;
typedef struct QarvtkHostPanel QarvtkHostPanel;
typedef struct QarvtkHostPlacement {
  double position[3], orientation[4], app_scale; /* app_pose in volume metres */
  double workspace_size[3]; /* full extents along volume X/Y/Z, in metres */
  double workspace_position[3]; /* cuboid centre in room metres */
} QarvtkHostPlacement;
typedef struct QarvtkHostVolume {
  QarvtkHostPlacement placement;
  const char* name; /* UTF-8; copied during create_volumes */
} QarvtkHostVolume;
typedef struct QarvtkHostEye {
  double position[3], orientation[4], fov[4]; /* x/y/z/w; left/right/up/down */
  uint32_t width, height, depth_width, depth_height;
} QarvtkHostEye;
typedef struct QarvtkHostFrame {
  uint64_t serial;
  QarvtkHostEye eyes[2];
  double near_plane, far_plane; /* app coordinates, also reported to SDK show_frame */
  uint32_t volume_index, reserved; /* index in create_volumes; reserved must be zero */
} QarvtkHostFrame;

/* ABI 5, Windows x64/session SDK only. All calls are on the creating GUI thread.
   poll: 0 waiting, 1 frame leased, 2 stopped, -1 error (message via status).
   Stop is asynchronous; destroy returns 0 until the worker is finished.
   No borrowed GPU texture survives color/depth returning. SDK frame ownership
   remains leased until finish or stop. Never unload this DLL with live handles. */
QARVTK_HOST_API const char* qarvtk_host_info(void);
QARVTK_HOST_API int qarvtk_host_create(const char* sdk_root,const char* target,const char* log_folder,
                                     const QarvtkHostPlacement*,QarvtkHost** result,char* error,uint32_t error_size);
/* One session/viewer, one independently movable app volume and sender per entry.
   At most one frame is leased to the GUI at a time, fairly across ready senders. */
QARVTK_HOST_API int qarvtk_host_create_volumes(const char* sdk_root,const char* target,const char* log_folder,
    const QarvtkHostVolume*,uint32_t count,QarvtkHost** result,char* error,uint32_t error_size);
QARVTK_HOST_API int qarvtk_host_poll(QarvtkHost*,QarvtkHostFrame*);
QARVTK_HOST_API int qarvtk_host_color(QarvtkHost*,uint64_t serial,uint32_t eye,
                                     uint32_t color,uint32_t depth,uint32_t width,uint32_t height);
QARVTK_HOST_API int qarvtk_host_depth(QarvtkHost*,uint64_t serial,uint32_t eye,
                                     uint32_t color,uint32_t depth,uint32_t volume_color,uint32_t volume_depth,
                                     uint32_t width,uint32_t height);
QARVTK_HOST_API int qarvtk_host_finish(QarvtkHost*,uint64_t serial);
QARVTK_HOST_API void qarvtk_host_stop(QarvtkHost*);
QARVTK_HOST_API int qarvtk_host_destroy(QarvtkHost*);
QARVTK_HOST_API void qarvtk_host_status(QarvtkHost*,char* text,uint32_t size);

/* Read-only mono previews. Create/capture with the source slice GL context
   current. Capture returns 1 copied, 0 busy/hidden/stopping, -1 panel error;
   it never waits for the SDK receiver and never stops the stereo stream.
   Close retires the panel; its opaque handle expires when host is destroyed. */
QARVTK_HOST_API int qarvtk_host_panel_create(QarvtkHost*,const char* name,uint32_t width,uint32_t height,
    double x,double y,double z,double width_meters,QarvtkHostPanel**,char* error,uint32_t error_size);
QARVTK_HOST_API int qarvtk_host_panel_capture(QarvtkHost*,QarvtkHostPanel*,uint32_t color,uint32_t width,uint32_t height,
    char* error,uint32_t error_size);
/* Copy only the image area, in bottom-left source pixels; no CPU readback. */
QARVTK_HOST_API int qarvtk_host_panel_capture_region(QarvtkHost*,QarvtkHostPanel*,uint32_t color,uint32_t width,uint32_t height,
    uint32_t x,uint32_t y,uint32_t crop_width,uint32_t crop_height,char* error,uint32_t error_size);
QARVTK_HOST_API int qarvtk_host_panel_visible(QarvtkHost*,QarvtkHostPanel*,uint32_t visible);
QARVTK_HOST_API void qarvtk_host_panel_close(QarvtkHost*,QarvtkHostPanel*);
QARVTK_HOST_API void qarvtk_host_panel_status(QarvtkHost*,QarvtkHostPanel*,char* text,uint32_t size);

#ifdef __cplusplus
}
#endif
