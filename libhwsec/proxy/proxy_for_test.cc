// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/proxy/proxy_for_test.h"

#include <memory>

#include <gmock/gmock.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client-test/tpm_manager/dbus-proxy-mocks.h>

#if USE_TPM2

// Prevent the conflict definition from tss.h
#pragma push_macro("TPM_ALG_RSA")
#undef TPM_ALG_RSA
#pragma push_macro("TPM_ALG_SHA")
#undef TPM_ALG_SHA
#pragma push_macro("TPM_ALG_HMAC")
#undef TPM_ALG_HMAC
#pragma push_macro("TPM_ALG_AES")
#undef TPM_ALG_AES
#pragma push_macro("TPM_ALG_MGF1")
#undef TPM_ALG_MGF1
#pragma push_macro("TPM_ALG_XOR")
#undef TPM_ALG_XOR

#include <trunks/mock_authorization_delegate.h>
#include <trunks/mock_blob_parser.h>
#include <trunks/mock_command_transceiver.h>
#include <trunks/mock_hmac_session.h>
#include <trunks/mock_policy_session.h>
#include <trunks/mock_tpm.h>
#include <trunks/mock_tpm_cache.h>
#include <trunks/mock_tpm_state.h>
#include <trunks/mock_tpm_utility.h>
#include <trunks/trunks_factory_for_test.h>

// Restore the definitions
#pragma pop_macro("TPM_ALG_RSA")
#pragma pop_macro("TPM_ALG_SHA")
#pragma pop_macro("TPM_ALG_HMAC")
#pragma pop_macro("TPM_ALG_AES")
#pragma pop_macro("TPM_ALG_MGF1")
#pragma pop_macro("TPM_ALG_XOR")

#endif

#if USE_TPM1
#include "libhwsec/overalls/mock_overalls.h"
#endif

#include "libhwsec/proxy/proxy.h"

namespace hwsec {

struct ProxyForTest::InnerData {
#if USE_TPM1
  testing::NiceMock<hwsec::overalls::MockOveralls> overalls;
#endif  // USE_TPM1
#if USE_TPM2
  testing::NiceMock<trunks::MockCommandTransceiver> trunks_command_transceiver;
  testing::NiceMock<trunks::MockTpm> tpm;
  testing::NiceMock<trunks::MockTpmCache> tpm_cache;
  testing::NiceMock<trunks::MockTpmState> tpm_state;
  testing::NiceMock<trunks::MockTpmUtility> tpm_utility;
  testing::NiceMock<trunks::MockAuthorizationDelegate> authorization_delegate;
  testing::NiceMock<trunks::MockHmacSession> hmac_session;
  testing::NiceMock<trunks::MockPolicySession> policy_session;
  testing::NiceMock<trunks::MockPolicySession> trial_session;
  testing::NiceMock<trunks::MockBlobParser> blob_parser;
  trunks::TrunksFactoryForTest trunks_factory;
#endif  // USE_TPM2
  testing::NiceMock<org::chromium::TpmManagerProxyMock> tpm_manager;
  testing::NiceMock<org::chromium::TpmNvramProxyMock> tpm_nvram;
};

ProxyForTest::ProxyForTest()
    : inner_data_(std::make_unique<ProxyForTest::InnerData>()),
      mock_proxy_data_(MockProxyData {
#if USE_TPM1
        .overalls = inner_data_->overalls,
#endif  // USE_TPM1
#if USE_TPM2
        .trunks_command_transceiver = inner_data_->trunks_command_transceiver,
        .tpm = inner_data_->tpm, .tpm_cache = inner_data_->tpm_cache,
        .tpm_state = inner_data_->tpm_state,
        .tpm_utility = inner_data_->tpm_utility,
        .authorization_delegate = inner_data_->authorization_delegate,
        .hmac_session = inner_data_->hmac_session,
        .policy_session = inner_data_->policy_session,
        .trial_session = inner_data_->trial_session,
        .blob_parser = inner_data_->blob_parser,
#endif  // USE_TPM2
        .tpm_manager = inner_data_->tpm_manager,
        .tpm_nvram = inner_data_->tpm_nvram,
      }) {
#if USE_TPM1
  SetOveralls(&mock_proxy_data_.overalls);
#endif
#if USE_TPM2
  trunks::TrunksFactoryForTest& factory = inner_data_->trunks_factory;
  factory.set_tpm(&mock_proxy_data_.tpm);
  factory.set_tpm_cache(&mock_proxy_data_.tpm_cache);
  factory.set_tpm_state(&mock_proxy_data_.tpm_state);
  factory.set_tpm_utility(&mock_proxy_data_.tpm_utility);
  factory.set_password_authorization_delegate(
      &mock_proxy_data_.authorization_delegate);
  factory.set_hmac_session(&mock_proxy_data_.hmac_session);
  factory.set_policy_session(&mock_proxy_data_.policy_session);
  factory.set_trial_session(&mock_proxy_data_.trial_session);
  factory.set_blob_parser(&mock_proxy_data_.blob_parser);
  SetTrunksCommandTransceiver(&mock_proxy_data_.trunks_command_transceiver);
  SetTrunksFactory(&inner_data_->trunks_factory);
#endif
  SetTpmManager(&mock_proxy_data_.tpm_manager);
  SetTpmNvram(&mock_proxy_data_.tpm_nvram);
}

ProxyForTest::~ProxyForTest() = default;

}  // namespace hwsec
