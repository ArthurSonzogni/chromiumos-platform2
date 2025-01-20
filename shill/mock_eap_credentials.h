// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_EAP_CREDENTIALS_H_
#define SHILL_MOCK_EAP_CREDENTIALS_H_

#include <string>

#include <gmock/gmock.h>

#include "shill/eap_credentials.h"

namespace shill {

class MockEapCredentials : public EapCredentials {
 public:
  MockEapCredentials();
  MockEapCredentials(const MockEapCredentials&) = delete;
  MockEapCredentials& operator=(const MockEapCredentials&) = delete;

  ~MockEapCredentials() override;

  MOCK_METHOD(bool, IsConnectable, (), (const, override));
  MOCK_METHOD(void,
              Load,
              (const StoreInterface*, const std::string&),
              (override));
  MOCK_METHOD(void,
              OutputConnectionMetrics,
              (Metrics*, Technology),
              (const, override));
  MOCK_METHOD(void,
              PopulateSupplicantProperties,
              (CertificateFile*, KeyValueStore*, CaCertExperimentPhase),
              (const, override));
  MOCK_METHOD(void,
              Save,
              (StoreInterface*, const std::string&, bool),
              (const, override));
  MOCK_METHOD(void, Reset, (), (override));
  MOCK_METHOD(bool, SetKeyManagement, (const std::string&, Error*), (override));
  MOCK_METHOD(const std::string&, key_management, (), (const, override));
  MOCK_METHOD(const std::string&, pin, (), (const, override));

 private:
  std::string kDefaultKeyManagement;
};

}  // namespace shill

#endif  // SHILL_MOCK_EAP_CREDENTIALS_H_
