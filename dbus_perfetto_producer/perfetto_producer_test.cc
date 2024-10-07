// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dbus_perfetto_producer/perfetto_producer.cc"

#include <base/check.h>
#include <gtest/gtest.h>

namespace dbus_perfetto_producer {

namespace {

class PerfettoProducerTest : public ::testing::Test {
 public:
  PerfettoProducerTest() = default;
  PerfettoProducerTest(const PerfettoProducerTest&) = delete;
  PerfettoProducerTest& operator=(const PerfettoProducerTest&) = delete;
};

void GetOriginalDestinationTest(MethodMap& methods,
                                uint64_t key,
                                std::string val) {
  EXPECT_EQ(val, GetOriginalDestination(methods, key));
  EXPECT_TRUE(GetOriginalDestination(methods, key).empty());
}

// This function tests that return values searched by a well-known name and a
// unique name are the same and both correct process information.
// If an input name is not mapped, GetProcessInfo() sends D-Bus messages, which
// cannot be tested in the current unit test environment. Therefore, only mapped
// keys can be tested with this function.
void GetProcessInfoTest(Maps& maps,
                        std::string well_known_name,
                        std::string unique_name,
                        uint64_t id,
                        std::string name) {
  ProcessInfo& process_from_unique =
      GetProcessInfo(nullptr, nullptr, maps, well_known_name);
  ProcessInfo& process_from_well_known =
      GetProcessInfo(nullptr, nullptr, maps, unique_name);
  EXPECT_EQ(id, process_from_unique.id);
  EXPECT_EQ(id, process_from_well_known.id);
  EXPECT_EQ(name, process_from_unique.name);
  EXPECT_EQ(name, process_from_well_known.name);
}

}  // namespace

TEST_F(PerfettoProducerTest, GetProcessNameTests) {
  EXPECT_EQ("Unknown 0", GetProcessName(0));
  EXPECT_EQ("platform2_test. 1", GetProcessName(1));
}

TEST_F(PerfettoProducerTest, GetOriginalDestinationTests) {
  MethodMap methods;
  methods[1] = "1";
  methods[10000000000000] = "Original Destination";
  GetOriginalDestinationTest(methods, 1, "1");
  GetOriginalDestinationTest(methods, 10000000000000, "Original Destination");
  ASSERT_EQ(0, methods.size());
}

TEST_F(PerfettoProducerTest, GetProcessInfoTests) {
  Maps maps;
  maps.names["well-known name"] = ":unique name";
  maps.names["org.chromium.UserDataAuth"] = ":1.47";
  maps.processes[":unique name"] = ProcessInfo(12345, "process name", nullptr);
  maps.processes[":1.47"] = ProcessInfo(1176, "cryptohomed", nullptr);
  GetProcessInfoTest(maps, "well-known name", ":unique name", 12345,
                     "process name");
  GetProcessInfoTest(maps, "org.chromium.UserDataAuth", ":1.47", 1176,
                     "cryptohomed");
}

}  // namespace dbus_perfetto_producer
