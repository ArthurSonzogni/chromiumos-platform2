// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/metrics_sender.h"

#include <memory>

#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "metrics/metrics_library_mock.h"

namespace secagentd::testing {

using ::testing::_;
using ::testing::Return;

class MetricsSenderTestFixture : public ::testing::Test {
 protected:
  MetricsSenderTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {
    auto metrics_library_mock = std::make_unique<MetricsLibraryMock>();
    metrics_library_mock_ = metrics_library_mock.get();
    metrics_sender_ =
        MetricsSender::CreateForTesting(std::move(metrics_library_mock));
  }

  void TearDown() override { task_environment_.RunUntilIdle(); }

  base::test::TaskEnvironment task_environment_;
  MetricsLibraryMock* metrics_library_mock_;
  std::unique_ptr<MetricsSender> metrics_sender_;

  int GetMaxMapValue(std::string name) const {
    return metrics_sender_->exclusive_max_map_.find(name)->second;
  }

  int GetSuccessValue(std::string name) const {
    return metrics_sender_->success_value_map_.find(name)->second;
  }

  int GetBatchEnumMapSize() { return metrics_sender_->batch_enum_map_.size(); }
  int GetNBatchCountHistogram() {
    return metrics_sender_->batch_count_map_.size();
  }
  int GetBatchHistogramNBucket(metrics::CountMetric m) {
    return metrics_sender_->batch_count_map_[m].size();
  }

  void Flush() { metrics_sender_->Flush(); }
};

TEST_F(MetricsSenderTestFixture, SendEnumMetricToUMA) {
  enum class TestEnum {
    kZero,
    kOne,
    kTwo,
    kMaxValue = kTwo,
  };
  const metrics::EnumMetric<TestEnum> kTestMetric = {.name = "TestMetric"};
  EXPECT_CALL(*metrics_library_mock_,
              SendEnumToUMA("ChromeOS.Secagentd.TestMetric",
                            static_cast<int>(TestEnum::kOne),
                            static_cast<int>(TestEnum::kMaxValue) + 1))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      metrics_sender_->SendEnumMetricToUMA(kTestMetric, TestEnum::kOne));
}

TEST_F(MetricsSenderTestFixture, CheckExclusiveMaxMap) {
  EXPECT_EQ(17, GetMaxMapValue("SendMessageResult"));
  EXPECT_EQ(3, GetMaxMapValue("Cache"));
  EXPECT_EQ(5, GetMaxMapValue("Process.ExecEvent"));
  EXPECT_EQ(5, GetMaxMapValue("Process.TerminateEvent"));
}

TEST_F(MetricsSenderTestFixture, CheckSuccessValueMap) {
  EXPECT_EQ(0, GetSuccessValue("SendMessageResult"));
  EXPECT_EQ(0, GetSuccessValue("Cache"));
  EXPECT_EQ(0, GetSuccessValue("Process.ExecEvent"));
  EXPECT_EQ(0, GetSuccessValue("Process.TerminateEvent"));
}

TEST_F(MetricsSenderTestFixture, SendBatchedEnumMetricsToUMA) {
  metrics_sender_->InitBatchedMetrics();

  static constexpr int kMetricCount1{201};
  static constexpr int kMetricCount2{50};

  for (int i = 0; i < kMetricCount1; i++) {
    metrics_sender_->IncrementBatchedMetric(
        metrics::kExecEvent, metrics::ProcessEvent::kSpawnPidNotInCache);
  }
  for (int i = 0; i < kMetricCount2; i++) {
    metrics_sender_->IncrementBatchedMetric(
        metrics::kTerminateEvent, metrics::ProcessEvent::kParentStillAlive);
  }
  EXPECT_CALL(*metrics_library_mock_,
              SendRepeatedEnumToUMA("ChromeOS.Secagentd.Process.ExecEvent", 1,
                                    5, kMetricCount1))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics_library_mock_,
              SendRepeatedEnumToUMA("ChromeOS.Secagentd.Process.TerminateEvent",
                                    4, 5, kMetricCount2))
      .WillOnce(Return(true));
  EXPECT_EQ(2, GetBatchEnumMapSize());

  task_environment_.FastForwardBy(base::Seconds(metrics::kBatchTimer));
  EXPECT_EQ(0, GetBatchEnumMapSize());

  for (int i = 0; i < kMetricCount1; i++) {
    metrics_sender_->IncrementBatchedMetric(metrics::kSendMessage,
                                            metrics::SendMessage::kSuccess);
  }
  // Success value (0) should be divided by 100 and rounded up.
  static constexpr int kRounded{(kMetricCount1 + 100 - 1) / 100};
  EXPECT_CALL(*metrics_library_mock_,
              SendRepeatedEnumToUMA("ChromeOS.Secagentd.SendMessageResult", 0,
                                    17, kRounded))
      .WillOnce(Return(true));
  EXPECT_EQ(1, GetBatchEnumMapSize());

  task_environment_.FastForwardBy(base::Seconds(metrics::kBatchTimer));
  EXPECT_EQ(0, GetBatchEnumMapSize());
}

