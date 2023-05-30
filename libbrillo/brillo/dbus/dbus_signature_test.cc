// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/dbus/dbus_signature.h"

#include <map>
#include <string>
#include <tuple>
#include <utility>

#include <base/files/scoped_file.h>
#include <dbus/object_path.h>
#include <gtest/gtest.h>

#include "brillo/any.h"
#include "brillo/dbus/test.pb.h"

namespace brillo::dbus_utils {

TEST(DBusUtils, Signatures_BasicTypes) {
  EXPECT_STREQ("b", GetDBusSignature<bool>());
  EXPECT_STREQ("y", GetDBusSignature<std::uint8_t>());
  EXPECT_STREQ("n", GetDBusSignature<std::int16_t>());
  EXPECT_STREQ("q", GetDBusSignature<std::uint16_t>());
  EXPECT_STREQ("i", GetDBusSignature<std::int32_t>());
  EXPECT_STREQ("u", GetDBusSignature<std::uint32_t>());
  EXPECT_STREQ("x", GetDBusSignature<std::int64_t>());
  EXPECT_STREQ("t", GetDBusSignature<std::uint64_t>());
  EXPECT_STREQ("d", GetDBusSignature<double>());
  EXPECT_STREQ("s", GetDBusSignature<std::string>());
  EXPECT_STREQ("o", GetDBusSignature<dbus::ObjectPath>());
  EXPECT_STREQ("h", GetDBusSignature<base::ScopedFD>());
  EXPECT_STREQ("v", GetDBusSignature<Any>());
}

TEST(DBusUtils, Signatures_Arrays) {
  EXPECT_STREQ("ab", GetDBusSignature<std::vector<bool>>());
  EXPECT_STREQ("ay", GetDBusSignature<std::vector<std::uint8_t>>());
  EXPECT_STREQ("an", GetDBusSignature<std::vector<std::int16_t>>());
  EXPECT_STREQ("aq", GetDBusSignature<std::vector<std::uint16_t>>());
  EXPECT_STREQ("ai", GetDBusSignature<std::vector<std::int32_t>>());
  EXPECT_STREQ("au", GetDBusSignature<std::vector<std::uint32_t>>());
  EXPECT_STREQ("ax", GetDBusSignature<std::vector<std::int64_t>>());
  EXPECT_STREQ("at", GetDBusSignature<std::vector<std::uint64_t>>());
  EXPECT_STREQ("ad", GetDBusSignature<std::vector<double>>());
  EXPECT_STREQ("as", GetDBusSignature<std::vector<std::string>>());
  EXPECT_STREQ("ao", GetDBusSignature<std::vector<dbus::ObjectPath>>());
  EXPECT_STREQ("ah", GetDBusSignature<std::vector<base::ScopedFD>>());
  EXPECT_STREQ("av", GetDBusSignature<std::vector<Any>>());
  EXPECT_STREQ(
      "a(is)",
      (GetDBusSignature<std::vector<std::pair<std::int32_t, std::string>>>()));
  EXPECT_STREQ("aad", GetDBusSignature<std::vector<std::vector<double>>>());
}

TEST(DBusUtils, Signatures_Maps) {
  EXPECT_STREQ("a{sb}", (GetDBusSignature<std::map<std::string, bool>>()));
  EXPECT_STREQ("a{ss}",
               (GetDBusSignature<std::map<std::string, std::string>>()));
  EXPECT_STREQ("a{sv}",
               (GetDBusSignature<std::map<std::string, brillo::Any>>()));
  EXPECT_STREQ("a{id}", (GetDBusSignature<std::map<std::int32_t, double>>()));
  EXPECT_STREQ(
      "a{ia{ss}}",
      (GetDBusSignature<std::map<int, std::map<std::string, std::string>>>()));
}

TEST(DBusUtils, Signatures_Pairs) {
  EXPECT_STREQ("(sb)", (GetDBusSignature<std::pair<std::string, bool>>()));
  EXPECT_STREQ("(sv)", (GetDBusSignature<std::pair<std::string, Any>>()));
  EXPECT_STREQ("(id)", (GetDBusSignature<std::pair<int, double>>()));
}

TEST(DBusUtils, Signatures_Tuples) {
  EXPECT_STREQ("(i)", (GetDBusSignature<std::tuple<std::int32_t>>()));
  EXPECT_STREQ("(sv)",
               (GetDBusSignature<std::tuple<std::string, brillo::Any>>()));
  EXPECT_STREQ(
      "(id(si))",
      (GetDBusSignature<std::tuple<std::int32_t, double,
                                   std::tuple<std::string, std::int32_t>>>()));
}

TEST(DBusUtils, Signatures_Protobufs) {
  EXPECT_STREQ("ay", (GetDBusSignature<google::protobuf::MessageLite>()));
  EXPECT_STREQ("ay", (GetDBusSignature<dbus_utils_test::TestMessage>()));
}

}  // namespace brillo::dbus_utils
