// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_
#define RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_

#include "rmad/system/shill_client.h"

#include <memory>

#include <base/memory/scoped_refptr.h>
#include <dbus/bus.h>
#include <shill/dbus-proxies.h>

namespace rmad {

class ShillClientImpl : public ShillClient {
 public:
  explicit ShillClientImpl(const scoped_refptr<dbus::Bus>& bus);
  explicit ShillClientImpl(
      std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
          flimflam_manager_proxy);
  ShillClientImpl(const ShillClientImpl&) = delete;
  ShillClientImpl& operator=(const ShillClientImpl&) = delete;

  ~ShillClientImpl() override = default;

  bool DisableCellular() const override;

 private:
  std::unique_ptr<org::chromium::flimflam::ManagerProxyInterface>
      flimflam_manager_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_SHILL_CLIENT_IMPL_H_
