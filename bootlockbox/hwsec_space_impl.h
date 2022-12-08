// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTLOCKBOX_HWSEC_SPACE_IMPL_H_
#define BOOTLOCKBOX_HWSEC_SPACE_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include <openssl/sha.h>

#include <brillo/dbus/dbus_connection.h>
#include <libhwsec/frontend/bootlockbox/frontend.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>

#include "bootlockbox/hwsec_space.h"

namespace bootlockbox {

struct BootLockboxSpace {
  uint16_t version;
  uint16_t flags;
  uint8_t digest[SHA256_DIGEST_LENGTH];
} __attribute__((packed));
inline constexpr uint8_t kSpaceVersion = 1;
inline constexpr uint32_t kSpaceSize = sizeof(BootLockboxSpace);

// Empty password is used for bootlockbox space. Confidentiality
// is not required and the space is write locked after user logs in.
inline constexpr char kWellKnownPassword[] = "";

// This class handles hwsec operations to read, write, lock and define nv
// spaces. Usage:
//   auto space_utility = HwsecSpaceImpl();
//   space_utility.Initialize();
//   space_utility.WriteSpace(...);
class HwsecSpaceImpl : public HwsecSpace {
 public:
  explicit HwsecSpaceImpl(std::unique_ptr<hwsec::BootLockboxFrontend> hwsec)
      : hwsec_(std::move(hwsec)) {}

  explicit HwsecSpaceImpl(std::unique_ptr<hwsec::BootLockboxFrontend> hwsec,
                          org::chromium::TpmManagerProxyInterface* tpm_owner)
      : hwsec_(std::move(hwsec)), tpm_owner_(tpm_owner) {}

  HwsecSpaceImpl(const HwsecSpaceImpl&) = delete;
  HwsecSpaceImpl& operator=(const HwsecSpaceImpl&) = delete;

  ~HwsecSpaceImpl() override = default;

  // Initializes hwsec_nvram if necessary.
  // Must be called before issuing and calls to this utility.
  bool Initialize() override;

  // This method defines a non-volatile storage area in Hwsec for bootlockboxd
  // via tpm_managerd.
  SpaceState DefineSpace() override;

  // This method writes |digest| to nvram space for bootlockboxd.
  bool WriteSpace(const std::string& digest) override;

  // Reads space and extract |digest|.
  SpaceState ReadSpace(std::string* digest) override;

  // Locks the bootlockbox space for writing.
  bool LockSpace() override;

  // Register the callback that would be called when Hwsec ownership had been
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

#endif  // BOOTLOCKBOX_HWSEC_SPACE_IMPL_H_
