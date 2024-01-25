// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_BACKING_DEVICE_FACTORY_H_
#define LIBSTORAGE_STORAGE_CONTAINER_BACKING_DEVICE_FACTORY_H_

#include "libstorage/storage_container/backing_device.h"

#include <memory>

#include <brillo/brillo_export.h>
#include "libstorage/platform/platform.h"

namespace libstorage {

// `BackingDeviceFactory` abstracts the creation of backing devices.
class BRILLO_EXPORT BackingDeviceFactory {
 public:
  explicit BackingDeviceFactory(Platform* platform);
  virtual ~BackingDeviceFactory() {}

  virtual std::unique_ptr<BackingDevice> Generate(
      const BackingDeviceConfig& config);

 private:
  Platform* platform_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_BACKING_DEVICE_FACTORY_H_
