// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/technology.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/error.h"

using std::string;
using std::vector;
using testing::ElementsAre;
using testing::Test;

namespace shill {

class TechnologyTest : public Test {};

TEST_F(TechnologyTest, IdentifierFromName) {
  EXPECT_EQ(Technology::kEthernet, Technology::IdentifierFromName("ethernet"));
  EXPECT_EQ(Technology::kWifi, Technology::IdentifierFromName("wifi"));
  EXPECT_EQ(Technology::kCellular, Technology::IdentifierFromName("cellular"));
  EXPECT_EQ(Technology::kTunnel, Technology::IdentifierFromName("Tunnel"));
  EXPECT_EQ(Technology::kVPN, Technology::IdentifierFromName("vpn"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromName("bluetooth"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromName("wimax"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromName("foo"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromName(""));
}

TEST_F(TechnologyTest, NameFromIdentifier) {
  EXPECT_EQ("ethernet", Technology::NameFromIdentifier(Technology::kEthernet));
  EXPECT_EQ("wifi", Technology::NameFromIdentifier(Technology::kWifi));
  EXPECT_EQ("cellular", Technology::NameFromIdentifier(Technology::kCellular));
  EXPECT_EQ("Tunnel", Technology::NameFromIdentifier(Technology::kTunnel));
  EXPECT_EQ("vpn", Technology::NameFromIdentifier(Technology::kVPN));
  EXPECT_EQ("Unknown", Technology::NameFromIdentifier(Technology::kUnknown));
}

TEST_F(TechnologyTest, IdentifierFromStorageGroup) {
  EXPECT_EQ(Technology::kVPN, Technology::IdentifierFromStorageGroup("vpn"));
  EXPECT_EQ(Technology::kVPN, Technology::IdentifierFromStorageGroup("vpn_a"));
  EXPECT_EQ(Technology::kVPN, Technology::IdentifierFromStorageGroup("vpn__a"));
  EXPECT_EQ(Technology::kVPN,
            Technology::IdentifierFromStorageGroup("vpn_a_1"));
  EXPECT_EQ(Technology::kUnknown,
            Technology::IdentifierFromStorageGroup("_vpn"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromStorageGroup("_"));
  EXPECT_EQ(Technology::kUnknown, Technology::IdentifierFromStorageGroup(""));
}

TEST_F(TechnologyTest, GetTechnologyVectorFromStringWithValidTechnologyNames) {
  vector<Technology::Identifier> technologies;
  Error error;

  EXPECT_TRUE(Technology::GetTechnologyVectorFromString(
      "", &technologies, &error));
  EXPECT_THAT(technologies, ElementsAre());
  EXPECT_TRUE(error.IsSuccess());

  EXPECT_TRUE(Technology::GetTechnologyVectorFromString(
      "ethernet", &technologies, &error));
  EXPECT_THAT(technologies, ElementsAre(Technology::kEthernet));
  EXPECT_TRUE(error.IsSuccess());

  EXPECT_TRUE(Technology::GetTechnologyVectorFromString(
      "ethernet,vpn", &technologies, &error));
  EXPECT_THAT(technologies, ElementsAre(Technology::kEthernet,
                                        Technology::kVPN));
  EXPECT_TRUE(error.IsSuccess());

  EXPECT_TRUE(Technology::GetTechnologyVectorFromString(
      "wifi,ethernet,vpn", &technologies, &error));
  EXPECT_THAT(technologies, ElementsAre(Technology::kWifi,
                                        Technology::kEthernet,
                                        Technology::kVPN));
  EXPECT_TRUE(error.IsSuccess());
}

TEST_F(TechnologyTest,
       GetTechnologyVectorFromStringWithInvalidTechnologyNames) {
  vector<Technology::Identifier> technologies;
  Error error;

  EXPECT_FALSE(Technology::GetTechnologyVectorFromString(
      "foo", &technologies, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("foo is an unknown technology name", error.message());

  EXPECT_FALSE(Technology::GetTechnologyVectorFromString(
      "ethernet,bar", &technologies, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("bar is an unknown technology name", error.message());

  EXPECT_FALSE(Technology::GetTechnologyVectorFromString(
      "ethernet,foo,vpn", &technologies, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("foo is an unknown technology name", error.message());
}

TEST_F(TechnologyTest,
       GetTechnologyVectorFromStringWithDuplicateTechnologyNames) {
  vector<Technology::Identifier> technologies;
  Error error;

  EXPECT_FALSE(Technology::GetTechnologyVectorFromString(
      "ethernet,vpn,ethernet", &technologies, &error));
  EXPECT_EQ(Error::kInvalidArguments, error.type());
  EXPECT_EQ("ethernet is duplicated in the list", error.message());
}

}  // namespace shill
