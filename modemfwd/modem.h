// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_MODEM_H_
#define MODEMFWD_MODEM_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>

#include "modemfwd/modem_helper_directory.h"
#include "shill/dbus-proxies.h"

namespace modemfwd {

class Modem {
 public:
  virtual ~Modem() = default;

  // Get this modem's device ID.
  virtual std::string GetDeviceId() const = 0;

  // Get a unique identifier for this modem, such as an IMEI.
  virtual std::string GetEquipmentId() const = 0;

  // Get an ID for the carrier this modem is currently operating with,
  // or the empty string if there is none. Note that the ID is not
  // necessarily a readable name or e.g. MCC/MNC pair.
  virtual std::string GetCarrierId() const = 0;

  // Information about this modem's installed firmware.
  virtual std::string GetMainFirmwareVersion() const = 0;
  virtual std::string GetCarrierFirmwareId() const = 0;
  virtual std::string GetCarrierFirmwareVersion() const = 0;

  virtual bool FlashMainFirmware(const base::FilePath& path_to_fw) = 0;
  virtual bool FlashCarrierFirmware(const base::FilePath& path_to_fw) = 0;
};

std::unique_ptr<Modem> CreateModem(
    std::unique_ptr<org::chromium::flimflam::DeviceProxy> device,
    ModemHelperDirectory* helper_directory);

std::unique_ptr<Modem> CreateStubModem(const std::string& device_id,
                                       ModemHelperDirectory* helper_directory);

}  // namespace modemfwd

#endif  // MODEMFWD_MODEM_H_
