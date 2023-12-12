// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_SUPPLICANT_MOCK_SUPPLICANT_PEER_PROXY_H_
#define SHILL_SUPPLICANT_MOCK_SUPPLICANT_PEER_PROXY_H_

#include <gmock/gmock.h>

#include "shill/store/key_value_store.h"
#include "shill/supplicant/supplicant_peer_proxy_interface.h"

namespace shill {

class MockSupplicantPeerProxy : public SupplicantPeerProxyInterface {
 public:
  MockSupplicantPeerProxy();
  MockSupplicantPeerProxy(const MockSupplicantPeerProxy&) = delete;
  MockSupplicantPeerProxy& operator=(const MockSupplicantPeerProxy&) = delete;

  ~MockSupplicantPeerProxy() override;

  MOCK_METHOD(bool, GetProperties, (KeyValueStore*), (override));
};

}  // namespace shill

#endif  // SHILL_SUPPLICANT_MOCK_SUPPLICANT_PEER_PROXY_H_
