// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_
#define RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_

#include <memory>

#include <shill-client/shill/dbus-proxies.h>

#include "rmad/system/shill_client.h"

namespace rmad {

class ShillClientImpl : public ShillClient {
 public:
  ShillClientImpl();
  explicit ShillClientImpl(
      std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
          flimflam_manager_proxy);
  ShillClientImpl(const ShillClientImpl&) = delete;
  ShillClientImpl& operator=(const ShillClientImpl&) = delete;

  ~ShillClientImpl() override;

  bool DisableCellular() const override;

 private:
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      flimflam_manager_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_
