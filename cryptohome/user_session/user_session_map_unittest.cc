// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/user_session_map.h"

#include <utility>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <gtest/gtest.h>

#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/user_session.h"

namespace cryptohome {
namespace {

using testing::Pair;
using testing::UnorderedElementsAre;

constexpr char kUsername1[] = "foo1@bar.com";
constexpr char kUsername2[] = "foo2@bar.com";

scoped_refptr<UserSession> CreateSession() {
  return base::MakeRefCounted<MockUserSession>();
}

// TODO(b/243846478): Receive `session` via a const-ref, after const
// `begin()`/`end()` methods are added.
std::vector<std::pair<std::string, scoped_refptr<UserSession>>> GetSessionItems(
    UserSessionMap& session_map) {
  return {session_map.begin(), session_map.end()};
}

class UserSessionMapTest : public testing::Test {
 protected:
  UserSessionMap session_map_;
};

TEST_F(UserSessionMapTest, InitialEmpty) {
  EXPECT_TRUE(session_map_.empty());
  EXPECT_EQ(session_map_.size(), 0);
  EXPECT_EQ(session_map_.begin(), session_map_.end());
  EXPECT_EQ(session_map_.Find(kUsername1), nullptr);
  EXPECT_EQ(session_map_.Find(kUsername2), nullptr);
}

TEST_F(UserSessionMapTest, AddOne) {
  const scoped_refptr<UserSession> session = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session));

  EXPECT_FALSE(session_map_.empty());
  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_THAT(GetSessionItems(session_map_),
              UnorderedElementsAre(Pair(kUsername1, session)));
  EXPECT_EQ(session_map_.Find(kUsername1), session);
  EXPECT_EQ(session_map_.Find(kUsername2), nullptr);
}

TEST_F(UserSessionMapTest, AddTwo) {
  const scoped_refptr<UserSession> session1 = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session1));
  const scoped_refptr<UserSession> session2 = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername2, session2));

  EXPECT_FALSE(session_map_.empty());
  EXPECT_EQ(session_map_.size(), 2);
  EXPECT_THAT(GetSessionItems(session_map_),
              UnorderedElementsAre(Pair(kUsername1, session1),
                                   Pair(kUsername2, session2)));
  EXPECT_EQ(session_map_.Find(kUsername1), session1);
  EXPECT_EQ(session_map_.Find(kUsername2), session2);
}

TEST_F(UserSessionMapTest, AddDuplicate) {
  const scoped_refptr<UserSession> session = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session));

  EXPECT_FALSE(session_map_.Add(kUsername1, session));

  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_EQ(session_map_.Find(kUsername1), session);
}

TEST_F(UserSessionMapTest, RemoveSingle) {
  const scoped_refptr<UserSession> session = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session));

  EXPECT_TRUE(session_map_.Remove(kUsername1));

  EXPECT_EQ(session_map_.size(), 0);
  EXPECT_EQ(session_map_.Find(kUsername1), nullptr);
}

TEST_F(UserSessionMapTest, RemoveWhenEmpty) {
  EXPECT_FALSE(session_map_.Remove(kUsername1));

  EXPECT_EQ(session_map_.size(), 0);
  EXPECT_EQ(session_map_.Find(kUsername1), nullptr);
}

TEST_F(UserSessionMapTest, RemoveNonExisting) {
  const scoped_refptr<UserSession> session1 = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session1));

  EXPECT_FALSE(session_map_.Remove(kUsername2));

  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_EQ(session_map_.Find(kUsername1), session1);
  EXPECT_EQ(session_map_.Find(kUsername2), nullptr);
}

TEST_F(UserSessionMapTest, RemoveTwice) {
  const scoped_refptr<UserSession> session = CreateSession();
  EXPECT_TRUE(session_map_.Add(kUsername1, session));
  EXPECT_TRUE(session_map_.Remove(kUsername1));

  EXPECT_FALSE(session_map_.Remove(kUsername1));

  EXPECT_EQ(session_map_.size(), 0);
  EXPECT_EQ(session_map_.Find(kUsername1), nullptr);
}

}  // namespace
}  // namespace cryptohome
