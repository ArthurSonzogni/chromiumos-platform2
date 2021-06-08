// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "trunks/csme/pinweaver_provision_impl.h"

#include <memory>
#include <string>

#include <crypto/sha2.h>

#include "trunks/csme/mei_client.h"
#include "trunks/csme/mei_client_factory.h"
#include "trunks/csme/pinweaver_provision_client.h"
#include "trunks/error_codes.h"
#include "trunks/tpm_state.h"
#include "trunks/tpm_utility.h"
#include "trunks/trunks_factory_impl.h"

namespace trunks {
namespace csme {

bool PinWeaverProvisionImpl::Provision() {
  trunks::TrunksFactoryImpl factory;
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";
  std::unique_ptr<trunks::TpmUtility> tpm_utility = factory.GetTpmUtility();

  // Persists the salting key in case it's not done yet.
  trunks::TPM_RC result = tpm_utility->PrepareForPinWeaver();
  if (result) {
    LOG(ERROR) << ": Failed to prepare for pinweaver: "
               << trunks::GetErrorString(result);
    return false;
  }

  trunks::TPMT_PUBLIC public_area;
  result = tpm_utility->GetKeyPublicArea(trunks::kCsmeSaltingKey, &public_area);
  if (result) {
    LOG(ERROR) << ": Failed to get public key info: "
               << trunks::GetErrorString(result);
    return false;
  }
  if (public_area.type != trunks::TPM_ALG_ECC) {
    LOG(ERROR) << "Unexpected key type (should be trunks::TPM_ALG_ECC): "
               << public_area.type;
    return false;
  }
  const std::string public_key =
      StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.x) +
      StringFrom_TPM2B_ECC_PARAMETER(public_area.unique.ecc.y);
  const std::string public_key_hash = crypto::SHA256HashString(public_key);
  if (!ProvisionSaltingKeyHash(public_key_hash)) {
    LOG(ERROR) << "Failed to provision pinweaver-scme salting key.";
    return false;
  }
  return true;
}

bool PinWeaverProvisionImpl::ProvisionSaltingKeyHash(
    const std::string& public_key_hash) {
  MeiClientFactory mei_client_factory;
  PinWeaverProvisionClient client(&mei_client_factory);

  bool committed = false;
  std::string salting_key_hash;
  if (client.GetSaltingKeyHash(&salting_key_hash, &committed) && committed) {
    if (salting_key_hash != public_key_hash) {
      LOG(ERROR) << "Provisioned salting key hash mismatched.";
      return false;
    }
    LOG(INFO) << "Already provisioned.";
    return true;
  }

  LOG(INFO) << "Not provisioned yet; start provisioning.";
  if (!client.SetSaltingKeyHash(public_key_hash)) {
    LOG(ERROR) << "Failed to set key hash.";
    return false;
  }
  if (!client.CommitSaltingKeyHash()) {
    LOG(ERROR) << "Failed to commit salting key hash.";
    return false;
  }
  return true;
}

bool PinWeaverProvisionImpl::InitOwner() {
  trunks::TrunksFactoryImpl factory;
  CHECK(factory.Initialize()) << "Failed to initialize trunks factory.";
  std::unique_ptr<trunks::TpmState> tpm_state(factory.GetTpmState());
  trunks::TPM_RC result = tpm_state->Initialize();
  if (result) {
    LOG(ERROR) << "Failed to initialize `TpmState`.";
    return false;
  }
  if (tpm_state->IsOwnerPasswordSet()) {
    LOG(ERROR) << "Init owner requites empty owner password.";
    return false;
  }
  return InitOwnerInternal();
}

bool PinWeaverProvisionImpl::InitOwnerInternal() {
  MeiClientFactory mei_client_factory;
  PinWeaverProvisionClient client(&mei_client_factory);
  if (!client.InitOwner()) {
    LOG(ERROR) << "Failed to init owner.";
    return false;
  }
  return true;
}

}  // namespace csme
}  // namespace trunks
