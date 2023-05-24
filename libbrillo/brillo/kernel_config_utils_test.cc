// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libbrillo/brillo/kernel_config_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

namespace brillo {

using ::testing::Optional;

class UtilTest : public ::testing::Test {};

TEST(UtilTest, ExtractKernelArgValueTest) {
  std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2 ver=";
  std::string dm_config = "foo bar, ver=2 root2=1 stuff=v";

  // kernel config.
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "root"),
              Optional(std::string{"/dev/dm-1"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "fuzzy"),
              Optional(std::string{"wuzzy"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "root2"),
              Optional(std::string{"/dev/dm-2"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "dm"), Optional(dm_config));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "ver"),
              Optional(std::string{""}));
}

TEST(UtilTest, ExtractFullyQuotedKey) {
  std::string kernel_config = "dm=\"foo bar, ver=2 root2=1 stuff=v\"";
  std::string expected_value = "foo bar, ver=2 root2=1 stuff=v";

  // Expect key values in quotes to be ignored.
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "stuff"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "foo"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "ver"), std::nullopt);
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "dm", true),
              Optional(expected_value));
  // Ensure quotes aren't stripped when specified.
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "dm", false),
              Optional("\"" + expected_value + "\""));
}

TEST(UtilTest, SupportTerminateToken) {
  std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, -- ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy -- root2=/dev/dm-2";

  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "root"),
              Optional(std::string{"/dev/dm-1"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "fuzzy"),
              Optional(std::string{"wuzzy"}));
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "root2"), std::nullopt);
}

TEST(UtilTest, ExtractIgnoreWhiteSpaces) {
  std::string kernel_config =
      "     root=/dev/dm-1\tdm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy                           root2=/dev/dm-2\nwuzzy=fuzzy";
  std::string dm_config = "foo bar, ver=2 root2=1 stuff=v";

  // Ensure leading and multiple delimiting spaces don't cause issues.
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "root"),
              Optional(std::string{"/dev/dm-1"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "fuzzy"),
              Optional(std::string{"wuzzy"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "root2"),
              Optional(std::string{"/dev/dm-2"}));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "dm"), Optional(dm_config));
  EXPECT_THAT(ExtractKernelArgValue(kernel_config, "wuzzy"),
              Optional(std::string{"fuzzy"}));
}

TEST(UtilTest, ExtractIgnoresQuotedKeys) {
  std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2";

  // Expect key values in quotes to be ignored.
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "foo"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "ver"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue(kernel_config, "stuff"), std::nullopt);
}

TEST(UtilTest, CorruptConfigs) {
  // Corrupt config.
  EXPECT_EQ(ExtractKernelArgValue("root=\"", "root"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue("root=\" bar", "root"), std::nullopt);
  EXPECT_EQ(ExtractKernelArgValue("root", "root"), std::nullopt);
}

TEST(UtilTest, SetKernelArgTest) {
  const std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2";
  std::string working_config;

  // Basic change.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("fuzzy", "tuzzy", working_config), true);
  EXPECT_EQ(working_config,
            "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=tuzzy root2=/dev/dm-2");

  // Empty a value.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("root", "", working_config), true);
  EXPECT_EQ(working_config,
            "root= dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=wuzzy root2=/dev/dm-2");
}

TEST(UtilTest, SetQuotedArgTest) {
  const std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2";
  std::string working_config;

  // Basic change.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("root", "\"a b \"", working_config), true);
  EXPECT_EQ(working_config,
            "root=\"a b \" dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=wuzzy root2=/dev/dm-2");

  // Empty a value.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("fuzzy", "\"tuzzy\"", working_config), true);
  EXPECT_EQ(working_config,
            "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=\"tuzzy\" root2=/dev/dm-2");
}

TEST(UtilTest, SetQuotedKernelArgTest) {
  const std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2";

  std::string working_config;
  // Set a value that requires quotes.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("root", "a b", working_config), true);
  EXPECT_EQ(working_config,
            "root=\"a b\" dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=wuzzy root2=/dev/dm-2");

  // Change a value that requires quotes to be removed.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("dm", "ab", working_config), true);
  EXPECT_EQ(working_config, "root=/dev/dm-1 dm=ab fuzzy=wuzzy root2=/dev/dm-2");

  // Change a quoted value that stays quoted.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("dm", "a b", working_config), true);
  EXPECT_EQ(working_config,
            "root=/dev/dm-1 dm=\"a b\" fuzzy=wuzzy root2=/dev/dm-2");
}

TEST(UtilTest, SetQuotedKernelArgWhiteSpacesTest) {
  const std::string kernel_config =
      "root=/dev/dm-1\ndm=\"foo bar, ver=2 root2=1 stuff=v\""
      "      fuzzy=wuzzy \t root2=/dev/dm-2";

  std::string working_config;
  working_config = kernel_config;
  // Ensure we skip over white spaces to edit the right root2 key.
  EXPECT_EQ(SetKernelArg("root2", "a b", working_config), true);
  EXPECT_EQ(working_config,
            "root=/dev/dm-1\ndm=\"foo bar, ver=2 root2=1 stuff=v\""
            "      fuzzy=wuzzy \t root2=\"a b\"");
}

TEST(UtilTest, SetUnsetEmptyValues) {
  std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=";
  // Ensure we skip over white spaces to edit the right root2 key.
  EXPECT_EQ(SetKernelArg("root2", "new_root", kernel_config), true);
  EXPECT_EQ(kernel_config,
            "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=wuzzy root2=new_root");

  EXPECT_EQ(SetKernelArg("root2", "", kernel_config), true);
  EXPECT_EQ(kernel_config,
            "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
            " fuzzy=wuzzy root2=");
}

TEST(UtilTest, SetUnknownValuesTest) {
  const std::string kernel_config =
      "root=/dev/dm-1 dm=\"foo bar, ver=2 root2=1 stuff=v\""
      " fuzzy=wuzzy root2=/dev/dm-2";

  std::string working_config;
  // Try to change value that's not present.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("unknown", "", working_config), false);
  EXPECT_EQ(working_config, kernel_config);

  // Try to change a term inside quotes to ensure it's ignored.
  working_config = kernel_config;
  EXPECT_EQ(SetKernelArg("ver", "", working_config), false);
  EXPECT_EQ(working_config, kernel_config);
}

}  // namespace brillo
