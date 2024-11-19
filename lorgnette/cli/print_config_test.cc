// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/print_config.h"

#include <sstream>
#include <string>
#include <utility>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette::cli {

namespace {

lorgnette::ScannerConfig MakeScannerConfig() {
  lorgnette::ScannerConfig config;

  // One active basic option.
  lorgnette::ScannerOption basic_opt;
  basic_opt.set_name("basic-option");
  basic_opt.set_title("Basic Option Title");
  basic_opt.set_description("Basic Option Description");
  basic_opt.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt.set_active(true);
  (*config.mutable_options())["basic-option"] = std::move(basic_opt);

  // One inactive basic option.
  lorgnette::ScannerOption inactive_opt;
  inactive_opt.set_name("inactive-option");
  inactive_opt.set_title("Inactive Option Title");
  inactive_opt.set_description("Inactive Option Description");
  inactive_opt.set_option_type(lorgnette::TYPE_STRING);
  inactive_opt.set_active(false);
  (*config.mutable_options())["inactive-option"] = std::move(inactive_opt);

  // One active advanced option.
  lorgnette::ScannerOption advanced_opt;
  advanced_opt.set_name("advanced-option");
  advanced_opt.set_title("Advanced Option Title");
  advanced_opt.set_description("Advanced Option Description");
  advanced_opt.set_option_type(lorgnette::TYPE_INT);
  advanced_opt.set_unit(lorgnette::OptionUnit::UNIT_DPI);
  advanced_opt.set_active(true);
  advanced_opt.set_advanced(true);
  (*config.mutable_options())["advanced-option"] = std::move(advanced_opt);

  // Two options in the first group.  The group will always  be visible.
  OptionGroup* group1 = config.add_option_groups();
  group1->set_title("Basic Group");
  *group1->add_members() = "basic-option";
  *group1->add_members() = "inactive-option";

  // No options in the empty group.  It will never be visible.
  OptionGroup* group2 = config.add_option_groups();
  group2->set_title("Empty Group");

  // One advanced option in the third group.  It will only be visible if
  // advanced options are shown.
  OptionGroup* group3 = config.add_option_groups();
  group3->set_title("Advanced Group");
  *group3->add_members() = "advanced-option";

  return config;
}

lorgnette::ScannerConfig MakeScannerConfigGroupAndUngrouped() {
  lorgnette::ScannerConfig config;

  OptionGroup* group1 = config.add_option_groups();
  group1->set_title("Basic Group");
  *group1->add_members() = "basic-option-grouped";
  *group1->add_members() = "basic-option-grouped_inactive";

  lorgnette::ScannerOption basic_opt;
  basic_opt.set_name("basic-option-grouped");
  basic_opt.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt.set_active(true);
  (*config.mutable_options())["basic-option-grouped"] = std::move(basic_opt);

  lorgnette::ScannerOption inactive_opt;
  inactive_opt.set_name("basic-option-grouped_inactive");
  inactive_opt.set_option_type(lorgnette::TYPE_STRING);
  inactive_opt.set_active(false);
  (*config.mutable_options())["basic-option-grouped_inactive"] =
      std::move(inactive_opt);

  lorgnette::ScannerOption basic_opt_no_group1;
  basic_opt_no_group1.set_name("basic-option-1-ungrouped");
  basic_opt_no_group1.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_no_group1.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_no_group1.set_active(true);
  (*config.mutable_options())["basic-option-1-ungrouped"] =
      std::move(basic_opt_no_group1);

  lorgnette::ScannerOption basic_opt_no_group2;
  basic_opt_no_group2.set_name("basic-option-2-ungrouped_inactive");
  basic_opt_no_group2.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_no_group2.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_no_group2.set_active(false);
  (*config.mutable_options())["basic-option-2-ungrouped_inactive"] =
      std::move(basic_opt_no_group2);

  return config;
}

lorgnette::ScannerConfig MakeScannerConfigNoGroups() {
  lorgnette::ScannerConfig config;

  lorgnette::ScannerOption basic_opt_1;
  basic_opt_1.set_name("basic-option-1-ungrouped");
  basic_opt_1.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_1.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_1.set_active(true);
  (*config.mutable_options())["basic-option-1-ungrouped"] =
      std::move(basic_opt_1);

  lorgnette::ScannerOption basic_opt_2;
  basic_opt_2.set_name("basic-option-2-ungrouped_inactive");
  basic_opt_2.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_2.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_2.set_active(false);
  (*config.mutable_options())["basic-option-2-ungrouped_inactive"] =
      std::move(basic_opt_2);

  lorgnette::ScannerOption basic_opt_3;
  basic_opt_3.set_name("basic-option-3-ungrouped");
  basic_opt_3.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_3.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_3.set_active(true);
  (*config.mutable_options())["basic-option-3-ungrouped"] =
      std::move(basic_opt_3);

  // One active advanced option.
  lorgnette::ScannerOption advanced_opt_ungrouped;
  advanced_opt_ungrouped.set_name("advanced-option-ungrouped");
  advanced_opt_ungrouped.set_option_type(lorgnette::TYPE_INT);
  advanced_opt_ungrouped.set_unit(lorgnette::OptionUnit::UNIT_DPI);
  advanced_opt_ungrouped.set_active(true);
  advanced_opt_ungrouped.set_advanced(true);
  (*config.mutable_options())["advanced-option-ungrouped"] =
      std::move(advanced_opt_ungrouped);

  return config;
}

lorgnette::ScannerConfig MakeScannerConfigOneGroup() {
  lorgnette::ScannerConfig config;

  lorgnette::ScannerOption basic_opt_1;
  basic_opt_1.set_name("basic-option-1-grouped");
  basic_opt_1.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_1.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_1.set_active(true);
  (*config.mutable_options())["basic-option-1-grouped"] =
      std::move(basic_opt_1);

  // Inactive Option
  lorgnette::ScannerOption basic_opt_2;
  basic_opt_2.set_name("basic-inactive-option-grouped");
  basic_opt_2.set_option_type(lorgnette::TYPE_FIXED);
  basic_opt_2.set_unit(lorgnette::OptionUnit::UNIT_MM);
  basic_opt_2.set_active(false);
  (*config.mutable_options())["basic-inactive-option-grouped"] =
      std::move(basic_opt_2);

  OptionGroup* group1 = config.add_option_groups();
  group1->set_title("Basic Group");
  *group1->add_members() = "basic-option-1-grouped";
  *group1->add_members() = "basic-inactive-option-grouped";

  return config;
}

TEST(PrintConfig, BasicOutputOnly) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfig(),
                     /*show_inactive=*/false,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_EQ(output.find("Empty Group"), std::string::npos);
  EXPECT_EQ(output.find("Advanced Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option:  Basic Option Title\n"),
            std::string::npos);
  EXPECT_EQ(output.find("inactive-option:  Inactive Option Title\n"),
            std::string::npos);
  EXPECT_EQ(output.find("advanced-option:  Advanced Option Title\n"),
            std::string::npos);
}

