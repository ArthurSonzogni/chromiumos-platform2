// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/shims/ppp.h"

#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace shill {

class PPPTest : public testing::Test {
 protected:
};

TEST(PPPTest, NameSecretShort) {
  char username[256] = {0};  // MAXNAMELEN
  char password[256] = {0};  // MAXSECRETLEN

  std::string user(30, 'A');
  std::string pass(30, 'B');

  EXPECT_TRUE(shims::PPP::CopyName(username, user));
  EXPECT_TRUE(shims::PPP::CopySecret(password, pass));

  EXPECT_EQ(user, std::string(username));
  EXPECT_EQ(pass, std::string(password));
}

TEST(PPPTest, NameSecretMaxLen) {
  char username[256] = {0};  // MAXNAMELEN
  char password[256] = {0};  // MAXSECRETLEN

  std::string user(255, 'A');
  std::string pass(255, 'B');

  EXPECT_TRUE(shims::PPP::CopyName(username, user));
  EXPECT_TRUE(shims::PPP::CopySecret(password, pass));

  EXPECT_EQ(user, std::string(username));
  EXPECT_EQ(pass, std::string(password));
}

TEST(PPPTest, NameSecretTooLong1) {
  char username[256] = {0};  // MAXNAMELEN
  char password[256] = {0};  // MAXSECRETLEN

  std::string user(256, 'A');
  std::string pass(256, 'B');

  EXPECT_FALSE(shims::PPP::CopyName(username, user));
  EXPECT_FALSE(shims::PPP::CopySecret(password, pass));

  EXPECT_EQ("", std::string(username));
  EXPECT_EQ("", std::string(password));
}

TEST(PPPTest, NameSecretTooLong2) {
  char username[256] = {0};  // MAXNAMELEN
  char password[256] = {0};  // MAXSECRETLEN

  std::string user(300, 'A');
  std::string pass(300, 'B');

  EXPECT_FALSE(shims::PPP::CopyName(username, user));
  EXPECT_FALSE(shims::PPP::CopySecret(password, pass));

  EXPECT_EQ("", std::string(username));
  EXPECT_EQ("", std::string(password));
}

}  // namespace shill
