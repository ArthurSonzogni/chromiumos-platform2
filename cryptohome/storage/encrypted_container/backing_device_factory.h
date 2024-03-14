// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_BACKING_DEVICE_FACTORY_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_BACKING_DEVICE_FACTORY_H_

#include <memory>

#include <libstorage/platform/platform.h>

#include "cryptohome/storage/encrypted_container/backing_device.h"

namespace cryptohome {

// `BackingDeviceFactory` abstracts the creation of backing devices.
class BackingDeviceFactory {
 public:
  explicit BackingDeviceFactory(libstorage::Platform* platform);
  virtual ~BackingDeviceFactory() {}

  virtual std::unique_ptr<BackingDevice> Generate(
      const BackingDeviceConfig& config);

 private:
  libstorage::Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_BACKING_DEVICE_FACTORY_H_
