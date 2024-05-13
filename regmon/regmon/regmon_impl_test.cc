// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "regmon/regmon/regmon_impl.h"

#include <utility>

#include <base/test/test_future.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <regmon/proto_bindings/regmon_service.pb.h>

#include "regmon/metrics/metrics_reporter.h"

namespace regmon {

using ::brillo::dbus_utils::MockDBusMethodResponse;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::StrEq;

class MockMetricsReporter : public metrics::MetricsReporter {
 public:
  MockMetricsReporter() = default;
  ~MockMetricsReporter() override = default;
  MOCK_METHOD(bool, ReportAnnotationViolation, (int unique_id), (override));
};

class RegmonImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto metrics_reporter = std::make_unique<MockMetricsReporter>();
    metrics_reporter_ = metrics_reporter.get();
    regmon_impl_ = std::make_unique<RegmonImpl>(std::move(metrics_reporter));
  }

  MockMetricsReporter* metrics_reporter_;
  std::unique_ptr<RegmonImpl> regmon_impl_;
};

TEST_F(RegmonImplTest, ReportAnnotationViolationNotCalledOnEmptyRequest) {
  RecordPolicyViolationRequest input;
  auto response =
      std::make_unique<MockDBusMethodResponse<RecordPolicyViolationResponse>>();
  EXPECT_CALL(*metrics_reporter_, ReportAnnotationViolation(_)).Times(0);

  regmon_impl_->RecordPolicyViolation(input, std::move(response));
}

TEST_F(RegmonImplTest, MissingPolicyErrorMessageOnEmptyRequest) {
  RecordPolicyViolationRequest request;
  auto response =
      std::make_unique<MockDBusMethodResponse<RecordPolicyViolationResponse>>();
  base::test::TestFuture<const RecordPolicyViolationResponse&> future;
  response->set_return_callback(future.GetCallback());
  EXPECT_CALL(*metrics_reporter_, ReportAnnotationViolation(_)).Times(0);

  regmon_impl_->RecordPolicyViolation(request, std::move(response));

  const RecordPolicyViolationResponse& result = future.Get();
  EXPECT_THAT(result.status().error_message(),
              StrEq("No policy found. Please set a policy value."));
}

TEST_F(RegmonImplTest, MissingAnnotationHashMessageOnEmptyAnnotationHash) {
  RecordPolicyViolationRequest request;
  PolicyViolation violation;
  violation.set_policy(PolicyViolation::CALENDAR_INTEGRATION_ENABLED);
  *request.mutable_violation() = violation;
  auto response =
      std::make_unique<MockDBusMethodResponse<RecordPolicyViolationResponse>>();
  base::test::TestFuture<const RecordPolicyViolationResponse&> future;
  response->set_return_callback(future.GetCallback());
  EXPECT_CALL(*metrics_reporter_, ReportAnnotationViolation(_)).Times(0);

  regmon_impl_->RecordPolicyViolation(request, std::move(response));

  const RecordPolicyViolationResponse& result = future.Get();
  EXPECT_THAT(
      result.status().error_message(),
      StrEq("No annotation hash found. Please set an annotation hash."));
}

TEST_F(RegmonImplTest, EmptyErrorMessageOnValidViolationRequest) {
  RecordPolicyViolationRequest request;
  const int kCalendarGetEventsAnnotationHash = 86429515;
  PolicyViolation violation;
  violation.set_policy(PolicyViolation::CALENDAR_INTEGRATION_ENABLED);
  violation.set_annotation_hash(kCalendarGetEventsAnnotationHash);
  *request.mutable_violation() = violation;
  auto response =
      std::make_unique<MockDBusMethodResponse<RecordPolicyViolationResponse>>();
  base::test::TestFuture<const RecordPolicyViolationResponse&> future;
  response->set_return_callback(future.GetCallback());
  EXPECT_CALL(*metrics_reporter_,
              ReportAnnotationViolation(kCalendarGetEventsAnnotationHash))
      .Times(1);

  regmon_impl_->RecordPolicyViolation(request, std::move(response));

  const RecordPolicyViolationResponse& result = future.Get();
  EXPECT_THAT(result.status().error_message(), IsEmpty());
}

}  // namespace regmon
