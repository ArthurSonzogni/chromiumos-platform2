// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <string>

#include <base/logging.h>
#include <base/macros.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
#include <brillo/syslog_logging.h>

#include "cryptohome/attestation.h"
#include "cryptohome/boot_lockbox.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/install_attributes.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"
#include "rpc.pb.h"  // NOLINT(build/include)

namespace cryptohome {

namespace tpm_manager {

int TakeOwnership(bool finalize) {
  base::Time start_time = base::Time::Now();
  cryptohome::Platform platform;
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  cryptohome::TpmInit tpm_init(tpm, &platform);
  tpm_init.SetupTpm(false);
  LOG(INFO) << "Initializing TPM.";
  bool took_ownership = false;
  if (!tpm_init.InitializeTpm(&took_ownership)) {
    LOG(ERROR) << "Failed to initialize TPM.";
    return -1;
  }
  cryptohome::InstallAttributes install_attributes(tpm);
  if (took_ownership && !install_attributes.PrepareSystem()) {
    LOG(ERROR) << "Failed to prepare install attributes NVRAM.";
    return -1;
  }
  if (!install_attributes.Init(&tpm_init)) {
    LOG(ERROR) << "Failed to initialize install attributes.";
    return -1;
  }
  if (!install_attributes.Finalize()) {
    LOG(ERROR) << "Failed to finalize install attributes.";
    return -1;
  }
  cryptohome::Crypto crypto(&platform);
  crypto.set_use_tpm(true);
  crypto.Init(&tpm_init);
  cryptohome::Attestation attestation;
  attestation.Initialize(tpm, &tpm_init, &platform, &crypto,
                         &install_attributes,
                         brillo::SecureBlob(), /* abe_data */
                         true /* retain_endorsement_data */);
  attestation.PrepareForEnrollment();
  if (!attestation.IsPreparedForEnrollment()) {
    LOG(ERROR) << "Failed to initialize attestation.";
    return -1;
  }
  if (finalize) {
    attestation.FinalizeEndorsementData();
    tpm_init.ClearStoredTpmPassword();
  }
  base::TimeDelta duration = base::Time::Now() - start_time;
  LOG(INFO) << "TPM initialization successful ("
            << duration.InMilliseconds() << " ms).";
  return 0;
}

int VerifyEK(bool is_cros_core) {
  cryptohome::Platform platform;
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  cryptohome::TpmInit tpm_init(tpm, &platform);
  tpm_init.SetupTpm(false);
  cryptohome::InstallAttributes install_attributes(tpm);
  install_attributes.Init(&tpm_init);
  cryptohome::Crypto crypto(&platform);
  crypto.set_use_tpm(true);
  crypto.Init(&tpm_init);
  cryptohome::Attestation attestation;
  attestation.Initialize(tpm, &tpm_init, &platform, &crypto,
                         &install_attributes,
                         brillo::SecureBlob(), /* abe_data */
                         true /* retain_endorsement_data */);
  if (!attestation.VerifyEK(is_cros_core)) {
    LOG(ERROR) << "Failed to verify TPM endorsement.";
    return -1;
  }
  LOG(INFO) << "TPM endorsement verified successfully.";
  return 0;
}

int DumpStatus() {
  cryptohome::Platform platform;
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  cryptohome::TpmInit tpm_init(tpm, &platform);
  tpm_init.SetupTpm(false);
  cryptohome::GetTpmStatusReply status;
  status.set_enabled(tpm_init.IsTpmEnabled());
  status.set_owned(tpm_init.IsTpmOwned());
  brillo::SecureBlob owner_password;
  if (tpm_init.GetTpmPassword(&owner_password)) {
    status.set_initialized(false);
    status.set_owner_password(owner_password.to_string());
  } else {
    // Initialized is true only when the TPM is owned and the owner password has
    // already been destroyed.
    status.set_initialized(status.owned());
  }
  int counter;
  int threshold;
  bool lockout;
  int seconds_remaining;
  if (tpm->GetDictionaryAttackInfo(&counter, &threshold, &lockout,
                                   &seconds_remaining)) {
    status.set_dictionary_attack_counter(counter);
    status.set_dictionary_attack_threshold(threshold);
    status.set_dictionary_attack_lockout_in_effect(lockout);
    status.set_dictionary_attack_lockout_seconds_remaining(seconds_remaining);
  }

  cryptohome::InstallAttributes install_attributes(tpm);
  install_attributes.Init(&tpm_init);
  status.set_install_lockbox_finalized(
      status.owned() &&
      !install_attributes.is_first_install() &&
      !install_attributes.is_invalid() &&
      install_attributes.is_initialized());

  cryptohome::Crypto crypto(&platform);
  crypto.set_use_tpm(true);
  crypto.Init(&tpm_init);
  cryptohome::Attestation attestation;
  attestation.Initialize(tpm, &tpm_init, &platform, &crypto,
                         &install_attributes,
                         brillo::SecureBlob(), /* abe_data */
                         true /* retain_endorsement_data */);
  status.set_attestation_prepared(attestation.IsPreparedForEnrollment());
  status.set_attestation_enrolled(attestation.IsEnrolled());
  status.set_verified_boot_measured(attestation.IsPCR0VerifiedMode());

  cryptohome::BootLockbox boot_lockbox(tpm, &platform, &crypto);
  status.set_boot_lockbox_finalized(boot_lockbox.IsFinalized());

  status.PrintDebugString();
  return 0;
}

int GetRandom(unsigned int random_bytes_count) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  brillo::SecureBlob random_bytes;
  tpm->GetRandomData(random_bytes_count, &random_bytes);
  if (random_bytes_count != random_bytes.size())
    return -1;

  std::string hex_bytes =
      base::HexEncode(random_bytes.data(), random_bytes.size());
  printf("%s\n", hex_bytes.c_str());
  return 0;
}

}  // namespace tpm_manager

}  // namespace cryptohome
