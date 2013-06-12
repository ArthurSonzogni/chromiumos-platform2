// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_UDEV_MONITOR_H_
#define MIST_UDEV_MONITOR_H_

#include <base/basictypes.h>

struct udev_monitor;

namespace mist {

class UdevDevice;

// A udev monitor, which wraps a udev_monitor C struct from libudev and related
// library functions into a C++ object.
class UdevMonitor {
 public:
  static const int kInvalidFileDescriptor = -1;

  // Constructs a UdevMonitor object by taking a raw pointer to a udev_monitor
  // struct as |monitor|. The ownership of |monitor| is not transferred, but its
  // reference count is increased by one during the lifetime of this object.
  explicit UdevMonitor(udev_monitor* monitor);

  // Destructs this UdevMonitor object and decreases the reference count of the
  // underlying udev_monitor struct by one.
  virtual ~UdevMonitor();

  // Wraps udev_monitor_enable_receiving(). Returns true on success.
  virtual bool EnableReceiving();

  // Wraps udev_monitor_set_receive_buffer_size(). Returns true on success.
  virtual bool SetReceiveBufferSize(int size);

  // Wraps udev_monitor_get_fd().
  virtual int GetFileDescriptor() const;

  // Wraps udev_monitor_receive_device(). The returned UdevDevice object is not
  // managed and should be deleted by the caller after use.
  virtual UdevDevice* ReceiveDevice();

  // Wraps udev_monitor_filter_add_match_subsystem_devtype(). Returns true on
  // success.
  virtual bool FilterAddMatchSubsystemDeviceType(const char* subsystem,
                                                 const char* device_type);

  // Wraps udev_monitor_filter_add_match_tag(). Returns true on success.
  virtual bool FilterAddMatchTag(const char* tag);

  // Wraps udev_monitor_filter_update(). Returns true on success.
  virtual bool FilterUpdate();

  // Wraps udev_monitor_filter_remove(). Returns true on success.
  virtual bool FilterRemove();

 private:
  // Allows MockUdevMonitor to invoke the private default constructor below.
  friend class MockUdevMonitor;

  // Constructs a UdevMonitor object without referencing a udev_monitor struct,
  // which is only allowed to be called by MockUdevMonitor.
  UdevMonitor();

  udev_monitor* monitor_;

  DISALLOW_COPY_AND_ASSIGN(UdevMonitor);
};

}  // namespace mist

#endif  // MIST_UDEV_MONITOR_H_
