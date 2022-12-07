// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_
#define BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include <openssl/sha.h>

#include <brillo/dbus/dbus_connection.h>
#include <libhwsec/frontend/bootlockbox/frontend.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "bootlockbox/tpm_nvspace.h"

namespace bootlockbox {

struct BootLockboxNVSpace {
  uint16_t version;
  uint16_t flags;
  uint8_t digest[SHA256_DIGEST_LENGTH];
} __attribute__((packed));
inline constexpr uint8_t kNVSpaceVersion = 1;
inline constexpr uint32_t kNVSpaceSize = sizeof(BootLockboxNVSpace);

// Empty password is used for bootlockbox nvspace. Confidentiality
// is not required and the nvspace is write locked after user logs in.
inline constexpr char kWellKnownPassword[] = "";

// This class handles tpm operations to read, write, lock and define nv spaces.
// Usage:
//   auto nvspace_utility = TPMNVSpaceImpl();
//   nvspace_utility.Initialize();
//   nvspace_utility.WriteNVSpace(...);
class TPMNVSpaceImpl : public TPMNVSpace {
 public:
  explicit TPMNVSpaceImpl(std::unique_ptr<hwsec::BootLockboxFrontend> hwsec)
      : hwsec_(std::move(hwsec)) {}

  explicit TPMNVSpaceImpl(std::unique_ptr<hwsec::BootLockboxFrontend> hwsec,
                          org::chromium::TpmManagerProxyInterface* tpm_owner)
      : hwsec_(std::move(hwsec)), tpm_owner_(tpm_owner) {}

  TPMNVSpaceImpl(const TPMNVSpaceImpl&) = delete;
  TPMNVSpaceImpl& operator=(const TPMNVSpaceImpl&) = delete;

  ~TPMNVSpaceImpl() override = default;

  // Initializes tpm_nvram if necessary.
  // Must be called before issuing and calls to this utility.
  bool Initialize() override;

  // This method defines a non-volatile storage area in TPM for bootlockboxd
  // via tpm_managerd.
  NVSpaceState DefineNVSpace() override;

  // This method writes |digest| to nvram space for bootlockboxd.
  bool WriteNVSpace(const std::string& digest) override;

  // Reads nvspace and extract |digest|.
  NVSpaceState ReadNVSpace(std::string* digest) override;

  // Locks the bootlockbox nvspace for writing.
  bool LockNVSpace() override;

  // Register the callback that would be called when TPM ownership had been
  // taken.
  void RegisterOwnershipTakenCallback(
      const base::RepeatingClosure& callback) override;

 private:
  // This method would be called when the ownership had been taken.
  void OnOwnershipTaken(const base::RepeatingClosure& callback,
                        const tpm_manager::OwnershipTakenSignal& signal);

  std::unique_ptr<hwsec::BootLockboxFrontend> hwsec_;

  // TODO(b/261693180): Remove these after we add the ready callback support in
  // libhwsec.
  brillo::DBusConnection connection_;
  std::unique_ptr<org::chromium::TpmManagerProxyInterface> default_tpm_owner_;
  org::chromium::TpmManagerProxyInterface* tpm_owner_;
};

}  // namespace bootlockbox

#endif  // BOOTLOCKBOX_TPM_NVSPACE_IMPL_H_