TEST(PrintConfig, OutputWithInactive) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfig(),
                     /*show_inactive=*/true,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_EQ(output.find("Empty Group"), std::string::npos);
  EXPECT_EQ(output.find("Advanced Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option:  Basic Option Title\n"),
            std::string::npos);
  EXPECT_NE(output.find("inactive-option:  Inactive Option Title\n"),
            std::string::npos);
  EXPECT_EQ(output.find("advanced-option:  Advanced Option Title\n"),
            std::string::npos);
}

TEST(PrintConfig, OutputWithAdvanced) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfig(),
                     /*show_inactive=*/false,
                     /*show_advanced=*/true, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_EQ(output.find("Empty Group"), std::string::npos);
  EXPECT_NE(output.find("Advanced Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option:  Basic Option Title\n"),
            std::string::npos);
  EXPECT_EQ(output.find("inactive-option:  Inactive Option Title\n"),
            std::string::npos);
  EXPECT_NE(output.find("advanced-option:  Advanced Option Title\n"),
            std::string::npos);
}

TEST(PrintConfig, OutputNoGroups) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfigNoGroups(),
                     /*show_inactive=*/false,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Ungrouped Options"), std::string::npos);
  EXPECT_NE(output.find("basic-option-1"), std::string::npos);
  EXPECT_EQ(output.find("basic-option-2"), std::string::npos);
  EXPECT_NE(output.find("basic-option-3"), std::string::npos);
  EXPECT_EQ(output.find("advanced-option-ungrouped"), std::string::npos);
}

TEST(PrintConfig, OutputNoGroupsShowAdvanced) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfigNoGroups(),
                     /*show_inactive=*/false,
                     /*show_advanced=*/true, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Ungrouped Options"), std::string::npos);
  EXPECT_NE(output.find("basic-option-1"), std::string::npos);
  EXPECT_EQ(output.find("basic-option-2"), std::string::npos);
  EXPECT_NE(output.find("basic-option-3"), std::string::npos);
  EXPECT_NE(output.find("advanced-option-ungrouped"), std::string::npos);
}

TEST(PrintConfig, OutputWithOneGroup) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfigOneGroup(),
                     /*show_inactive=*/false,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option-1-grouped"), std::string::npos);
  EXPECT_EQ(output.find("basic-inactive-option-grouped"), std::string::npos);
}

TEST(PrintConfig, OutputWithOneGroupShowInactive) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfigOneGroup(),
                     /*show_inactive=*/true,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option-1"), std::string::npos);
  EXPECT_NE(output.find("basic-inactive-option-grouped"), std::string::npos);
}

TEST(PrintConfig, OutputWithGroupedAndUngroupedShowInactive) {
  std::ostringstream os;

  PrintScannerConfig(MakeScannerConfigGroupAndUngrouped(),
                     /*show_inactive=*/true,
                     /*show_advanced=*/false, os);
  std::string output = os.str();

  EXPECT_TRUE(base::StartsWith(output, "--- Scanner Config ---\n"));
  EXPECT_TRUE(base::EndsWith(output, "--- End Scanner Config ---\n"));
  EXPECT_NE(output.find("Basic Group"), std::string::npos);
  EXPECT_NE(output.find("basic-option-grouped"), std::string::npos);
  EXPECT_NE(output.find("basic-option-grouped_inactive"), std::string::npos);
  EXPECT_NE(output.find("Ungrouped Options"), std::string::npos);
  EXPECT_NE(output.find("basic-option-1-ungrouped"), std::string::npos);
  EXPECT_NE(output.find("basic-option-2-ungrouped_inactive"),
            std::string::npos);
}

}  // namespace
}  // namespace lorgnette::cli