TEST_F(MetricsSenderTestFixture, SendBatchedCountMetricsToUMA) {
  metrics_sender_->InitBatchedMetrics();

  static constexpr int kMetricCount1{201};
  static constexpr int kMetricSample1{527};
  static constexpr int kMetricSample1Bucketized{
      512};  // quantized to nbuckets and rounded down.

  static constexpr int kMetricCount2{50};
  static constexpr int kMetricSample2{734};
  static constexpr int kMetricSample2Bucketized{
      704};  // quantized to nbuckets and rounded down.

  for (int i = 0; i < kMetricCount1; i++) {
    metrics_sender_->IncrementCountMetric(metrics::kCommandLineSize,
                                          kMetricSample1);
  }
  for (int i = 0; i < kMetricCount2; i++) {
    metrics_sender_->IncrementCountMetric(metrics::kCommandLineSize,
                                          kMetricSample2);
  }

  EXPECT_CALL(
      *metrics_library_mock_,
      SendRepeatedToUMA("ChromeOS.Secagentd.CommandLineLength",
                        kMetricSample1Bucketized, metrics::kCommandLineSize.min,
                        metrics::kCommandLineSize.max,
                        metrics::kCommandLineSize.nbuckets, kMetricCount1))
      .WillOnce(Return(true));

  EXPECT_CALL(
      *metrics_library_mock_,
      SendRepeatedToUMA("ChromeOS.Secagentd.CommandLineLength",
                        kMetricSample2Bucketized, metrics::kCommandLineSize.min,
                        metrics::kCommandLineSize.max,
                        metrics::kCommandLineSize.nbuckets, kMetricCount2))
      .WillOnce(Return(true));
  EXPECT_EQ(1, GetNBatchCountHistogram());
  EXPECT_EQ(2, GetBatchHistogramNBucket(metrics::kCommandLineSize));

  task_environment_.FastForwardBy(base::Seconds(metrics::kBatchTimer));
  EXPECT_EQ(0, GetNBatchCountHistogram());
}

TEST_F(MetricsSenderTestFixture, RunRegisteredCallbacks) {
  base::test::TestFuture<void> future_1;
  metrics_sender_->RegisterMetricOnFlushCallback(
      future_1.GetRepeatingCallback());

  base::test::TestFuture<void> future_2;
  metrics_sender_->RegisterMetricOnFlushCallback(
      future_2.GetRepeatingCallback());

  metrics_sender_->InitBatchedMetrics();
  task_environment_.FastForwardBy(base::Seconds(metrics::kBatchTimer));

  EXPECT_TRUE(future_1.Wait());
  EXPECT_TRUE(future_2.Wait());
}

TEST_F(MetricsSenderTestFixture, EarlyFlushSaturatedMetric) {
  EXPECT_CALL(*metrics_library_mock_,
              SendRepeatedEnumToUMA("ChromeOS.Secagentd.Process.ExecEvent", 1,
                                    5, metrics::kMaxMapValue))
      .WillOnce(Return(true));

  for (int i = 0; i < metrics::kMaxMapValue; i++) {
    metrics_sender_->IncrementBatchedMetric(
        metrics::kExecEvent, metrics::ProcessEvent::kSpawnPidNotInCache);
  }
  EXPECT_EQ(0, GetBatchEnumMapSize());

  for (int i = 0; i < metrics::kMaxMapValue; i++) {
    // Send in success value.
    metrics_sender_->IncrementBatchedMetric(metrics::kExecEvent,
                                            metrics::ProcessEvent::kFullEvent);
  }
  EXPECT_EQ(1, GetBatchEnumMapSize());
}

}  // namespace secagentd::testing
