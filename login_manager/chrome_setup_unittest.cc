// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/chrome_setup.h"

#include <set>

#include <base/bind.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <chromeos/ui/chromium_command_builder.h>
#include <gtest/gtest.h>

using chromeos::ui::ChromiumCommandBuilder;

namespace login_manager {

class ChromeSetupTest : public ::testing::Test {
 public:
  ChromeSetupTest() {}

 protected:
  // Two sizes are supported for the wallpaper flags.
  const std::vector<std::string> kSizes{"small", "large"};
  const std::string kNotPresent = "<not present>";
  const base::Callback<bool(const base::FilePath&)> kPathInSetCallback =
      base::Bind(&ChromeSetupTest::PathInSet, base::Unretained(this));

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
    return base::StringPrintf(
        "--%s-wallpaper-%s", flag_type.c_str(), size.c_str());
  }

  // Get the expected pathname for the given base name and size.
  std::string GetPath(const std::string& base, const std::string& size) {
    return base::StringPrintf("/usr/share/chromeos-assets/wallpaper/%s_%s.jpg",
                              base.c_str(),
                              size.c_str());
  }

  ChromiumCommandBuilder builder_;
  // Set of paths to report as existing.
  std::set<std::string> paths_{
      GetPath("default", "small"),
      GetPath("default", "large"),
      GetPath("child", "small"),
      GetPath("child", "large"),
      GetPath("guest", "small"),
      GetPath("guest", "large"),
  };
};

TEST_F(ChromeSetupTest, TestOem) {
  paths_.insert(GetPath("oem", "small"));
  paths_.insert(GetPath("oem", "large"));
  login_manager::SetUpWallpaperFlags(&builder_, kPathInSetCallback);
  std::vector<std::string> argv = builder_.arguments();
  ASSERT_EQ(5, argv.size());

  for (std::string size : kSizes) {
    EXPECT_EQ(GetPath("oem", size),
              GetFlag(argv, GetFlagName("default", size)));
    EXPECT_EQ(GetPath("guest", size),
              GetFlag(argv, GetFlagName("guest", size)));
  }
  EXPECT_EQ("", GetFlag(argv, "--default-wallpaper-is-oem"));
}

TEST_F(ChromeSetupTest, TestDefault) {
  login_manager::SetUpWallpaperFlags(&builder_, kPathInSetCallback);
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

}  // namespace login_manager
