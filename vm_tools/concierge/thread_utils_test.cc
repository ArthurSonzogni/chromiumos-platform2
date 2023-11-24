// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/thread_utils.h"

#include "base/functional/bind.h"
#include "base/functional/callback_forward.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "gtest/gtest.h"

namespace vm_tools::concierge {
namespace {

class ThreadUtilsTest : public testing::Test {
 public:
  ThreadUtilsTest() {
    CHECK(main_sequence->RunsTasksInCurrentSequence());
    CHECK(!other_sequence->RunsTasksInCurrentSequence());
  }

 private:
  base::test::TaskEnvironment task_env_;

 protected:
  scoped_refptr<base::SequencedTaskRunner> main_sequence =
      base::SequencedTaskRunner::GetCurrentDefault();
  scoped_refptr<base::SequencedTaskRunner> other_sequence =
      base::ThreadPool::CreateSequencedTaskRunner({});
};

}  // namespace

TEST_F(ThreadUtilsTest, PostTaskAndWaitForResult) {
  base::RepeatingCallback<int()> callback = base::BindLambdaForTesting([&]() {
    EXPECT_FALSE(main_sequence->RunsTasksInCurrentSequence());
    EXPECT_TRUE(other_sequence->RunsTasksInCurrentSequence());
    return 1337;
  });
  int res = PostTaskAndWaitForResult(other_sequence,
                                     base::BindOnce(std::move(callback)));
  EXPECT_EQ(res, 1337);
}

TEST_F(ThreadUtilsTest, PostTaskAndWait) {
  int res = 1336;
  PostTaskAndWait(other_sequence, base::BindLambdaForTesting([&]() {
                    EXPECT_FALSE(main_sequence->RunsTasksInCurrentSequence());
                    EXPECT_TRUE(other_sequence->RunsTasksInCurrentSequence());
                    res += 1;
                  }));
  EXPECT_EQ(res, 1337);
}

}  // namespace vm_tools::concierge
