// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_BACKEND_H_
#define LIBHWSEC_BACKEND_MOCK_BACKEND_H_

#include <gmock/gmock.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/mock_config.h"
#include "libhwsec/backend/mock_da_mitigation.h"
#include "libhwsec/backend/mock_deriving.h"
#include "libhwsec/backend/mock_encryption.h"
#include "libhwsec/backend/mock_key_management.h"
#include "libhwsec/backend/mock_pinweaver.h"
#include "libhwsec/backend/mock_random.h"
#include "libhwsec/backend/mock_recovery_crypto.h"
#include "libhwsec/backend/mock_ro_data.h"
#include "libhwsec/backend/mock_sealing.h"
#include "libhwsec/backend/mock_session_management.h"
#include "libhwsec/backend/mock_signature_sealing.h"
#include "libhwsec/backend/mock_signing.h"
#include "libhwsec/backend/mock_state.h"
#include "libhwsec/backend/mock_storage.h"
#include "libhwsec/backend/mock_vendor.h"

namespace hwsec {

class MockBackend : public Backend {
 public:
  struct MockBackendData {
    MockState state;
    MockDAMitigation da_mitigation;
    MockStorage storage;
    MockRoData ro_data;
    MockSealing sealing;
    MockSignatureSealing signature_sealing;
    MockDeriving deriving;
    MockEncryption encryption;
    MockSigning signing;
    MockKeyManagement key_management;
    MockSessionManagement session_management;
    MockConfig config;
    MockRandom random;
    MockPinWeaver pinweaver;
    MockVendor vendor;
    MockRecoveryCrypto recovery_crypto;
  };

  MockBackend() = default;
  virtual ~MockBackend() = default;

  MockBackendData& GetMock() { return mock_data_; }

 private:
  State* GetState() override { return &mock_data_.state; }
  DAMitigation* GetDAMitigation() override { return &mock_data_.da_mitigation; }
  Storage* GetStorage() override { return &mock_data_.storage; }
  RoData* GetRoData() override { return &mock_data_.ro_data; }
  Sealing* GetSealing() override { return &mock_data_.sealing; }
  SignatureSealing* GetSignatureSealing() override {
    return &mock_data_.signature_sealing;
  }
  Deriving* GetDeriving() override { return &mock_data_.deriving; }
  Encryption* GetEncryption() override { return &mock_data_.encryption; }
  Signing* GetSigning() override { return &mock_data_.signing; }
  KeyManagement* GetKeyManagement() override {
    return &mock_data_.key_management;
  }
  SessionManagement* GetSessionManagement() override {
    return &mock_data_.session_management;
  }
  Config* GetConfig() override { return &mock_data_.config; }
  Random* GetRandom() override { return &mock_data_.random; }
  PinWeaver* GetPinWeaver() override { return &mock_data_.pinweaver; }
  Vendor* GetVendor() override { return &mock_data_.vendor; }
  RecoveryCrypto* GetRecoveryCrypto() override {
    return &mock_data_.recovery_crypto;
  }

  MockBackendData mock_data_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_BACKEND_H_
