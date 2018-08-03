// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/policy/user_proximity_voting.h"

#include <gtest/gtest.h>

namespace power_manager {
namespace policy {

TEST(UserProximityVotingTest, DefaultStates) {
  UserProximityVoting voting;
  EXPECT_EQ(voting.GetVote(), UserProximity::UNKNOWN);

  EXPECT_TRUE(voting.Vote(1, UserProximity::NEAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::NEAR);
}

TEST(UserProximityVotingTest, StateChange) {
  UserProximityVoting voting;
  EXPECT_TRUE(voting.Vote(1, UserProximity::NEAR));

  EXPECT_TRUE(voting.Vote(1, UserProximity::FAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::FAR);

  EXPECT_FALSE(voting.Vote(1, UserProximity::FAR));

  EXPECT_TRUE(voting.Vote(1, UserProximity::NEAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::NEAR);
}

TEST(UserProximityVotingTest, ConsensusChange) {
  UserProximityVoting voting;
  EXPECT_TRUE(voting.Vote(1, UserProximity::NEAR));
  EXPECT_FALSE(voting.Vote(2, UserProximity::NEAR));

  EXPECT_FALSE(voting.Vote(1, UserProximity::FAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::NEAR);

  EXPECT_TRUE(voting.Vote(2, UserProximity::FAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::FAR);

  EXPECT_FALSE(voting.Vote(1, UserProximity::FAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::FAR);

  EXPECT_TRUE(voting.Vote(2, UserProximity::NEAR));
  EXPECT_EQ(voting.GetVote(), UserProximity::NEAR);
}

}  // namespace policy
}  // namespace power_manager
