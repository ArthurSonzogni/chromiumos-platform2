// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/daemon_task.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "modemfwd/mock_daemon_delegate.h"

using ::testing::InSequence;
using ::testing::Invoke;

namespace modemfwd {

class TaskTest : public ::testing::Test {
 public:
  TaskTest() {}

 protected:
  MockDelegate delegate_;
};

class SetPropTask : public Task {
 public:
  explicit SetPropTask(Delegate* delegate) : Task(delegate, "set", "set") {}

  void Start() { SetProp("foo", 1); }
};

TEST_F(TaskTest, SetProp) {
  SetPropTask task(&delegate_);

  EXPECT_CALL(delegate_, TaskUpdated(&task)).Times(1);
  task.Start();
  EXPECT_EQ(task.props().at("foo"), 1);
}

class UpdatePropTask : public Task {
 public:
  explicit UpdatePropTask(Delegate* delegate)
      : Task(delegate, "update", "update") {}

  void Start() {
    SetProp("foo", 1);
    SetProp("foo", 2);
  }
};

TEST_F(TaskTest, UpdateProp) {
  UpdatePropTask task(&delegate_);

  {
    InSequence s;
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().at("foo"), 1);
    }));
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().at("foo"), 2);
    }));
  }
  task.Start();
}

class NoopUpdateTask : public Task {
 public:
  explicit NoopUpdateTask(Delegate* delegate)
      : Task(delegate, "noop_update", "noop_update") {}

  void Start() {
    SetProp("foo", 1);
    SetProp("foo", 1);
  }
};

TEST_F(TaskTest, NoopUpdate) {
  NoopUpdateTask task(&delegate_);

  EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
    EXPECT_EQ(task->props().at("foo"), 1);
  }));
  task.Start();
}

class SetEmptyPropTask : public Task {
 public:
  explicit SetEmptyPropTask(Delegate* delegate)
      : Task(delegate, "set_empty", "set_empty") {}

  void Start() { SetProp("foo", brillo::Any()); }
};

TEST_F(TaskTest, SetEmptyProp) {
  SetEmptyPropTask task(&delegate_);

  EXPECT_CALL(delegate_, TaskUpdated(&task)).Times(0);
  task.Start();
  EXPECT_EQ(task.props().count("foo"), 0);
}

class DeletePropTask : public Task {
 public:
  explicit DeletePropTask(Delegate* delegate)
      : Task(delegate, "delete", "delete") {}

  void Start() {
    SetProp("foo", "bar");
    DeleteProp("foo");
  }
};

TEST_F(TaskTest, DeleteProp) {
  DeletePropTask task(&delegate_);

  {
    InSequence s;
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().at("foo"), "bar");
    }));
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().count("foo"), 0);
    }));
  }
  task.Start();
}

class DeleteViaSetEmptyTask : public Task {
 public:
  explicit DeleteViaSetEmptyTask(Delegate* delegate)
      : Task(delegate, "delete_set_empty", "delete_set_empty") {}

  void Start() {
    SetProp("foo", "bar");
    SetProp("foo", brillo::Any());
  }
};

TEST_F(TaskTest, DeletePropViaSetEmpty) {
  DeleteViaSetEmptyTask task(&delegate_);

  {
    InSequence s;
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().at("foo"), "bar");
    }));
    EXPECT_CALL(delegate_, TaskUpdated(&task)).WillOnce(Invoke([](Task* task) {
      EXPECT_EQ(task->props().count("foo"), 0);
    }));
  }
  task.Start();
}

}  // namespace modemfwd
