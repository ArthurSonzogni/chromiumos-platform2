// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_H_
#define LORGNETTE_SANE_CLIENT_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <sane/sane.h>

#include "lorgnette/sane_device.h"

namespace lorgnette {

// This class represents a connection to the scanner library SANE.  Once
// created, it will initialize a connection to SANE, and it will disconnect
// when destroyed.
// At most 1 connection to SANE is allowed to be active per process, so the
// user must be careful to ensure that is the case.
class SaneClient {
 public:
  virtual ~SaneClient() {}

  virtual std::optional<std::vector<ScannerInfo>> ListDevices(
      brillo::ErrorPtr* error) = 0;
  std::unique_ptr<SaneDevice> ConnectToDevice(brillo::ErrorPtr* error,
                                              SANE_Status* sane_status,
                                              const std::string& device_name);

 protected:
  virtual base::FilePath IppUsbSocketDir() const;

  virtual std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error,
      SANE_Status* sane_status,
      const std::string& device_name) = 0;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_H_
