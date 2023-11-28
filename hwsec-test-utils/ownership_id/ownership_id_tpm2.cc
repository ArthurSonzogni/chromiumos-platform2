// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <base/time/time.h>
#include <base/strings/string_number_conversions.h>
#include <crypto/sha2.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "hwsec-test-utils/ownership_id/ownership_id_tpm2.h"

namespace {
// A constant to represent the corner case.
constexpr char kNoLockoutPassword[] = "NO_LOCKOUT_PASSWORD";
// Default D-Bus connection Timeout
constexpr base::TimeDelta kDefaultTimeout = base::Minutes(5);
constexpr int kDefaultTimeoutMs = kDefaultTimeout.InMilliseconds();
}  // namespace

namespace hwsec_test_utils {

bool OwnershipIdTpm2::InitializeTpmManager() {
  scoped_refptr<dbus::Bus> bus =
      connection_.ConnectWithTimeout(kDefaultTimeout);
  if (!bus) {
    LOG(ERROR) << "Failed to connect to system bus through libbrillo";
    return false;
  }

  tpm_manager_ = std::make_unique<org::chromium::TpmManagerProxy>(bus);

  return true;
}

std::optional<std::string> OwnershipIdTpm2::Get() {
  if (!InitializeTpmManager()) {
    LOG(ERROR) << "InitializeTpmManager failed.";
    return std::nullopt;
  }

  tpm_manager::GetTpmStatusRequest status_request;
  tpm_manager::GetTpmStatusReply status_reply;

  if (brillo::ErrorPtr err;
      !tpm_manager_->GetTpmStatus(status_request, &status_reply, &err,
                                  kDefaultTimeoutMs) ||
      status_reply.status() != tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "GetTpmStatus failed.";
    return std::nullopt;
  }

  if (!status_reply.enabled()) {
    LOG(ERROR) << "TPM is not enabled.";
    return std::nullopt;
  }

  if (!status_reply.owned()) {
    // Return empty string for unowned status.
    return "";
  }

  if (status_reply.local_data().lockout_password().empty()) {
    LOG(WARNING) << "Empty lockout password.";
    return kNoLockoutPassword;
  }

  const std::string id =
      crypto::SHA256HashString(status_reply.local_data().lockout_password());
  return base::HexEncode(id.data(), id.length());
}

}  // namespace hwsec_test_utils
