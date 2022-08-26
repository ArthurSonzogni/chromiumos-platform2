// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/user_session/user_session_map.h"

#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "cryptohome/user_session/mock_user_session.h"
#include "cryptohome/user_session/user_session.h"

namespace cryptohome {
namespace {

using testing::Pair;
using testing::UnorderedElementsAre;

constexpr char kUsername1[] = "foo1@bar.com";
constexpr char kUsername2[] = "foo2@bar.com";

// TODO(b/243846478): Receive `session` via a const-ref, after const
// `begin()`/`end()` methods are added.
std::vector<std::pair<std::string, const UserSession*>> GetSessionItems(
    UserSessionMap& session_map) {
  std::vector<std::pair<std::string, const UserSession*>> items;
  for (const auto& [account_id, session] : session_map) {
    items.emplace_back(account_id, session.get());
  }
  return items;
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
  auto session = std::make_unique<MockUserSession>();
  const UserSession* session_ptr = session.get();

  EXPECT_TRUE(session_map_.Add(kUsername1, std::move(session)));

  EXPECT_FALSE(session_map_.empty());
  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_THAT(GetSessionItems(session_map_),
              UnorderedElementsAre(Pair(kUsername1, session_ptr)));
  EXPECT_EQ(session_map_.Find(kUsername1), session_ptr);
  EXPECT_EQ(session_map_.Find(kUsername2), nullptr);
}

TEST_F(UserSessionMapTest, AddTwo) {
  auto session1 = std::make_unique<MockUserSession>();
  const UserSession* session1_ptr = session1.get();
  auto session2 = std::make_unique<MockUserSession>();
  const UserSession* session2_ptr = session2.get();

  EXPECT_TRUE(session_map_.Add(kUsername1, std::move(session1)));
  EXPECT_TRUE(session_map_.Add(kUsername2, std::move(session2)));

  EXPECT_FALSE(session_map_.empty());
  EXPECT_EQ(session_map_.size(), 2);
  EXPECT_THAT(GetSessionItems(session_map_),
              UnorderedElementsAre(Pair(kUsername1, session1_ptr),
                                   Pair(kUsername2, session2_ptr)));
  EXPECT_EQ(session_map_.Find(kUsername1), session1_ptr);
  EXPECT_EQ(session_map_.Find(kUsername2), session2_ptr);
}

TEST_F(UserSessionMapTest, AddDuplicate) {
  auto session1 = std::make_unique<MockUserSession>();
  const UserSession* session1_ptr = session1.get();
  EXPECT_TRUE(session_map_.Add(kUsername1, std::move(session1)));

  EXPECT_FALSE(
      session_map_.Add(kUsername1, std::make_unique<MockUserSession>()));

  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_EQ(session_map_.Find(kUsername1), session1_ptr);
}

TEST_F(UserSessionMapTest, RemoveSingle) {
  EXPECT_TRUE(
      session_map_.Add(kUsername1, std::make_unique<MockUserSession>()));

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
  auto session = std::make_unique<MockUserSession>();
  const UserSession* session_ptr = session.get();
  EXPECT_TRUE(session_map_.Add(kUsername1, std::move(session)));

  EXPECT_FALSE(session_map_.Remove(kUsername2));

  EXPECT_EQ(session_map_.size(), 1);
  EXPECT_EQ(session_map_.Find(kUsername1), session_ptr);
  EXPECT_EQ(session_map_.Find(kUsername2), nullptr);
}

TEST_F(UserSessionMapTest, RemoveTwice) {
  EXPECT_TRUE(
      session_map_.Add(kUsername1, std::make_unique<MockUserSession>()));
  EXPECT_TRUE(session_map_.Remove(kUsername1));

  EXPECT_FALSE(session_map_.Remove(kUsername1));

  EXPECT_EQ(session_map_.size(), 0);
  EXPECT_EQ(session_map_.Find(kUsername1), nullptr);
}

}  // namespace
}  // namespace cryptohome
