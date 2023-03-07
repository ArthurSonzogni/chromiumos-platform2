// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_PROXY_PROXY_FOR_TEST_H_
#define LIBHWSEC_PROXY_PROXY_FOR_TEST_H_

#include <memory>

#include <gmock/gmock.h>

#include "libhwsec/proxy/proxy.h"

#ifndef BUILD_LIBHWSEC
#error "Don't include this file outside libhwsec!"
#endif

// Forward declarations for mocks
#if USE_TPM1
namespace hwsec::overalls {
class MockOveralls;
}  // namespace hwsec::overalls
#endif  // USE_TPM1

#if USE_TPM2
namespace trunks {
class MockCommandTransceiver;
class MockTpm;
class MockTpmCache;
class MockTpmState;
class MockTpmUtility;
class MockAuthorizationDelegate;
class MockHmacSession;
class MockPolicySession;
class MockBlobParser;
class TrunksFactoryForTest;
}  // namespace trunks
#endif  // USE_TPM2

namespace org::chromium {
class TpmManagerProxyMock;
class TpmNvramProxyMock;
}  // namespace org::chromium

namespace hwsec {

// A proxy implementation for testing. Custom instances can be injected. If no
// instance has been injected, a default mock instance will be used. Objects for
// which ownership is passed to the caller are instantiated as forwarders which
// simply forward calls to the current instance set for the class.
//
// Example usage:
//   ProxyForTest proxy;
//   org::chromium::TpmManagerProxyMock mock_tpm_manager;
//   proxy.SetTpmManager(mock_tpm_manager);
//   // Set expectations on mock_tpm_manager...

struct MockProxyData {
#if USE_TPM1
  hwsec::overalls::MockOveralls& overalls;
#endif

#if USE_TPM2
  trunks::MockCommandTransceiver& trunks_command_transceiver;
  trunks::MockTpm& tpm;
  trunks::MockTpmCache& tpm_cache;
  trunks::MockTpmState& tpm_state;
  trunks::MockTpmUtility& tpm_utility;
  trunks::MockAuthorizationDelegate& authorization_delegate;
  trunks::MockHmacSession& hmac_session;
  trunks::MockPolicySession& policy_session;
  trunks::MockPolicySession& trial_session;
  trunks::MockBlobParser& blob_parser;
#endif

  org::chromium::TpmManagerProxyMock& tpm_manager;
  org::chromium::TpmNvramProxyMock& tpm_nvram;
};

class ProxyForTest : public Proxy {
 public:
  ProxyForTest();
  ~ProxyForTest() override;

  MockProxyData& GetMock() { return mock_proxy_data_; }

 private:
  // The InnerData implementation is in the cpp file.
  struct InnerData;

  std::unique_ptr<InnerData> inner_data_;
  MockProxyData mock_proxy_data_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_PROXY_PROXY_FOR_TEST_H_
