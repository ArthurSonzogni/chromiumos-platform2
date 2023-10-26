// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "base/files/file_path.h"

#include "libhwsec/backend/tpm2/backend.h"

namespace hwsec {

BackendTpm2::BackendTpm2(Proxy& proxy,
                         MiddlewareDerivative middleware_derivative,
                         const base::FilePath& pw_hash_tree_dir,
                         Metrics* metrics)
    : proxy_(proxy),
      tpm_manager_(proxy_.GetTpmManager()),
      tpm_nvram_(proxy_.GetTpmNvram()),
      crossystem_(proxy_.GetCrossystem()),
      platform_(proxy_.GetPlatform()),
      middleware_derivative_(middleware_derivative),
      context_(proxy_.GetTrunksCommandTransceiver(), proxy_.GetTrunksFactory()),
      state_(tpm_manager_),
      da_mitigation_(tpm_manager_),
      session_management_(context_),
      config_(context_, session_management_, crossystem_),
      storage_(config_, tpm_manager_, tpm_nvram_),
      key_management_(context_, config_, tpm_manager_, middleware_derivative_),
      sealing_(context_, config_, key_management_, session_management_),
      signature_sealing_(
          context_, config_, key_management_, session_management_),
      deriving_(context_, config_, key_management_),
      encryption_(context_, config_, key_management_),
      signing_(context_, config_, key_management_),
      random_(context_),
      pinweaver_(context_, config_),
      pinweaver_manager_(pinweaver_, pw_hash_tree_dir, metrics),
      recovery_crypto_(context_, config_, key_management_, session_management_),
      u2f_(context_),
      attestation_(context_, config_, key_management_, random_, signing_),
      ro_data_(context_, key_management_, signing_, tpm_nvram_),
      vendor_(context_, tpm_manager_, ro_data_),
      version_attestation_(context_, config_, key_management_, platform_) {}

BackendTpm2::~BackendTpm2() = default;

}  // namespace hwsec
