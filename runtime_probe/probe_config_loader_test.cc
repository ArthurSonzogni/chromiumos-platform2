// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "runtime_probe/probe_config_loader.h"

namespace runtime_probe {

namespace {

constexpr char kConfigName[] = "probe_config.json";
constexpr char kConfigHash[] = "0B6621DE5CDB0F805E614F19CAA6C38104F1F178";

base::FilePath GetTestDataPath() {
  char* src_env = std::getenv("SRC");
  CHECK(src_env != nullptr)
      << "Expect to have the envvar |SRC| set when testing.";
  return base::FilePath(src_env).Append("testdata");
}

}  // namespace

TEST(LoadProbeConfigDataFromFile, RelativePath) {
  const auto rel_file_path = GetTestDataPath().Append(kConfigName);
  const auto abs_file_path = base::MakeAbsoluteFilePath(rel_file_path);

  const auto probe_config = LoadProbeConfigDataFromFile(rel_file_path);
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path, abs_file_path);
  EXPECT_FALSE(probe_config->config.GetDict().empty());
  EXPECT_EQ(probe_config->sha1_hash, kConfigHash);
}

TEST(LoadProbeConfigDataFromFile, AbsolutePath) {
  const auto rel_file_path = GetTestDataPath().Append(kConfigName);
  const auto abs_file_path = base::MakeAbsoluteFilePath(rel_file_path);

  const auto probe_config = LoadProbeConfigDataFromFile(abs_file_path);
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path, abs_file_path);
  EXPECT_FALSE(probe_config->config.GetDict().empty());
  EXPECT_EQ(probe_config->sha1_hash, kConfigHash);
}

TEST(LoadProbeConfigDataFromFile, MissingFile) {
  const auto probe_config =
      LoadProbeConfigDataFromFile(base::FilePath{"missing_file.json"});
  EXPECT_FALSE(probe_config);
}

TEST(LoadProbeConfigDataFromFile, InvalidFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const base::FilePath rel_path{"invalid_config.json"};
  const char invalid_probe_config[] = "foo\nbar";
  PCHECK(WriteFile(temp_dir.GetPath().Append(rel_path), invalid_probe_config));

  const auto probe_config = LoadProbeConfigDataFromFile(rel_path);
  EXPECT_FALSE(probe_config);
}

TEST(LoadProbeConfigDataFromFile, SymbolicLink) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  const auto rel_file_path = GetTestDataPath().Append(kConfigName);
  const auto abs_file_path = base::MakeAbsoluteFilePath(rel_file_path);
  const auto symlink_config_path = temp_dir.GetPath().Append("config.json");
  PCHECK(base::CreateSymbolicLink(abs_file_path, symlink_config_path));

  const auto probe_config = LoadProbeConfigDataFromFile(symlink_config_path);
  EXPECT_TRUE(probe_config);
  EXPECT_EQ(probe_config->path, abs_file_path);
  EXPECT_FALSE(probe_config->config.GetDict().empty());
  EXPECT_EQ(probe_config->sha1_hash, kConfigHash);
}

}  // namespace runtime_probe
