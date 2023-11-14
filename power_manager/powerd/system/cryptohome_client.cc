// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/cryptohome_client.h"

#include <algorithm>
#include <map>
#include <memory>

#include <base/check.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/dbus/data_serialization.h>
#include <brillo/files/file_util.h>
#include <chromeos/dbus/service_constants.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/message.h>
#include <dbus/cryptohome/dbus-constants.h>

namespace power_manager::system {

namespace {

// Maximum amount of time to wait for a reply from Cryptohome.
constexpr base::TimeDelta kCryptohomeDBusTimeout = base::Seconds(3);

}  // namespace

CryptohomeClient::CryptohomeClient() {}

CryptohomeClient::CryptohomeClient(DBusWrapperInterface* dbus_wrapper) {
  DCHECK(dbus_wrapper);

  dbus_wrapper_ = dbus_wrapper;
  cryptohome_proxy_ =
      dbus_wrapper_->GetObjectProxy(user_data_auth::kUserDataAuthServiceName,
                                    user_data_auth::kUserDataAuthServicePath);
}

void CryptohomeClient::EvictDeviceKey(int suspend_request_id) {
  user_data_auth::EvictDeviceKeyRequest request;
  request.set_eviction_id(suspend_request_id);
  dbus::MethodCall method_call(user_data_auth::kUserDataAuthInterface,
                               user_data_auth::kEvictDeviceKey);
  dbus::MessageWriter writer(&method_call);
  brillo::dbus_utils::WriteDBusArgs(&writer, request);
  std::unique_ptr<dbus::Response> response = dbus_wrapper_->CallMethodSync(
      cryptohome_proxy_, &method_call, kCryptohomeDBusTimeout);
  if (!response)
    return;

  user_data_auth::EvictDeviceKeyReply reply;
  if (!dbus::MessageReader(response.get()).PopArrayOfBytesAsProto(&reply)) {
    LOG(ERROR) << "Unable to parse EvictDeviceKeyReply message";
    return;
  }
  if (reply.error() !=
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "EvictDeviceKey() failed: " << reply.error();
  }
}

}  // namespace power_manager::system
