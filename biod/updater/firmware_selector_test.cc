// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/firmware_selector.h"

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/types/cxx23_to_underlying.h>
#include <base/types/expected.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

namespace {

constexpr char kValidFirmwareName1[] = "dragonclaw_v2.2.110-b936c0a3c.bin";
constexpr char kValidFirmwareName2[] = "dragonclaw_v1.0.4-b936c0a3c.bin";

const base::FilePath kInitFilePath("/UNTOUCHED_PATH");
constexpr char kValidBoardName[] = "dragonclaw";

// (board_name, file_name)
// All |file_name|'s should be unique, so that tests can pull any
// combination of elements to test with.
// All |board_name|'s should be unique, so that tests can check for
// proper firmware name fetching when multiple valid firmwares are present.
const std::vector<std::pair<std::string, std::string>> kValidFirmwareNames = {
    std::make_pair("hatch_fp", "hatch_fp_v2.2.110-b936c0a3c.bin"),
    std::make_pair("dragonclaw", "dragonclaw_v1.0.4-b936c0a3c.bin"),
    std::make_pair("dragonguts", "dragonguts_v1.2.3-d00d8badf00d.bin"),
};

const std::vector<std::string> kInvalidFirmwareNames = {
    "nocturne_fp_v2.2.110-b936c0a3c.txt",
    "not_fpmcu_firmware.bin",
    "not_fpmcu_firmware.txt",
    "_fp_.txt",
    "file",
};

const std::vector<biod::updater::FirmwareSelector::FindFirmwareFileStatus>
    kFindFirmwareFileStatuses = {
        biod::updater::FirmwareSelector::FindFirmwareFileStatus::kNoDirectory,
        biod::updater::FirmwareSelector::FindFirmwareFileStatus::kFileNotFound,
        biod::updater::FirmwareSelector::FindFirmwareFileStatus::kMultipleFiles,
};

}  // namespace

namespace biod {
namespace updater {

using FindFirmwareFileStatus = FirmwareSelector::FindFirmwareFileStatus;

class FirmwareSelectorTest : public testing::Test {
 protected:
  bool TouchFile(const base::FilePath& abspath) const {
    base::File file(abspath,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    file.Close();

    EXPECT_TRUE(base::PathExists(abspath));
    return true;
  }

  bool RemoveFile(const base::FilePath& abspath) const {
    return brillo::DeletePathRecursively(abspath);
  }

  FirmwareSelectorTest() = default;
  ~FirmwareSelectorTest() override = default;
};

TEST_F(FirmwareSelectorTest, BetaFirmwareAvailableButNotAllowed) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  EXPECT_TRUE(CreateDirectory(temp_dir.GetPath().Append("beta")));

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());

  // Given a directory with production firmware and beta firmware,
  EXPECT_TRUE(
      TouchFile(temp_dir.GetPath().Append("beta").Append(kValidFirmwareName1)));
  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(kValidFirmwareName2)));

  // searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);

  // succeeds
  EXPECT_TRUE(status.has_value());
  // and returns the path to the production firmware file.
  EXPECT_EQ(status.value(), temp_dir.GetPath().Append(kValidFirmwareName2));
}

TEST_F(FirmwareSelectorTest, GoodBetaFirmware) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(".allow_beta_firmware")));
  EXPECT_TRUE(CreateDirectory(temp_dir.GetPath().Append("beta")));

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    base::FilePath fw_file_path;

    // Given a directory with one correctly named beta firmware file
    fw_file_path = temp_dir.GetPath().Append("beta").Append(firmware_name);
    EXPECT_TRUE(TouchFile(fw_file_path));

    // searching for a beta firmware file
    auto status = selector.FindFirmwareFile(board_name);
    // succeeds
    EXPECT_TRUE(status.has_value());
    // and returns the path to the beta firmware file.
    EXPECT_EQ(status.value(), fw_file_path);
  }
}

TEST_F(FirmwareSelectorTest, NoBetaFirmwareFallbackToProduction) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());
  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(".allow_beta_firmware")));
  EXPECT_TRUE(CreateDirectory(temp_dir.GetPath().Append("beta")));

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    base::FilePath fw_file_path;

    // Given a directory with one correctly named firmware file
    fw_file_path = temp_dir.GetPath().Append(firmware_name);
    EXPECT_TRUE(TouchFile(fw_file_path));

    // searching for a firmware file
    auto status = selector.FindFirmwareFile(board_name);
    // succeeds
    EXPECT_TRUE(status.has_value());
    // and returns the path to the production firmware file.
    EXPECT_EQ(status.value(), fw_file_path);
  }
}

TEST_F(FirmwareSelectorTest, InvalidPathBlank) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           base::FilePath(""));

  // Given an empty directory path, searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);
  // fails with a no directory error.
  EXPECT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), FindFirmwareFileStatus::kNoDirectory);
}

TEST_F(FirmwareSelectorTest, InvalidPathOddChars) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           base::FilePath("--"));

  // Given "--" as directory path, searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);
  // fails with a no directory error
  EXPECT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), FindFirmwareFileStatus::kNoDirectory);
}

