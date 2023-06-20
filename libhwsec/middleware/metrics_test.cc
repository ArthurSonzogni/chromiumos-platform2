// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/middleware/metrics.h"

#include <type_traits>

#include <base/base64.h>
#include <crypto/sha2.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libhwsec-foundation/error/testing_helper.h>
#include <metrics/metrics_library_mock.h>

#include "libhwsec/backend/state.h"
#include "libhwsec/error/tpm_retry_action.h"
#include "libhwsec/middleware/function_name.h"

using ::hwsec_foundation::status::MakeStatus;
using ::testing::Return;

namespace hwsec {

class MetricsTest : public ::testing::Test {
 public:
  MetricsTest() = default;

 protected:
  MetricsLibraryMock mock_metrics_;
  Metrics metrics_{&mock_metrics_};
};

TEST_F(MetricsTest, SimplifyFuncName) {
  int later_int = static_cast<int>(TPMRetryAction::kLater);
  int exclusive_max = static_cast<int>(TPMRetryAction::kMaxValue) + 1;

  EXPECT_CALL(mock_metrics_,
              SendEnumToUMA("Platform.Libhwsec.RetryAction.State.IsReady",
                            later_int, exclusive_max))
      .WillOnce(Return(true));

  EXPECT_CALL(mock_metrics_,
              SendEnumToUMA("Platform.Libhwsec.RetryAction.State", later_int,
                            exclusive_max))
      .WillOnce(Return(true));

  EXPECT_TRUE(metrics_.SendFuncResultToUMA(
      SimplifyFuncName(GetFuncName<&State::IsReady>()),
      MakeStatus<TPMError>("Test error", TPMRetryAction::kLater)));
}

}  // namespace hwsec
