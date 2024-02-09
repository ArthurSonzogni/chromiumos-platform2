// Copyright 2009 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/action_pipe.h"

#include <gtest/gtest.h>
#include <string>
#include "update_engine/common/action.h"

using std::string;

namespace chromeos_update_engine {

using chromeos_update_engine::ActionPipe;

class ActionPipeTestAction;

template <>
class ActionTraits<ActionPipeTestAction> {
 public:
  typedef string OutputObjectType;
  typedef string InputObjectType;
};

// This is a simple Action class for testing.
class ActionPipeTestAction : public Action<ActionPipeTestAction> {
 public:
  typedef string InputObjectType;
  typedef string OutputObjectType;
  ActionPipe<string>* in_pipe() { return in_pipe_.get(); }
  ActionPipe<string>* out_pipe() { return out_pipe_.get(); }
  void PerformAction() {}
  string Type() const { return "ActionPipeTestAction"; }
};

class ActionPipeTest : public ::testing::Test {};

// This test creates two simple Actions and sends a message via an ActionPipe
// from one to the other.
TEST_F(ActionPipeTest, SimpleTest) {
  ActionPipeTestAction a, b;
  BondActions(&a, &b);
  a.out_pipe()->set_contents("foo");
  EXPECT_EQ("foo", b.in_pipe()->contents());
}

TEST_F(ActionPipeTest, SetInPipeTest) {
  ActionPipeTestAction a;
  EXPECT_FALSE(a.HasInputObject());
  SetInPipe(&a);
  EXPECT_TRUE(a.HasInputObject());
}

TEST_F(ActionPipeTest, SetOutPipeTest) {
  ActionPipeTestAction a;
  EXPECT_FALSE(a.HasOutputPipe());
  SetOutPipe(&a);
  EXPECT_TRUE(a.HasOutputPipe());
}

}  // namespace chromeos_update_engine
