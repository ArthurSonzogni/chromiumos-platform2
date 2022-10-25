// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/store/pkcs11_util.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <chaps/isolate.h>
#include <chaps/pkcs11/cryptoki.h>
#include <chaps/token_manager_client.h>

#include <memory>
#include <string>
#include <vector>

namespace shill {

namespace pkcs11 {

ScopedSession::ScopedSession(CK_SLOT_ID slot) {
  CK_RV rv = C_Initialize(nullptr);
  if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
    // This may be normal in a test environment.
    LOG(INFO) << "PKCS #11 is not available. C_Initialize rv: " << rv;
    return;
  }
  CK_FLAGS flags = CKF_RW_SESSION | CKF_SERIAL_SESSION;
  rv = C_OpenSession(slot, flags, nullptr, nullptr, &handle_);
  if (rv != CKR_OK) {
    LOG(ERROR) << "Failed to open PKCS #11 session. C_OpenSession rv: " << rv;
  }
}

ScopedSession::~ScopedSession() {
  if (IsValid() && (C_CloseSession(handle_) != CKR_OK)) {
    LOG(WARNING) << "Failed to close PKCS #11 session.";
  }
  handle_ = CK_INVALID_HANDLE;
}

bool GetUserSlot(const std::string& user_hash, CK_SLOT_ID_PTR slot) {
  const char kChapsSystemToken[] = "/var/lib/chaps";
  const char kChapsDaemonStore[] = "/run/daemon-store/chaps";
  base::FilePath token_path =
      user_hash.empty() ? base::FilePath(kChapsSystemToken)
                        : base::FilePath(kChapsDaemonStore).Append(user_hash);
  CK_RV rv;
  rv = C_Initialize(nullptr);
  if (rv != CKR_OK && rv != CKR_CRYPTOKI_ALREADY_INITIALIZED) {
    LOG(WARNING) << "C_Initialize failed. rv: " << rv;
    return false;
  }
  CK_ULONG num_slots = 0;
  rv = C_GetSlotList(CK_TRUE, nullptr, &num_slots);
  if (rv != CKR_OK) {
    LOG(WARNING) << "C_GetSlotList(nullptr) failed. rv: " << rv;
    return false;
  }
  std::vector<CK_SLOT_ID> slots;
  slots.resize(num_slots);
  rv = C_GetSlotList(CK_TRUE, slots.data(), &num_slots);
  if (rv != CKR_OK) {
    LOG(WARNING) << "C_GetSlotList failed. rv: " << rv;
    return false;
  }
  chaps::TokenManagerClient token_manager;
  // Look through all slots for |token_path|.
  for (CK_SLOT_ID curr_slot : slots) {
    base::FilePath slot_path;
    if (token_manager.GetTokenPath(
            chaps::IsolateCredentialManager::GetDefaultIsolateCredential(),
            curr_slot, &slot_path) &&
        (token_path == slot_path)) {
      *slot = curr_slot;
      return true;
    }
  }
  LOG(WARNING) << "Path not found.";
  return false;
}

}  // namespace pkcs11

}  // namespace shill
