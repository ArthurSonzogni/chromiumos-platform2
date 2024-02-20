// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIMARY_IO_MANAGER_FAKE_UDEV_H_
#define PRIMARY_IO_MANAGER_FAKE_UDEV_H_

#include <memory>
#include "primary_io_manager/udev.h"

namespace primary_io_manager {

class FakeUdev : public Udev {
 public:
  FakeUdev() = default;
  FakeUdev(const FakeUdev&) = delete;
  FakeUdev& operator=(const FakeUdev&) = delete;

  ~FakeUdev() override = default;

  bool Init(const Udev::DeviceCallback& _device_added_callback,
            const Udev::DeviceCallback& _device_removed_callback) override {
    return true;
  }
};

class FakeUdevFactory : public UdevFactory {
 public:
  FakeUdevFactory() = default;
  FakeUdevFactory(const FakeUdevFactory&) = delete;
  FakeUdevFactory& operator=(const FakeUdevFactory&) = delete;

  std::unique_ptr<Udev> Create(
      const Udev::DeviceCallback& device_added_callback,
      const Udev::DeviceCallback& device_removed_callback) const override {
    auto udev = std::make_unique<FakeUdev>();
    udev->Init(device_added_callback, device_removed_callback);
    return udev;
  }
};

}  // namespace primary_io_manager

#endif  // PRIMARY_IO_MANAGER_FAKE_UDEV_H_
