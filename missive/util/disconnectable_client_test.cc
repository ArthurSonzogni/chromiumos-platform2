// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/util/disconnectable_client.h"

#include <memory>
#include <utility>

#include <base/task/sequenced_task_runner.h>
#include <base/test/scoped_feature_list.h>
#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <base/types/expected.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "missive/util/status.h"
#include "missive/util/status_macros.h"
#include "missive/util/statusor.h"
#include "missive/util/test_support_callbacks.h"

using ::testing::Eq;

namespace reporting {

class MockDelegate : public DisconnectableClient::Delegate {
 public:
  MockDelegate(int64_t input,
               base::TimeDelta delay,
               base::OnceCallback<void(StatusOr<int64_t>)> completion_cb)
      : input_(input),
        delay_(delay),
        completion_cb_(std::move(completion_cb)) {}
  MockDelegate(const MockDelegate& other) = delete;
  MockDelegate& operator=(const MockDelegate& other) = delete;
  ~MockDelegate() override = default;

  void DoCall(base::OnceClosure cb) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, std::move(cb), delay_);
  }

  void Respond(Status status) override {
    CHECK(completion_cb_);
    if (!status.ok()) {
      std::move(completion_cb_).Run(base::unexpected(std::move(status)));
      return;
    }
    std::move(completion_cb_).Run(input_ * 2);
  }

 private:
  const int64_t input_;
  const base::TimeDelta delay_;
  base::OnceCallback<void(StatusOr<int64_t>)> completion_cb_;
};

class FailDelegate : public DisconnectableClient::Delegate {
 public:
  FailDelegate(base::TimeDelta delay,
               base::OnceCallback<void(StatusOr<int64_t>)> completion_cb)
      : delay_(delay), completion_cb_(std::move(completion_cb)) {}
  FailDelegate(const FailDelegate& other) = delete;
  FailDelegate& operator=(const FailDelegate& other) = delete;
  ~FailDelegate() override = default;

  void DoCall(base::OnceClosure cb) override {
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, std::move(cb), delay_);
  }

  void Respond(Status status) override {
    CHECK(completion_cb_);
    if (!status.ok()) {
      std::move(completion_cb_).Run(base::unexpected(std::move(status)));
      return;
    }
    std::move(completion_cb_)
        .Run(base::unexpected(Status(error::CANCELLED, "Failed in test")));
  }

 private:
  const base::TimeDelta delay_;
  base::OnceCallback<void(StatusOr<int64_t>)> completion_cb_;
};

class DisconnectableClientTest : public ::testing::Test {
 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  DisconnectableClient client_{base::SequencedTaskRunner::GetCurrentDefault()};
};

TEST_F(DisconnectableClientTest, NormalConnection) {
  client_.SetAvailability(/*is_available=*/true);

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::TimeDelta(), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::TimeDelta(), res2.cb()));

  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));
  result = res2.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(444));
}

TEST_F(DisconnectableClientTest, NoConnection) {
  test::TestEvent<StatusOr<int64_t>> res;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::TimeDelta(), res.cb()));

  auto result = res.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();
}

TEST_F(DisconnectableClientTest, FailedCallOnNormalConnection) {
  client_.SetAvailability(/*is_available=*/true);

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  test::TestEvent<StatusOr<int64_t>> res3;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(1), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<FailDelegate>(base::Seconds(2), res2.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(3), res3.cb()));

  task_environment_.FastForwardBy(base::Seconds(1));

  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));

  task_environment_.FastForwardBy(base::Seconds(1));

  result = res2.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::CANCELLED))
      << result.error();

  task_environment_.FastForwardBy(base::Seconds(1));

  result = res3.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(444));
}

TEST_F(DisconnectableClientTest, DroppedConnection) {
  client_.SetAvailability(/*is_available=*/true);

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(1), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(2), res2.cb()));

  task_environment_.FastForwardBy(base::Seconds(1));

  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));

  client_.SetAvailability(/*is_available=*/false);

  result = res2.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();
}

