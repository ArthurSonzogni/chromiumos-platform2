// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GOBI_CROMO_PLUGIN_DEVICE_WATCHER_H_
#define GOBI_CROMO_PLUGIN_DEVICE_WATCHER_H_

#include <glib.h>

#include <string>

// Use udev to keep track of additions and removals of devices
struct udev;
struct udev_monitor;
struct udev_device;

typedef void (*DeviceCallback)(void* context,
                               const char* action,
                               const char* device);
typedef void (*TimeoutCallback)(void*);

class DeviceWatcher {
 public:
  explicit DeviceWatcher(const char* subsystem);
  ~DeviceWatcher();

  void StartMonitoring();
  void StopMonitoring();
  void StartPolling(int interval_secs,
                    TimeoutCallback callback,
                    void* userdata);
  void StopPolling();
  void HandleUdevEvent();
  void HandlePollEvent();

  void set_callback(DeviceCallback callback, void* userdata);

 private:
  std::string subsystem_;
  DeviceCallback device_callback_;
  void* device_callback_arg_;
  TimeoutCallback timeout_callback_;
  void* timeout_callback_arg_;
  struct udev* udev_;
  struct udev_monitor* udev_monitor_;
  guint udev_watch_id_;
  guint timeout_id_;
};

#endif  // GOBI_CROMO_PLUGIN_DEVICE_WATCHER_H_
