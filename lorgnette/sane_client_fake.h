// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_FAKE_H_
#define LORGNETTE_SANE_CLIENT_FAKE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <sane/sane.h>

#include "lorgnette/sane_client.h"
#include "lorgnette/sane_device.h"
#include "lorgnette/sane_device_fake.h"

namespace lorgnette {

class SaneClientFake : public SaneClient {
 public:
  std::optional<std::vector<ScannerInfo>> ListDevices(
      brillo::ErrorPtr* error, bool local_only) override {
    return (list_devices_result_ ? std::make_optional(scanners_)
                                 : std::nullopt);
  }

  void SetListDevicesResult(bool value);
  void AddDevice(const std::string& name,
                 const std::string& manufacturer,
                 const std::string& model,
                 const std::string& type);
  void RemoveDevice(const std::string& name);

  void SetDeviceForName(const std::string& device_name,
                        std::unique_ptr<SaneDeviceFake> device);
  void SetIppUsbSocketDir(base::FilePath path);

 protected:
  std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error,
      SANE_Status* sane_status,
      const std::string& device_name) override;
  base::FilePath IppUsbSocketDir() const override;

 private:
  std::map<std::string, std::unique_ptr<SaneDeviceFake>> devices_;
  bool list_devices_result_;
  std::vector<ScannerInfo> scanners_;
  std::optional<base::FilePath> ippusb_socket_dir_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_FAKE_H_
