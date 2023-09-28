// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/scan_options.h"

#include <sstream>
#include <string>
#include <utility>

#include <base/strings/string_util.h>
#include <gtest/gtest.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/test_util.h"

namespace lorgnette::cli {

namespace {

lorgnette::ScannerConfig MakeScannerConfig() {
  lorgnette::ScannerConfig config;
  config.mutable_scanner()->set_token("TestScanner");

  lorgnette::ScannerOption fixed_opt;
  fixed_opt.set_name("fixed-option");
  fixed_opt.set_title("Fixed Option Title");
  fixed_opt.set_description("Fixed Option Description");
  fixed_opt.set_option_type(lorgnette::TYPE_FIXED);
  fixed_opt.set_unit(lorgnette::OptionUnit::UNIT_MM);
  fixed_opt.set_active(true);
  (*config.mutable_options())["fixed-option"] = std::move(fixed_opt);

  lorgnette::ScannerOption string_opt;
  string_opt.set_name("string-option");
  string_opt.set_title("String Option Title");
  string_opt.set_description("String Option Description");
  string_opt.set_option_type(lorgnette::TYPE_STRING);
  string_opt.set_active(true);
  (*config.mutable_options())["string-option"] = std::move(string_opt);

  lorgnette::ScannerOption int_opt;
  int_opt.set_name("int-option");
  int_opt.set_title("Int Option Title");
  int_opt.set_description("Int Option Description");
  int_opt.set_option_type(lorgnette::TYPE_INT);
  int_opt.set_unit(lorgnette::OptionUnit::UNIT_DPI);
  int_opt.set_active(true);
  int_opt.set_advanced(true);
  (*config.mutable_options())["int-option"] = std::move(int_opt);

  lorgnette::ScannerOption bool_opt;
  bool_opt.set_name("bool-option");
  bool_opt.set_title("Bool Option Title");
  bool_opt.set_description("Bool Option Description");
  bool_opt.set_option_type(lorgnette::TYPE_BOOL);
  bool_opt.set_active(true);
  (*config.mutable_options())["bool-option"] = std::move(bool_opt);

  return config;
}

TEST(GetScanOptions, NoArgs) {
  base::StringPairs scan_options = GetScanOptions({});
  EXPECT_EQ(scan_options.size(), 0);
}

TEST(GetScanOptions, NoSetOptions) {
  base::StringPairs scan_options = GetScanOptions({"scan"});
  EXPECT_EQ(scan_options.size(), 0);
}

TEST(GetScanOptions, NoOptionArgs) {
  base::StringPairs scan_options = GetScanOptions({"set_options", "end"});
  EXPECT_EQ(scan_options.size(), 0);
}

TEST(GetScanOptions, DashTerminatesSettings) {
  base::StringPairs scan_options = GetScanOptions({"set_options", "-arg=val"});
  EXPECT_EQ(scan_options.size(), 0);
}

TEST(GetScanOptions, OptionsWithEmptyKeyNotAllowed) {
  base::StringPairs scan_options = GetScanOptions({"set_options", "=value1"});
  EXPECT_EQ(scan_options.size(), 0);
}

TEST(GetScanOptions, OptionsWithEmptyValuePermitted) {
  base::StringPairs scan_options = GetScanOptions({"set_options", "option1="});
  ASSERT_EQ(scan_options.size(), 1);
  auto expected = std::pair<std::string, std::string>("option1", "");
  EXPECT_EQ(scan_options[0], expected);
}

TEST(GetScanOptions, OptionsAndOtherArgs) {
  base::StringPairs scan_options = GetScanOptions(
      {"-arg1=yes", "set_options", "option1=value=3", "end", "-arg2=no"});
  ASSERT_EQ(scan_options.size(), 1);
  auto expected = std::pair<std::string, std::string>("option1", "value=3");
  EXPECT_EQ(scan_options[0], expected);
}

TEST(MakeSetOptionsRequest, NoOptions) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {});
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->scanner(), EqualsProto(config.scanner()));
  EXPECT_EQ(request->options_size(), 0);
}

TEST(MakeSetOptionsRequest, UnknownOption) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {{"bad-option", "val"}});
  EXPECT_FALSE(request.has_value());
}

TEST(MakeSetOptionsRequest, InvalidBool) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {{"bool-option", "val"}});
  EXPECT_FALSE(request.has_value());
}