TEST_F(FirmwareSelectorTest, DirectoryWithoutFirmware) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());

  // Given a directory with no firmware files, searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);
  // fails with a file not found error.
  EXPECT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), FindFirmwareFileStatus::kFileNotFound);
}

TEST_F(FirmwareSelectorTest, OneGoodFirmwareFilePattern) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    base::FilePath fw_file_path;

    // Given a directory with one correctly named firmware file
    fw_file_path = temp_dir.GetPath().Append(firmware_name);
    EXPECT_TRUE(TouchFile(fw_file_path));

    // searching for a firmware file
    auto status = selector.FindFirmwareFile(board_name);
    // succeeds
    EXPECT_TRUE(status.has_value());
    // and returns the path to the original firmware file.
    EXPECT_EQ(status.value(), fw_file_path);
  }
}

TEST_F(FirmwareSelectorTest, OneBadFirmwareFilePattern) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  for (const auto& bad_fw_name : kInvalidFirmwareNames) {
    base::FilePath fw_file_path;

    // Given a directory with one incorrectly named firmware file,
    fw_file_path = temp_dir.GetPath().Append(bad_fw_name);
    EXPECT_TRUE(TouchFile(fw_file_path));

    // searching for a firmware file
    auto status = selector.FindFirmwareFile(kValidBoardName);
    // fails with a file not found error.
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error(), FindFirmwareFileStatus::kFileNotFound);
  }
}

TEST_F(FirmwareSelectorTest, MultipleValidFiles) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  // Given a directory with multiple correctly named firmware files
  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(firmware_name)));
  }

  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    base::FilePath fw_file_path;

    // searching for a firmware file
    auto status = selector.FindFirmwareFile(board_name);
    // succeeds
    EXPECT_TRUE(status.has_value());
    // and returns the path to the corresponding firmware file.
    EXPECT_EQ(status.value(), temp_dir.GetPath().Append(firmware_name));
  }
}

TEST_F(FirmwareSelectorTest, MultipleValidFilesExceptSpecifc) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  // Given a directory with multiple correctly named firmware files and
  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(firmware_name)));
  }

  for (const auto& [board_name, firmware_name] : kValidFirmwareNames) {
    base::FilePath fw_file_path;
    const auto good_file_path = temp_dir.GetPath().Append(firmware_name);

    // but missing the board specific firmware file,
    EXPECT_TRUE(RemoveFile(good_file_path));

    // searching for a firmware file
    auto status = selector.FindFirmwareFile(board_name);
    // fails with a file not found error.
    EXPECT_FALSE(status.has_value());
    EXPECT_EQ(status.error(), FindFirmwareFileStatus::kFileNotFound);

    EXPECT_TRUE(TouchFile(good_file_path));
  }
}

TEST_F(FirmwareSelectorTest, MultipleFilesError) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());

  // Given a directory with two correctly named firmware files,
  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(kValidFirmwareName1)));
  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(kValidFirmwareName2)));

  // searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);

  // fails with a multiple files error.
  EXPECT_FALSE(status.has_value());
  EXPECT_EQ(status.error(), FindFirmwareFileStatus::kMultipleFiles);
}

TEST_F(FirmwareSelectorTest, OneGoodAndOneBadFirmwareFilePattern) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::updater::FirmwareSelector selector(temp_dir.GetPath(),
                                           temp_dir.GetPath());
  base::FilePath good_file_path =
      temp_dir.GetPath().Append(kValidFirmwareName1);
  base::FilePath bad_file_path =
      temp_dir.GetPath().Append(kInvalidFirmwareNames[0]);

  // Given a directory with one correctly named and one incorrectly named
  // firmware file,
  EXPECT_TRUE(TouchFile(good_file_path));
  EXPECT_TRUE(TouchFile(bad_file_path));

  // searching for a firmware file
  auto status = selector.FindFirmwareFile(kValidBoardName);
  // succeeds
  EXPECT_TRUE(status.has_value());
  // and returns the path to the single correctly named firmware file.
  EXPECT_EQ(status.value(), good_file_path);
}

TEST_F(FirmwareSelectorTest, NonblankStatusMessages) {
  // Given a FindFirmwareFile status
  for (auto status : kFindFirmwareFileStatuses) {
    // when we ask for the human readable string
    std::string msg = FirmwareSelector::FindFirmwareFileStatusToString(status);
    // expect it to not be "".
    EXPECT_FALSE(msg.empty()) << "Status " << base::to_underlying(status)
                              << " converts to a blank status string.";
  }
}

TEST_F(FirmwareSelectorTest, UniqueStatusMessages) {
  // Given a set of all FindFirmwareFile status messages
  std::unordered_set<std::string> status_msgs;
  for (auto status : kFindFirmwareFileStatuses) {
    status_msgs.insert(
        FirmwareSelector::FindFirmwareFileStatusToString(status));
  }

  // expect the set to contain the same number of unique messages
  // as there are original statuses.
  EXPECT_EQ(status_msgs.size(), kFindFirmwareFileStatuses.size())
      << "There are one or more non-unique status messages.";
}

}  // namespace updater
}  // namespace biod
