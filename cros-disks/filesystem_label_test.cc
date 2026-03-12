// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/filesystem_label.h"

#include <linux/limits.h>

#include <string>

#include <gtest/gtest.h>

namespace cros_disks {

namespace {

// A subset of known forbidden characters for testing
const char kForbiddenTestCharacters[] = {
    '*',  '?',  '.',  ',',    ';',    ':',    '/', '\\', '|',
    '+',  '=',  '<',  '>',    '[',    ']',    '"', '\'', '\t',
    '\v', '\r', '\n', '\x02', '\x10', '\x7f', '\0'};

};  // namespace

TEST(FilesystemLabelTest, ValidateVolumeLabel) {
  // Test long volume names
  EXPECT_EQ(LabelError::kLongName, ValidateVolumeLabel("ABCDEFGHIJKL", "vfat"));
  EXPECT_EQ(LabelError::kLongName,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOP", "exfat"));
  EXPECT_EQ(LabelError::kLongName,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG", "ntfs"));

  // Test volume name length limits
  EXPECT_EQ(LabelError::kSuccess, ValidateVolumeLabel("ABCDEFGHIJK", "vfat"));
  EXPECT_EQ(LabelError::kSuccess,
            ValidateVolumeLabel("ABCDEFGHIJKLMNO", "exfat"));
  EXPECT_EQ(LabelError::kSuccess,
            ValidateVolumeLabel("ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEF", "ntfs"));

  // Test unsupported file system type
  EXPECT_EQ(LabelError::kUnsupportedFilesystem,
            ValidateVolumeLabel("ABC", "nonexistent-fs"));
}

TEST(FilesystemLabelTest, Sanitize) {
  EXPECT_EQ(Sanitize(""), "_");
  EXPECT_EQ(Sanitize("A"), "A");
  EXPECT_EQ(Sanitize(".A/B/C.D"), "A_B_C.D");
  EXPECT_EQ(Sanitize("."), "_");
  EXPECT_EQ(Sanitize(".."), "_");
  EXPECT_EQ(Sanitize("..."), "_");
  EXPECT_EQ(Sanitize(".a"), "a");
  EXPECT_EQ(Sanitize("..a"), "a");
  EXPECT_EQ(Sanitize("./"), "_");
  EXPECT_EQ(Sanitize("../.."), "_..");
  EXPECT_EQ(Sanitize(std::string(PATH_MAX, 'x')), std::string(NAME_MAX, 'x'));
  EXPECT_EQ(Sanitize(std::string(PATH_MAX, '/')), std::string(NAME_MAX, '_'));
  EXPECT_EQ(Sanitize(std::string(PATH_MAX, '.')), "_");
  EXPECT_EQ(Sanitize(std::string(PATH_MAX, '.') + "a"), "a");

  // Multibyte UTF-8 sequence.
  const std::string smiley = "\xF0\x9F\x98\x80";
  EXPECT_EQ(Sanitize(std::string(NAME_MAX, 'x') + smiley),
            std::string(NAME_MAX, 'x'));
  EXPECT_EQ(Sanitize(std::string(NAME_MAX - 1, 'x') + smiley),
            std::string(NAME_MAX - 1, 'x'));
  EXPECT_EQ(Sanitize(std::string(NAME_MAX - 2, 'x') + smiley),
            std::string(NAME_MAX - 2, 'x'));
  EXPECT_EQ(Sanitize(std::string(NAME_MAX - 3, 'x') + smiley),
            std::string(NAME_MAX - 3, 'x'));
  EXPECT_EQ(Sanitize(std::string(NAME_MAX - 4, 'x') + smiley),
            std::string(NAME_MAX - 4, 'x') + smiley);
}

class FilesystemLabelCharacterTest
    : public ::testing::TestWithParam<const char*> {};

INSTANTIATE_TEST_SUITE_P(AsciiRange,
                         FilesystemLabelCharacterTest,
                         testing::Values("vfat", "exfat", "ntfs"));

TEST_P(FilesystemLabelCharacterTest, ValidateVolumeLabelCharacters) {
  const char* filesystem = GetParam();

  using LabelError::kInvalidCharacter;
  using LabelError::kSuccess;

  // Test allowed characters in volume name
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("AZaz09", filesystem));
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("!#$ %&()", filesystem));
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("@^_-`{}~", filesystem));

  for (char c : kForbiddenTestCharacters) {
    // Test forbidden characters in volume name
    EXPECT_EQ(kInvalidCharacter,
              ValidateVolumeLabel(std::string("ABC") + c, filesystem));
  }

  // Test volume name starting with '-'.
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("-", filesystem));
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("--", filesystem));
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("-V", filesystem));
  EXPECT_EQ(kSuccess, ValidateVolumeLabel("--help", filesystem));
}

}  // namespace cros_disks
