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

}  // namespace
}  // namespace lorgnette::cli
