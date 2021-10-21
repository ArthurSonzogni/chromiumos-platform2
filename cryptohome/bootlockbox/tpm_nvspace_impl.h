// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_
#define CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_

#include <memory>
#include <string>

#include <openssl/sha.h>

#include <brillo/dbus/dbus_connection.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "cryptohome/bootlockbox/tpm_nvspace.h"

namespace cryptohome {

struct BootLockboxNVSpace {
  uint16_t version;
  uint16_t flags;
  uint8_t digest[SHA256_DIGEST_LENGTH];
} __attribute__((packed));
constexpr uint8_t kNVSpaceVersion = 1;
constexpr uint32_t kNVSpaceSize = sizeof(BootLockboxNVSpace);

// Empty password is used for bootlockbox nvspace. Confidentiality
// is not required and the nvspace is write locked after user logs in.
constexpr char kWellKnownPassword[] = "";

// This class handles tpm operations to read, write, lock and define nv spaces.
// Usage:
//   auto nvspace_utility = TPMNVSpaceImpl();
//   nvspace_utility.Initialize();
//   nvspace_utility.WriteNVSpace(...);
class TPMNVSpaceImpl : public TPMNVSpace {
 public:
  TPMNVSpaceImpl() = default;

  // Constructor that does not take ownership of tpm_nvram.
  TPMNVSpaceImpl(org::chromium::TpmNvramProxyInterface* tpm_nvram,
                 org::chromium::TpmManagerProxyInterface* tpm_owner);

  TPMNVSpaceImpl(const TPMNVSpaceImpl&) = delete;
  TPMNVSpaceImpl& operator=(const TPMNVSpaceImpl&) = delete;

  ~TPMNVSpaceImpl() {}

  // Initializes tpm_nvram if necessary.
  // Must be called before issuing and calls to this utility.
  bool Initialize() override;

  // This method defines a non-volatile storage area in TPM for bootlockboxd
  // via tpm_managerd.
  bool DefineNVSpace() override;

  // This method writes |digest| to nvram space for bootlockboxd.
  bool WriteNVSpace(const std::string& digest) override;

  // Reads nvspace and extract |digest|.
  bool ReadNVSpace(std::string* digest, NVSpaceState* state) override;

  // Locks the bootlockbox nvspace for writing.
  bool LockNVSpace() override;

  // Register the callback that would be called when TPM ownership had been
  // taken.
  void RegisterOwnershipTakenCallback(
      const base::RepeatingClosure& callback) override;

 private:
  // Check the owner password presents in tpm_manager.
  bool IsOwnerPasswordPresent();

  // This method removes owner dependency from tpm_manager.
  bool RemoveNVSpaceOwnerDependency();

  // This method would be called when the ownership had been taken.
  void OnOwnershipTaken(const base::RepeatingClosure& callback,
                        const tpm_manager::OwnershipTakenSignal& signal);

  brillo::DBusConnection connection_;

  // Tpm manager interfaces that relays relays tpm request to tpm_managerd over
  // DBus. It is used for defining nvspace on the first boot. This object is
  // created in Initialize and should only be used in the same thread.
  std::unique_ptr<org::chromium::TpmNvramProxyInterface> default_tpm_nvram_;
  org::chromium::TpmNvramProxyInterface* tpm_nvram_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> default_tpm_owner_;
  org::chromium::TpmManagerProxyInterface* tpm_owner_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_
