/*
 * Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_USB_V4L2_EVENT_MONITOR_H_
#define CAMERA_HAL_USB_V4L2_EVENT_MONITOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <base/containers/flat_set.h>
#include <base/files/file_path.h>
#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>

#include "cros-camera/cros_camera_hal.h"

namespace cros {

// V4L2EventMonitor is a monitor for the status change of camera
// privacy switch and shutter events.
class V4L2EventMonitor {
 public:
  V4L2EventMonitor();
  V4L2EventMonitor(const V4L2EventMonitor&) = delete;
  V4L2EventMonitor& operator=(const V4L2EventMonitor&) = delete;
  ~V4L2EventMonitor();

  void RegisterCallback(PrivacySwitchStateChangeCallback callback);

  // Try to activate the event loop to subscribe the privacy event if it hasn't
  // been started by given |camera_id| and its corresponding |device_path|.
  void TrySubscribe(int camera_id,
                    const base::FilePath& device_path,
                    bool has_privacy_switch);

  // Remove the |camera_id| from the subscription list.
  void Unsubscribe(int camera_id);

 private:
  void OnStatusChanged(int camera_id, PrivacySwitchState state);

  // Subscribe the camera privacy switch status changed and shutter trace as
  // v4l2-events with given |camera_id| and |device_fd|.
  void SubscribeEvent(int camera_id,
                      base::ScopedFD device_fd,
                      bool has_privacy_switch);

  // Unsubscribe all v4l2-event.
  void UnsubscribeEvents();

  // Keep dequeuing the v4l2-events from device.
  void RunDequeueEventsLoop();

  // Triggers the event thread to restart the loop.
  void RestartEventLoop();

  PrivacySwitchState state_;

  PrivacySwitchStateChangeCallback callback_;

  // The thread for dequeuing v4l2-events.
  base::Thread event_thread_;

  base::Lock camera_id_lock_;
  // The map for subscribed camera ids and their file descriptors.
  base::flat_map<int, base::ScopedFD> subscribed_camera_id_to_fd_
      GUARDED_BY(camera_id_lock_);

  // The map for subscribed camera ids to a flag if the device has a HW privacy
  // switch.
  base::flat_set<int> subscribed_camera_ids_with_privacy_switch_
      GUARDED_BY(camera_id_lock_);

  // The endpoint for writing of the pipe to control the event loop. Writing
  // things into the pipe can restart the event loop. Closing the pipe will stop
  // the event loop.
  base::ScopedFD control_pipe_;

  // The endpoint for reading of the pipe to control the event loop.
  base::ScopedFD control_fd_;
};

}  // namespace cros

#endif  // CAMERA_HAL_USB_V4L2_EVENT_MONITOR_H_