TEST(MakeSetOptionsRequest, ValidBool) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {
                                        {"bool-option", "true"},
                                        {"bool-option", "1"},
                                        {"bool-option", "yes"},
                                        {"bool-option", "false"},
                                        {"bool-option", "0"},
                                        {"bool-option", "no"},
                                    });
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->scanner(), EqualsProto(config.scanner()));
  ASSERT_EQ(request->options_size(), 6);
  EXPECT_EQ(request->options().at(0).bool_value(), true);
  EXPECT_EQ(request->options().at(1).bool_value(), true);
  EXPECT_EQ(request->options().at(2).bool_value(), true);
  EXPECT_EQ(request->options().at(3).bool_value(), false);
  EXPECT_EQ(request->options().at(4).bool_value(), false);
  EXPECT_EQ(request->options().at(5).bool_value(), false);
}

TEST(MakeSetOptionsRequest, InvalidInt) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {{"int-option", "1.0"}});
  EXPECT_FALSE(request.has_value());
}

TEST(MakeSetOptionsRequest, ValidInt) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {
                                        {"int-option", "1"},
                                        {"int-option", "-11"},
                                        {"int-option", "1,2"},
                                        {"int-option", " 2 , -1 "},
                                        {"int-option", "0"},
                                    });
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->scanner(), EqualsProto(config.scanner()));
  ASSERT_EQ(request->options_size(), 5);
  ASSERT_EQ(request->options().at(0).int_value().value_size(), 1);
  EXPECT_EQ(request->options().at(0).int_value().value().at(0), 1);
  ASSERT_EQ(request->options().at(1).int_value().value_size(), 1);
  EXPECT_EQ(request->options().at(1).int_value().value().at(0), -11);
  ASSERT_EQ(request->options().at(2).int_value().value_size(), 2);
  EXPECT_EQ(request->options().at(2).int_value().value().at(0), 1);
  EXPECT_EQ(request->options().at(2).int_value().value().at(1), 2);
  ASSERT_EQ(request->options().at(3).int_value().value_size(), 2);
  EXPECT_EQ(request->options().at(3).int_value().value().at(0), 2);
  EXPECT_EQ(request->options().at(3).int_value().value().at(1), -1);
  ASSERT_EQ(request->options().at(4).int_value().value_size(), 1);
  EXPECT_EQ(request->options().at(4).int_value().value().at(0), 0);
}

TEST(MakeSetOptionsRequest, InvalidFixed) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {{"fixed-option", "a"}});
  EXPECT_FALSE(request.has_value());
}

TEST(MakeSetOptionsRequest, ValidFixed) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {
                                        {"fixed-option", "1.5"},
                                        {"fixed-option", "-11.25"},
                                        {"fixed-option", "1.5,2"},
                                        {"fixed-option", " -2 , +1.5 "},
                                        {"fixed-option", "0.1"},
                                    });
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->scanner(), EqualsProto(config.scanner()));
  ASSERT_EQ(request->options_size(), 5);
  ASSERT_EQ(request->options().at(0).fixed_value().value_size(), 1);
  EXPECT_EQ(request->options().at(0).fixed_value().value().at(0), 1.5);
  ASSERT_EQ(request->options().at(1).fixed_value().value_size(), 1);
  EXPECT_EQ(request->options().at(1).fixed_value().value().at(0), -11.25);
  ASSERT_EQ(request->options().at(2).fixed_value().value_size(), 2);
  EXPECT_EQ(request->options().at(2).fixed_value().value().at(0), 1.5);
  EXPECT_EQ(request->options().at(2).fixed_value().value().at(1), 2.0);
  ASSERT_EQ(request->options().at(3).fixed_value().value_size(), 2);
  EXPECT_EQ(request->options().at(3).fixed_value().value().at(0), -2.0);
  EXPECT_EQ(request->options().at(3).fixed_value().value().at(1), 1.5);
  ASSERT_EQ(request->options().at(4).fixed_value().value_size(), 1);
  EXPECT_EQ(request->options().at(4).fixed_value().value().at(0), 0.1);
}

TEST(MakeSetOptionsRequest, ValidString) {
  lorgnette::ScannerConfig config = MakeScannerConfig();
  std::optional<lorgnette::SetOptionsRequest> request =
      MakeSetOptionsRequest(config, {
                                        {"string-option", ""},
                                        {"string-option", "abcde"},
                                    });
  ASSERT_TRUE(request.has_value());
  EXPECT_THAT(request->scanner(), EqualsProto(config.scanner()));
  ASSERT_EQ(request->options_size(), 2);
  EXPECT_EQ(request->options().at(0).string_value(), "");
  EXPECT_EQ(request->options().at(1).string_value(), "abcde");
}

}  // namespace
}  // namespace lorgnette::cli
