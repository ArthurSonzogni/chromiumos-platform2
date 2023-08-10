// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/mglru.h"

#include <vector>

#include <gtest/gtest.h>

#include "vm_tools/concierge/mm/mglru_test_util.h"

using vm_tools::vm_memory_management::MglruGeneration;
using vm_tools::vm_memory_management::MglruMemcg;
using vm_tools::vm_memory_management::MglruNode;

namespace vm_tools::concierge::mglru {
namespace {

const char simple_input[] =
    R"(memcg     1
 node     2
        3      4      5        6
)";

TEST(MglruUtilTest, TestEmpty) {
  std::optional<MglruStats> stats = ParseStatsFromString("", 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestWrongTokenCg) {
  std::string input =
      R"(Pmemcg     0
 node     0
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestMissingIdCg) {
  std::string input =
      R"(memcg
 node     0
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestWrongTokenNode) {
  std::string input =
      R"(memcg     0
 Pnode     0
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestMissingIdNode) {
  std::string input =
      R"(memcg     0
 node
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestMissingCgHeader) {
  std::string input =
      R"(node     0
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestMissingNodeHeader) {
  std::string input =
      R"(memcg     0
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestMissingGeneration) {
  std::string input =
      R"(memcg     0
 node     0
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestTooBigGeneration) {
  std::string input =
      R"(memcg     0
 node     0
        695      40523      18334        4175 55
        696      40523      18334        4175
)";

  MglruStats expected_stats;
  MglruMemcg* cg = AddMemcg(&expected_stats, 0);
  MglruNode* node = AddNode(cg, 0);
  AddGeneration(node, 695, 40523, 18334, 4175);
  AddGeneration(node, 696, 40523, 18334, 4175);

  std::optional<MglruStats> stats = ParseStatsFromString(input, 1024);
  ASSERT_TRUE(stats);
  ASSERT_TRUE(StatsEqual(expected_stats, *stats));
}

TEST(MglruUtilTest, TestTooSmallGeneration) {
  std::string input =
      R"(memcg     0
 node     0
        695      40523      18334
        695      40523      18334        4175
)";

  std::optional<MglruStats> stats = ParseStatsFromString(input, 4096);
  ASSERT_FALSE(stats);
}

TEST(MglruUtilTest, TestSimple) {
  MglruStats expected_stats;
  MglruMemcg* cg = AddMemcg(&expected_stats, 1);
  MglruNode* node = AddNode(cg, 2);
  AddGeneration(node, 3, 4, 5, 6);

  // A 'page size' of 1024 means that there should be no conversion from page
  // units to KB units, so the expected stats should exactly match the input
  // file
  std::optional<MglruStats> stats = ParseStatsFromString(simple_input, 1024);
  ASSERT_TRUE(stats);
  ASSERT_TRUE(StatsEqual(expected_stats, *stats));
}

TEST(MglruUtilTest, TestPageSizeConversion) {
  MglruStats expected_stats;
  MglruMemcg* cg = AddMemcg(&expected_stats, 1);
  MglruNode* node = AddNode(cg, 2);
  AddGeneration(node, 3, 4, 20, 24);

  // A page size of 4096 means that the input file (pages) should be multiplied
  // by 4 to get KB units
  std::optional<MglruStats> stats = ParseStatsFromString(simple_input, 4096);
  ASSERT_TRUE(stats);
  ASSERT_TRUE(StatsEqual(expected_stats, *stats));
}

TEST(MglruUtilTest, TestMultiple) {
  std::string input =
      R"(memcg     0
 node     0
        695      40523      18334        4175
        696      35101      35592       22242
        697      10961      32552       12081
        698       3419      21460        4438
 node     1
        695      40523      18334        4175
        696      35101      35592       22242
        697      10961      32552       12081
        698       3419      21460        4438
memcg     1
 node     0
        695      40523      18334        4175
        696      35101      35592       22242
        697      10961      32552       12081
        698       3419      21460        4438
)";

  MglruStats expected_stats;
  MglruMemcg* cg = AddMemcg(&expected_stats, 0);
  MglruNode* node = AddNode(cg, 0);
  AddGeneration(node, 695, 40523, 18334, 4175);
  AddGeneration(node, 696, 35101, 35592, 22242);
  AddGeneration(node, 697, 10961, 32552, 12081);
  AddGeneration(node, 698, 3419, 21460, 4438);
  node = AddNode(cg, 1);
  AddGeneration(node, 695, 40523, 18334, 4175);
  AddGeneration(node, 696, 35101, 35592, 22242);
  AddGeneration(node, 697, 10961, 32552, 12081);
  AddGeneration(node, 698, 3419, 21460, 4438);
  cg = AddMemcg(&expected_stats, 1);
  node = AddNode(cg, 0);
  AddGeneration(node, 695, 40523, 18334, 4175);
  AddGeneration(node, 696, 35101, 35592, 22242);
  AddGeneration(node, 697, 10961, 32552, 12081);
  AddGeneration(node, 698, 3419, 21460, 4438);

  // Page size of 1024 should result in no conversion from pages to KB units
  std::optional<MglruStats> stats = ParseStatsFromString(input, 1024);
  ASSERT_TRUE(stats);
  ASSERT_TRUE(StatsEqual(expected_stats, *stats));
}

TEST(MglruUtilTest, TestMultipleNewKernel) {
  // New kernel versions have a trailing '/' after the memcg id
  std::string input =
      R"(memcg     1 /
  node     0
           0       1177          0         822
           1       1177          7           0
           2       1177          0           0
           3       1177       1171        5125
)";

  MglruStats expected_stats;
  MglruMemcg* cg = AddMemcg(&expected_stats, 1);
  MglruNode* node = AddNode(cg, 0);
  AddGeneration(node, 0, 1177, 0, 822);
  AddGeneration(node, 1, 1177, 7, 0);
  AddGeneration(node, 2, 1177, 0, 0);
  AddGeneration(node, 3, 1177, 1171, 5125);

  std::optional<MglruStats> stats = ParseStatsFromString(input, 1024);
  ASSERT_TRUE(stats);
  ASSERT_TRUE(StatsEqual(expected_stats, *stats));
}

}  // namespace
}  // namespace vm_tools::concierge::mglru
