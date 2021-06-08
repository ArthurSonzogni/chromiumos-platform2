// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TRUNKS_CSME_PINWEAVER_PROVISION_IMPL_H_
#define TRUNKS_CSME_PINWEAVER_PROVISION_IMPL_H_

#include "trunks/csme/pinweaver_provision.h"

#include <string>

namespace trunks {
namespace csme {

// The implementation of `PinWeaverProvision`.
class PinWeaverProvisionImpl : public PinWeaverProvision {
 public:
  PinWeaverProvisionImpl() = default;
  ~PinWeaverProvisionImpl() override = default;
  bool Provision() override;
  bool InitOwner() override;

 private:
  bool ProvisionSaltingKeyHash(const std::string& public_key_hash);
  bool InitOwnerInternal();
};

}  // namespace csme
}  // namespace trunks

#endif  // TRUNKS_CSME_PINWEAVER_PROVISION_IMPL_H_
