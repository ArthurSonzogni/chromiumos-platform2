// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/chrome_setup.h"

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/bind.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <chromeos/ui/chromium_command_builder.h>
#include <chromeos/ui/util.h>
#include <libsegmentation/feature_management.h>
#include <libsegmentation/feature_management_fake.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using chromeos::ui::ChromiumCommandBuilder;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::HasSubstr;

namespace login_manager {

class ChromeSetupTest : public ::testing::Test {
 public:
  ChromeSetupTest() {}

 protected:
  // Two sizes are supported for the wallpaper flags.
  const std::vector<std::string> kSizes{"small", "large"};
  const std::string kNotPresent = "<not present>";
  const std::string kModel = "reef";
  const std::string kDefaultDisplay = "default";
  const std::string kOldDisplay = "old";
  const std::string kShelfFlag = "--enable-dim-shelf";
  const std::string kFeatureFlag = "--enable-features";
  const base::RepeatingCallback<bool(const base::FilePath&)>
      kPathInSetCallback = base::BindRepeating(&ChromeSetupTest::PathInSet,
                                               base::Unretained(this));

  void SetUp() override {
    auto fake = std::make_unique<segmentation::fake::FeatureManagementFake>();
    fake_feature_management_ = fake.get();
    feature_management_ =
        std::make_unique<segmentation::FeatureManagement>(std::move(fake));
  }

  // This returns true if the path is found in the paths_ set.
  bool PathInSet(const base::FilePath& path) {
    return paths_.count(path.MaybeAsASCII()) != 0;
  }