TEST_F(DisconnectableClientTest, FailedCallOnDroppedConnection) {
  client_.SetAvailability(/*is_available=*/true);

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  test::TestEvent<StatusOr<int64_t>> res3;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(1), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<FailDelegate>(base::Seconds(2), res2.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(3), res3.cb()));

  task_environment_.FastForwardBy(base::Seconds(1));

  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));

  client_.SetAvailability(/*is_available=*/false);

  task_environment_.FastForwardBy(base::Seconds(1));

  result = res2.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();

  result = res3.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();
}

TEST_F(DisconnectableClientTest, ConnectionDroppedThenRestored) {
  client_.SetAvailability(/*is_available=*/true);

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  test::TestEvent<StatusOr<int64_t>> res3;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(1), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(2), res2.cb()));

  task_environment_.FastForwardBy(base::Seconds(1));

  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));

  client_.SetAvailability(/*is_available=*/false);

  task_environment_.FastForwardBy(base::Seconds(1));

  result = res2.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();

  client_.SetAvailability(/*is_available=*/true);

  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(333, base::Seconds(1), res3.cb()));

  task_environment_.FastForwardBy(base::Seconds(1));

  result = res3.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(666));
}

TEST_F(DisconnectableClientTest, NormalConnectionBeyondLimit) {
  client_.SetAvailability(/*is_available=*/true);

  // Set jobs limit to 2 for this test.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitFromCommandLine(
      "EnableReportingDelegateJobsLimit<study:max_running/2", "");

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  test::TestEvent<StatusOr<int64_t>> res3;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(10), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(10), res2.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(333, base::Seconds(5), res3.cb()));

  // No result right after launch.
  EXPECT_TRUE(res1.no_result());
  EXPECT_TRUE(res2.no_result());
  EXPECT_TRUE(res3.no_result());

  // Because of the limit, only the first two delegates would be finished,
  // even though the third one takes less time.
  task_environment_.FastForwardBy(base::Seconds(10));
  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));
  result = res2.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(444));
  EXPECT_TRUE(res3.no_result());

  // Afterward the third delegate can finish.
  task_environment_.FastForwardBy(base::Seconds(5));
  result = res3.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(666));
}

TEST_F(DisconnectableClientTest, ConnectionGoingDownBeyondLimit) {
  client_.SetAvailability(/*is_available=*/true);

  // Set jobs limit to 2 for this test.
  base::test::ScopedFeatureList scoped_feature_list;
  scoped_feature_list.InitFromCommandLine(
      "EnableReportingDelegateJobsLimit<study:max_running/2", "");

  test::TestEvent<StatusOr<int64_t>> res1;
  test::TestEvent<StatusOr<int64_t>> res2;
  test::TestEvent<StatusOr<int64_t>> res3;
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(111, base::Seconds(10), res1.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(222, base::Seconds(10), res2.cb()));
  client_.MaybeMakeCall(
      std::make_unique<MockDelegate>(333, base::Seconds(5), res3.cb()));

  // No result right after launch.
  EXPECT_TRUE(res1.no_result());
  EXPECT_TRUE(res2.no_result());
  EXPECT_TRUE(res3.no_result());

  // Because of the limit, only the first two delegates would be finished,
  // even though the third one takes less time.
  task_environment_.FastForwardBy(base::Seconds(10));
  auto result = res1.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(222));
  result = res2.result();
  ASSERT_OK(result) << result.error();
  EXPECT_THAT(result.value(), Eq(444));
  EXPECT_TRUE(res3.no_result());

  // Afterward the third delegate can start, but connection is down.
  client_.SetAvailability(/*is_available=*/false);
  task_environment_.FastForwardBy(base::Seconds(5));
  result = res3.result();
  ASSERT_FALSE(result.has_value());
  ASSERT_THAT(result.error().error_code(), Eq(error::UNAVAILABLE))
      << result.error();
}
}  // namespace reporting
