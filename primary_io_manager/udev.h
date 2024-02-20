// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIMARY_IO_MANAGER_UDEV_H_
#define PRIMARY_IO_MANAGER_UDEV_H_

#include <libudev.h>

#include <memory>

#include "base/files/file_descriptor_watcher_posix.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "primary_io_manager/udev_scopers.h"

namespace primary_io_manager {

// Simple wrapper around libudev.
class Udev {
 public:
  using DeviceCallback =
      base::RepeatingCallback<void(const ScopedUdevDevicePtr)>;

  virtual ~Udev() = default;

  virtual bool Init(const DeviceCallback& device_added_callback,
                    const DeviceCallback& device_removed_callback) = 0;
};

// Udev wrapper implementation.
class UdevImpl : public Udev {
 public:
  UdevImpl();
  UdevImpl(const UdevImpl&) = delete;
  UdevImpl& operator=(const UdevImpl&) = delete;

  ~UdevImpl() override;

  // Initialize the wrapper object, registering callbacks for device monitor
  // events. Return value of false indicates initialization failure and an
  // unusable object.
  bool Init(const DeviceCallback& device_added_callback,
            const DeviceCallback& device_removed_callback) override;

 private:
  void OnDeviceAction();

  DeviceCallback device_added_callback_;
  DeviceCallback device_removed_callback_;

  ScopedUdevPtr udev_;
  ScopedUdevMonitorPtr monitor_;

  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;

  base::WeakPtrFactory<UdevImpl> weak_factory_{this};
};

// Factory for udev object.
class UdevFactory {
 public:
  virtual ~UdevFactory();

  // Create the udev object. Callbacks are invoked on input device
  // addition/removal.
  virtual std::unique_ptr<Udev> Create(
      const Udev::DeviceCallback& device_added_callback,
      const Udev::DeviceCallback& device_removed_callback) const = 0;
};

class UdevImplFactory : public UdevFactory {
 public:
  UdevImplFactory();
  UdevImplFactory(const UdevImplFactory&) = delete;
  UdevImplFactory& operator=(const UdevImplFactory&) = delete;

  ~UdevImplFactory() override;

  std::unique_ptr<Udev> Create(
      const Udev::DeviceCallback& device_added_callback,
      const Udev::DeviceCallback& device_removed_callback) const override;
};

}  // namespace primary_io_manager

#endif  // PRIMARY_IO_MANAGER_UDEV_H_
