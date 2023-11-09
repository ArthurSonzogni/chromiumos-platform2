// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_IMPL_H_
#define LORGNETTE_SANE_CLIENT_IMPL_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/synchronization/lock.h>
#include <brillo/errors/error.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <sane/sane.h>

#include "lorgnette/libsane_wrapper.h"
#include "lorgnette/sane_client.h"
#include "lorgnette/sane_device.h"

namespace lorgnette {

using DeviceSet = std::pair<base::Lock, std::unordered_set<std::string>>;

class SaneClientImpl : public SaneClient {
 public:
  static std::unique_ptr<SaneClientImpl> Create(LibsaneWrapper* libsane);
  ~SaneClientImpl();

  std::optional<std::vector<ScannerInfo>> ListDevices(brillo::ErrorPtr* error,
                                                      bool local_only) override;

  static std::optional<std::vector<ScannerInfo>> DeviceListToScannerInfo(
      const SANE_Device** device_list);

 protected:
  std::unique_ptr<SaneDevice> ConnectToDeviceInternal(
      brillo::ErrorPtr* error,
      SANE_Status* sane_status,
      const std::string& device_name) override;

 private:
  explicit SaneClientImpl(LibsaneWrapper* libsane);

  LibsaneWrapper* libsane_;  // Not owned.
  base::Lock lock_;
  std::shared_ptr<DeviceSet> open_devices_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_IMPL_H_
