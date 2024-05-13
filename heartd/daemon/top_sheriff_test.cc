// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/daemon/top_sheriff.h"

#include <base/test/task_environment.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "heartd/daemon/sheriffs/sheriff.h"

namespace heartd {

namespace {

class TestSheriff final : public Sheriff {
 public:
  explicit TestSheriff(bool has_shift_work) : has_shift_work_(has_shift_work) {}
  TestSheriff(const TestSheriff&) = delete;
  TestSheriff& operator=(const TestSheriff&) = delete;
  ~TestSheriff() override{};

  // heartd::Sheriff override:
  void OneShotWork() override { ++number_one_shot_work_called_; }
  bool HasShiftWork() override { return has_shift_work_; }
  void AdjustSchedule() override {}
  void ShiftWork() override { ++number_shift_work_called_; }
  void CleanUp() override {}

  void AdjustSchedule(const base::TimeDelta& schedule) { schedule_ = schedule; }
  int number_one_shot_work_called() { return number_one_shot_work_called_; }
  int number_main_work_called() { return number_shift_work_called_; }

 private:
  bool has_shift_work_ = false;
  int number_one_shot_work_called_ = 0;
  int number_shift_work_called_ = 0;
};

class TopSheriffTest : public testing::Test {
 public:
  TopSheriffTest() = default;
  ~TopSheriffTest() override = default;

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::unique_ptr<TopSheriff> top_sheriff_ = std::make_unique<TopSheriff>();
};

}  // namespace

TEST_F(TopSheriffTest, NoShiftWork) {
  TestSheriff* test_sheriff = new TestSheriff(/* has_shift_work */ false);

  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff));
  top_sheriff_->GetToWork();

  EXPECT_EQ(test_sheriff->number_one_shot_work_called(), 1);

  // The default shift frequency is 60 minutes.
  task_environment_.FastForwardBy(base::Minutes(60));
  EXPECT_EQ(test_sheriff->number_main_work_called(), 0);
}

TEST_F(TopSheriffTest, HasShiftWorkWithDefaultFrequency) {
  TestSheriff* test_sheriff = new TestSheriff(/* has_shift_work */ true);

  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff));
  top_sheriff_->GetToWork();

  EXPECT_EQ(test_sheriff->number_one_shot_work_called(), 1);

  // The default shift frequency is 60 minutes.
  task_environment_.FastForwardBy(base::Minutes(60));
  EXPECT_EQ(test_sheriff->number_main_work_called(), 1);
  task_environment_.FastForwardBy(base::Minutes(60));
  EXPECT_EQ(test_sheriff->number_main_work_called(), 2);
}

TEST_F(TopSheriffTest, AdjustSchedule) {
  TestSheriff* test_sheriff = new TestSheriff(/* has_shift_work */ true);
  test_sheriff->AdjustSchedule(base::Minutes(10));

  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff));
  top_sheriff_->GetToWork();

  EXPECT_EQ(test_sheriff->number_one_shot_work_called(), 1);

  task_environment_.FastForwardBy(base::Minutes(60));
  EXPECT_EQ(test_sheriff->number_main_work_called(), 6);
}

TEST_F(TopSheriffTest, MultipleSheriffs) {
  TestSheriff* test_sheriff_1 = new TestSheriff(/* has_shift_work */ true);
  TestSheriff* test_sheriff_2 = new TestSheriff(/* has_shift_work */ true);
  TestSheriff* test_sheriff_3 = new TestSheriff(/* has_shift_work */ false);
  test_sheriff_1->AdjustSchedule(base::Minutes(10));
  test_sheriff_2->AdjustSchedule(base::Minutes(20));

  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff_1));
  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff_2));
  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff_3));
  top_sheriff_->GetToWork();

  EXPECT_EQ(test_sheriff_1->number_one_shot_work_called(), 1);
  EXPECT_EQ(test_sheriff_2->number_one_shot_work_called(), 1);
  EXPECT_EQ(test_sheriff_3->number_one_shot_work_called(), 1);

  task_environment_.FastForwardBy(base::Minutes(60));
  EXPECT_EQ(test_sheriff_1->number_main_work_called(), 6);
  EXPECT_EQ(test_sheriff_2->number_main_work_called(), 3);
  EXPECT_EQ(test_sheriff_3->number_main_work_called(), 0);
}

TEST_F(TopSheriffTest, OneShotWorkOnlyRunOnce) {
  TestSheriff* test_sheriff = new TestSheriff(/* has_shift_work */ false);

  top_sheriff_->AddSheriff(std::unique_ptr<Sheriff>(test_sheriff));
  top_sheriff_->GetToWork();

  EXPECT_EQ(test_sheriff->number_one_shot_work_called(), 1);

  // Start shift again.
  top_sheriff_->GetToWork();
  EXPECT_EQ(test_sheriff->number_one_shot_work_called(), 1);
}

}  // namespace heartd