// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_CRYPTOHOME_CLIENT_IMPL_H_
#define RMAD_SYSTEM_CRYPTOHOME_CLIENT_IMPL_H_

#include "rmad/system/cryptohome_client.h"

#include <cstdint>
#include <memory>

#include <base/memory/scoped_refptr.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <user_data_auth-client/user_data_auth/dbus-proxies.h>

namespace rmad {

class CryptohomeClientImpl : public CryptohomeClient {
 public:
  explicit CryptohomeClientImpl(const scoped_refptr<dbus::Bus>& bus);
  explicit CryptohomeClientImpl(
      std::unique_ptr<org::chromium::InstallAttributesInterfaceProxyInterface>
          install_attributes_proxy);
  CryptohomeClientImpl(const CryptohomeClientImpl&) = delete;
  CryptohomeClientImpl& operator=(const CryptohomeClientImpl&) = delete;

  ~CryptohomeClientImpl() override = default;

  bool IsCcdBlocked() override;

 private:
  bool GetFwmp(uint32_t* flags);

  std::unique_ptr<org::chromium::InstallAttributesInterfaceProxyInterface>
      install_attributes_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_CRYPTOHOME_CLIENT_IMPL_H_
