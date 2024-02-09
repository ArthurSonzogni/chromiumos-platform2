// Copyright 2014 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_MOCK_OMAHA_REQUEST_PARAMS_H_
#define UPDATE_ENGINE_CROS_MOCK_OMAHA_REQUEST_PARAMS_H_

#include <string>

#include <gmock/gmock.h>

#include "update_engine/cros/omaha_request_params.h"

namespace chromeos_update_engine {

class MockOmahaRequestParams : public OmahaRequestParams {
 public:
  MockOmahaRequestParams() : OmahaRequestParams() {
    // Delegate all calls to the parent instance by default. This helps the
    // migration from tests using the real RequestParams when they should have
    // use a fake or mock.
    ON_CALL(*this, GetAppId())
        .WillByDefault(
            testing::Invoke(this, &MockOmahaRequestParams::FakeGetAppId));
    ON_CALL(*this, SetTargetChannel(testing::_, testing::_, testing::_))
        .WillByDefault(testing::Invoke(
            this, &MockOmahaRequestParams::FakeSetTargetChannel));
    ON_CALL(*this, UpdateDownloadChannel())
        .WillByDefault(testing::Invoke(
            this, &MockOmahaRequestParams::FakeUpdateDownloadChannel));
    ON_CALL(*this, ShouldPowerwash())
        .WillByDefault(testing::Invoke(
            this, &MockOmahaRequestParams::FakeShouldPowerwash));
  }

  MOCK_CONST_METHOD0(GetAppId, std::string(void));
  MOCK_METHOD3(SetTargetChannel,
               bool(const std::string& channel,
                    bool is_powerwash_allowed,
                    std::string* error));
  MOCK_CONST_METHOD0(target_version_prefix, std::string(void));
  MOCK_METHOD0(UpdateDownloadChannel, void(void));
  MOCK_CONST_METHOD0(IsUpdateUrlOfficial, bool(void));
  MOCK_CONST_METHOD0(ShouldPowerwash, bool(void));

 private:
  // Wrappers to call the parent class and behave like the real object by
  // default. See "Delegating Calls to a Parent Class" in gmock's documentation.
  std::string FakeGetAppId() const { return OmahaRequestParams::GetAppId(); }

  bool FakeSetTargetChannel(const std::string& channel,
                            bool is_powerwash_allowed,
                            std::string* error) {
    return OmahaRequestParams::SetTargetChannel(channel, is_powerwash_allowed,
                                                error);
  }

  void FakeUpdateDownloadChannel() {
    return OmahaRequestParams::UpdateDownloadChannel();
  }

  bool FakeShouldPowerwash() const {
    return OmahaRequestParams::ShouldPowerwash();
  }
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_MOCK_OMAHA_REQUEST_PARAMS_H_