  // Returns the value of the given flag [name], by looking it up in [args].
  // Note that the value can be missing, in which case "" is returned.
  // If the flag is not present in the list, returns kNotPresent.
  std::string GetFlag(const std::vector<std::string>& args,
                      const std::string& name) {
    for (auto arg : args) {
      std::vector<std::string> tokens =
          SplitString(arg, "=", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
      CHECK_LE(tokens.size(), 2U);
      if (tokens[0] == name)
        return tokens.size() == 1 ? "" : tokens[1];
    }
    return kNotPresent;
  }

  // Get the name of the wallpaper flag for the given flag type and size.
  std::string GetFlagName(const std::string& flag_type,
                          const std::string& size) {
    return base::StringPrintf("--%s-wallpaper-%s", flag_type.c_str(),
                              size.c_str());
  }

  // Get the expected pathname for the given base name and size.
  std::string GetPath(const std::string& base, const std::string& size) {
    return base::StringPrintf("/usr/share/chromeos-assets/wallpaper/%s_%s.jpg",
                              base.c_str(), size.c_str());
  }

  ChromiumCommandBuilder builder_;
  // Set of paths to report as existing.
  std::set<std::string> paths_{
      GetPath("default", "small"), GetPath("default", "large"),
      GetPath("child", "small"),   GetPath("child", "large"),
      GetPath("guest", "small"),   GetPath("guest", "large"),
  };
  brillo::FakeCrosConfig cros_config_;

  std::unique_ptr<segmentation::FeatureManagement> feature_management_;
  segmentation::fake::FeatureManagementFake* fake_feature_management_;
};

TEST_F(ChromeSetupTest, TestSerializedAshSwitches) {
  using std::string_literals::operator""s;
  cros_config_.SetString("/ui", "serialized-ash-switches",
                         "--foo\0--bar-baz=bam\0--bip\0"s);
  login_manager::AddSerializedAshSwitches(&builder_, &cros_config_);
  auto argv = builder_.arguments();
  EXPECT_THAT(argv,
              testing::UnorderedElementsAre("--foo", "--bar-baz=bam", "--bip"));
}

TEST_F(ChromeSetupTest, TestNNPalmEmpty) {
  login_manager::SetUpOzoneNNPalmPropertiesFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  EXPECT_THAT(argv,
              testing::UnorderedElementsAre("--ozone-nnpalm-properties={}"));
}

TEST_F(ChromeSetupTest, TestNNPalmFilled) {
  cros_config_.SetString(login_manager::kOzoneNNPalmPropertiesPath,
                         login_manager::kOzoneNNPalmCompatibleProperty, "true");
  cros_config_.SetString(login_manager::kOzoneNNPalmPropertiesPath,
                         login_manager::kOzoneNNPalmRadiusProperty, "0.1, 1.5");
  login_manager::SetUpOzoneNNPalmPropertiesFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  EXPECT_THAT(argv, testing::UnorderedElementsAre(
                        "--ozone-nnpalm-properties={\"radius-polynomial\":\"0."
                        "1, 1.5\",\"touch-compatible\":\"true\"}"));
}

TEST_F(ChromeSetupTest, TestOem) {
  paths_.insert(GetPath("oem", "small"));
  paths_.insert(GetPath("oem", "large"));
  login_manager::SetUpWallpaperFlags(&builder_, nullptr, kPathInSetCallback);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(7, argv.size());

  for (std::string size : kSizes) {
    EXPECT_EQ(GetPath("oem", size),
              GetFlag(argv, GetFlagName("default", size)));
    EXPECT_EQ(GetPath("child", size),
              GetFlag(argv, GetFlagName("child", size)));
    EXPECT_EQ(GetPath("guest", size),
              GetFlag(argv, GetFlagName("guest", size)));
  }
  EXPECT_EQ("", GetFlag(argv, "--default-wallpaper-is-oem"));
}

TEST_F(ChromeSetupTest, TestDefault) {
  login_manager::SetUpWallpaperFlags(&builder_, nullptr, kPathInSetCallback);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(6, argv.size());
  for (std::string size : kSizes) {
    EXPECT_EQ(GetPath("default", size),
              GetFlag(argv, GetFlagName("default", size)));
    EXPECT_EQ(GetPath("child", size),
              GetFlag(argv, GetFlagName("child", size)));
    EXPECT_EQ(GetPath("guest", size),
              GetFlag(argv, GetFlagName("guest", size)));
  }
  EXPECT_EQ(kNotPresent, GetFlag(argv, "--default-wallpaper-is-oem"));
}

TEST_F(ChromeSetupTest, TestModelDoesNotExist) {
  cros_config_.SetString("/", login_manager::kWallpaperProperty, kModel);
  login_manager::SetUpWallpaperFlags(&builder_, &cros_config_,
                                     kPathInSetCallback);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(6, argv.size());
  for (std::string size : kSizes) {
    EXPECT_EQ(GetPath("default", size),
              GetFlag(argv, GetFlagName("default", size)));
    EXPECT_EQ(GetPath("child", size),
              GetFlag(argv, GetFlagName("child", size)));
    EXPECT_EQ(GetPath("guest", size),
              GetFlag(argv, GetFlagName("guest", size)));
  }
  EXPECT_EQ(kNotPresent, GetFlag(argv, "--default-wallpaper-is-oem"));
}

TEST_F(ChromeSetupTest, TestModelExists) {
  cros_config_.SetString("/", login_manager::kWallpaperProperty, kModel);
  paths_.insert(GetPath(kModel, "large"));
  paths_.insert(GetPath(kModel, "small"));
  login_manager::SetUpWallpaperFlags(&builder_, &cros_config_,
                                     kPathInSetCallback);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(7, argv.size());
  for (std::string size : kSizes) {
    EXPECT_EQ(GetPath(kModel, size),
              GetFlag(argv, GetFlagName("default", size)));
    EXPECT_EQ(GetPath("child", size),
              GetFlag(argv, GetFlagName("child", size)));
    EXPECT_EQ(GetPath("guest", size),
              GetFlag(argv, GetFlagName("guest", size)));
  }
  EXPECT_EQ("", GetFlag(argv, "--default-wallpaper-is-oem"));
}

TEST_F(ChromeSetupTest, TestPowerButtonPosition) {
  login_manager::SetUpPowerButtonPositionFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  const std::string kPowerButtonEdge = "left";
  cros_config_.SetString(login_manager::kPowerButtonPositionPath,
                         login_manager::kPowerButtonEdgeField,
                         kPowerButtonEdge);
  login_manager::SetUpPowerButtonPositionFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  // Add "--ash-power-button-position" flag only if both kPowerButtonEdgeField
  // and kPowerButtonPositionField are set correctly.
  const std::string kPowerButtonPosition = "0.3";
  cros_config_.SetString(login_manager::kPowerButtonPositionPath,
                         login_manager::kPowerButtonPositionField,
                         kPowerButtonPosition);
  login_manager::SetUpPowerButtonPositionFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  ASSERT_EQ(1, argv.size());
  base::Value::Dict position_info;
  position_info.Set(login_manager::kPowerButtonEdgeField,
                    std::move(kPowerButtonEdge));
  double position_as_double = 0;
  base::StringToDouble(kPowerButtonPosition, &position_as_double);
  position_info.Set(login_manager::kPowerButtonPositionField,
                    position_as_double);
  std::string json_position_info;
  base::JSONWriter::Write(position_info, &json_position_info);
  EXPECT_EQ(json_position_info, GetFlag(argv, "--ash-power-button-position"));
}

TEST_F(ChromeSetupTest, TestHelpContentSwitch) {
  login_manager::SetUpHelpContentSwitch(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  cros_config_.SetString("/ui", "help-content-id", "GOOGLE-EVE");
  login_manager::SetUpHelpContentSwitch(&builder_, &cros_config_);
  argv = builder_.arguments();
  EXPECT_EQ("GOOGLE-EVE", GetFlag(argv, "--device-help-content-id"));
}

TEST_F(ChromeSetupTest, TestRegulatoryLabel) {
  login_manager::SetUpRegulatoryLabelFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  cros_config_.SetString("/", login_manager::kRegulatoryLabelProperty, kModel);
  login_manager::SetUpRegulatoryLabelFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  EXPECT_EQ(kModel, GetFlag(argv, "--regulatory-label-dir"));
}

TEST_F(ChromeSetupTest, TestAutoDimNewDisplay) {
  login_manager::SetUpRegulatoryLabelFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  EXPECT_EQ(kNotPresent, GetFlag(argv, kShelfFlag));

  cros_config_.SetString(login_manager::kHardwarePropertiesPath,
                         login_manager::kDisplayCategoryField, kDefaultDisplay);
  login_manager::SetUpAutoDimFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  EXPECT_EQ(kNotPresent, GetFlag(argv, kShelfFlag));
}

TEST_F(ChromeSetupTest, TestAutoDimOldDisplay) {
  login_manager::SetUpRegulatoryLabelFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  EXPECT_EQ(kNotPresent, GetFlag(argv, kShelfFlag));

  cros_config_.SetString(login_manager::kHardwarePropertiesPath,
                         login_manager::kDisplayCategoryField, kOldDisplay);
  login_manager::SetUpAutoDimFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  EXPECT_EQ("", GetFlag(argv, kShelfFlag));
}

TEST_F(ChromeSetupTest, TestAllowAmbientEQ) {
  login_manager::SetUpAllowAmbientEQFlag(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  cros_config_.SetString(login_manager::kPowerPath,
                         login_manager::kAllowAmbientEQField, "0");
  login_manager::SetUpAllowAmbientEQFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  cros_config_.SetString(login_manager::kPowerPath,
                         login_manager::kAllowAmbientEQField, "1");
  login_manager::SetUpAllowAmbientEQFlag(&builder_, &cros_config_);
  argv = builder_.arguments();
  ASSERT_EQ(1, argv.size());
  ASSERT_EQ(login_manager::kAllowAmbientEQFeature, GetFlag(argv, kFeatureFlag));
}

TEST_F(ChromeSetupTest, TestSchedulerFlags) {
  constexpr char kBoostUrgentVal[] = "99";

  login_manager::SetUpSchedulerFlags(&builder_, &cros_config_);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(0, argv.size());

  cros_config_.SetString(login_manager::kSchedulerTunePath,
                         login_manager::kBoostUrgentProperty, kBoostUrgentVal);
  login_manager::SetUpSchedulerFlags(&builder_, &cros_config_);
  argv = builder_.arguments();
  EXPECT_EQ(kBoostUrgentVal, GetFlag(argv, "--scheduler-boost-urgent"));
}

TEST_F(ChromeSetupTest, TestAddFeatureManagementFlagEmpty) {
  fake_feature_management_->SetFeatureLevel(
      segmentation::FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_0);
  fake_feature_management_->SetMaxFeatureLevel(
      segmentation::FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1);
  fake_feature_management_->SetScopeLevel(
      segmentation::FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_0);

  login_manager::AddFeatureManagementFlags(&builder_,
                                           feature_management_.get());
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(3, argv.size());
  EXPECT_EQ("0", GetFlag(argv, "--feature-management-level"));
  EXPECT_EQ("1", GetFlag(argv, "--feature-management-max-level"));
  EXPECT_EQ("0", GetFlag(argv, "--feature-management-scope"));
}

TEST_F(ChromeSetupTest, TestAddFeatureManagementFlagNonEmpty) {
  std::string feat1 =
      base::StrCat({segmentation::FeatureManagement::kPrefix, "Feat1"});
  std::string feat2 =
      base::StrCat({segmentation::FeatureManagement::kPrefix, "Feat2"});

  fake_feature_management_->SetFeature(feat1, segmentation::USAGE_CHROME);
  fake_feature_management_->SetFeature(feat2, segmentation::USAGE_CHROME);
  fake_feature_management_->SetFeatureLevel(
      segmentation::FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_0);
  fake_feature_management_->SetMaxFeatureLevel(
      segmentation::FeatureManagementInterface::FeatureLevel::FEATURE_LEVEL_1);
  fake_feature_management_->SetScopeLevel(
      segmentation::FeatureManagementInterface::ScopeLevel::SCOPE_LEVEL_0);

  login_manager::AddFeatureManagementFlags(&builder_,
                                           feature_management_.get());
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(4, argv.size());
  std::vector<std::string> result =
      base::SplitString(GetFlag(argv, kFeatureFlag), ",", base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_ALL);
  EXPECT_THAT(result, testing::UnorderedElementsAre(feat1, feat2));
  EXPECT_EQ("0", GetFlag(argv, "--feature-management-level"));
  EXPECT_EQ("1", GetFlag(argv, "--feature-management-max-level"));
  EXPECT_EQ("0", GetFlag(argv, "--feature-management-scope"));
}

void InitWithUseFlag(std::optional<std::string> flag,
                     base::ScopedTempDir* temp_dir,
                     ChromiumCommandBuilder* builder) {
  ASSERT_TRUE(temp_dir->CreateUniqueTempDir());
  base::FilePath test_dir = temp_dir->GetPath();
  builder->set_base_path_for_testing(test_dir);
  base::FilePath use_flags_path = chromeos::ui::util::GetReparentedPath(
      ChromiumCommandBuilder::kUseFlagsPath, test_dir);
  base::File::Error error;
  CHECK(base::CreateDirectoryAndGetError(use_flags_path.DirName(), &error))
      << error;
  std::string flag_file_contents;
  if (flag) {
    flag_file_contents = *flag + "\n";
  }
  if (base::WriteFile(use_flags_path, flag_file_contents.c_str(),
                      flag_file_contents.length()) !=
      flag_file_contents.length()) {
    PLOG(FATAL) << "Could not write to " << use_flags_path.value() << ": ";
  }

  // Need a lsb-release file or Init will fail.
  base::FilePath lsb_path = chromeos::ui::util::GetReparentedPath(
      ChromiumCommandBuilder::kLsbReleasePath, test_dir);
  CHECK(base::CreateDirectoryAndGetError(lsb_path.DirName(), &error)) << error;
  if (base::WriteFile(lsb_path, "", 0) != 0) {
    PLOG(FATAL) << "Could not write to " << lsb_path.value() << ": ";
  }
  CHECK(builder->Init());
}

TEST(TestAddCrashHandlerFlag, Crashpad) {
  base::ScopedTempDir temp_dir;
  ChromiumCommandBuilder builder;
  InitWithUseFlag(std::nullopt, &temp_dir, &builder);
  AddCrashHandlerFlag(&builder);
  EXPECT_THAT(builder.arguments(), ElementsAre("--enable-crashpad"));
}

TEST(TestAddCrashHandlerFlag, Breakpad) {
  base::ScopedTempDir temp_dir;
  ChromiumCommandBuilder builder;
  InitWithUseFlag(std::string("force_breakpad"), &temp_dir, &builder);
  AddCrashHandlerFlag(&builder);
  EXPECT_THAT(builder.arguments(), ElementsAre("--no-enable-crashpad"));
}

}  // namespace login_manager
