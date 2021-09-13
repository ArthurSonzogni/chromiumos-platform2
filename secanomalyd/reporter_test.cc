// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for crash reporting functionality.

#include "secanomalyd/reporter.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <memory>
#include <vector>

#include <base/macros.h>
#include <base/optional.h>
#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_split.h>
#include <brillo/process/process_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::MatchesRegex;
using ::testing::Return;

namespace secanomalyd {

namespace {

constexpr char kUsrLocal[] = "/usr/local";

constexpr char kWxMountUsrLocal[] =
    "/dev/sda1 /usr/local ext4 "
    "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0";

base::ScopedFD GetDevNullFd() {
  return base::ScopedFD(HANDLE_EINTR(open("/dev/null", O_WRONLY)));
}

base::ScopedFD GetDevZeroFd() {
  return base::ScopedFD(HANDLE_EINTR(open("/dev/zero", O_RDONLY)));
}

}  // namespace

TEST(SignatureTest, SignatureForOneMount) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::string signature = GenerateSignature(wx_mounts);
  EXPECT_THAT(signature, MatchesRegex("-usr-local-[0-9A-F]{10}"));
}

TEST(SignatureTest, SignatureForThreeMounts) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  MountEntry wx_root(
      "/dev/sda1 /mnt/stateful_partition ext4 "
      "rw,seclabel,nosuid,nodev,noatime,"
      "resgid=20119,commit=600,data=ordered 0 0");
  MountEntry wx_usb(
      "/dev/sdb1 /media/removable/USB_Drive ext2 "
      "rw,dirsync,nosuid,nodev,seclabel,relatime,nosymfollow");

  wx_mounts.emplace(wx_root.dest(), wx_root);
  wx_mounts.emplace(wx_usb.dest(), wx_usb);

  std::string signature = GenerateSignature(wx_mounts);
  EXPECT_THAT(signature,
              MatchesRegex("-media-removable-USB_Drive-[0-9A-F]{10}"));

  // Make sure the signature doesn't change when insertion order changes.
  wx_mounts.clear();
  wx_mounts.emplace(wx_usb.dest(), wx_usb);
  wx_mounts.emplace(wx_root.dest(), wx_root);
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::string new_signature = GenerateSignature(wx_mounts);
  ASSERT_THAT(new_signature,
              MatchesRegex("-media-removable-USB_Drive-[0-9A-F]{10}"));
  EXPECT_EQ(signature, new_signature);
}

// The simplest report will only contain one anomalous condition and empty
// accompanying sections.
TEST(ReporterTest, SimplestReport) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  MaybeReport report =
      GenerateAnomalousSystemReport(wx_mounts, base::nullopt, base::nullopt);

  ASSERT_TRUE(report.has_value());

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      report.value(), "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // One signature line, one metadata line, three headers, one anomaly,
  // two "empty section" messages.
  ASSERT_EQ(lines.size(), 8u);

  // Signature.
  EXPECT_THAT(std::string(lines[0]), MatchesRegex("-usr-local-[0-9A-F]{10}"));

  // Metadata.
  base::StringPairs kvpairs;
  ASSERT_TRUE(
      base::SplitStringIntoKeyValuePairs(lines[1], '\x01', '\x02', &kvpairs));
  for (const auto& kv : kvpairs) {
    if (kv.first == "signal") {
      // The anomaly was a writable+executable mount so the signal is
      // "wx-mount".
      EXPECT_EQ(kv.second, "wx-mount");
    } else if (kv.first == "dest") {
      // Metadata 'dest' key matches signature.
      EXPECT_EQ(kv.second, "/usr/local");
    }
  }

  // Headers.
  EXPECT_EQ(std::string(lines[2]), "=== Anomalous conditions ===");
  EXPECT_EQ(std::string(lines[4]), "=== Mounts ===");
  EXPECT_EQ(std::string(lines[6]), "=== Processes ===");

  // Anomalous mount.
  EXPECT_EQ(std::string(lines[3]), "/dev/sda1 /usr/local ext4");

  // Empty sections.
  EXPECT_EQ(std::string(lines[5]), "Could not obtain mounts");
  EXPECT_EQ(std::string(lines[7]), "Could not obtain processes");
}

TEST(ReporterTest, CrashReporterSuceeds) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::unique_ptr<brillo::ProcessMock> crash_reporter(
      new brillo::ProcessMock());

  base::ScopedFD dev_null = GetDevNullFd();

  EXPECT_CALL(*crash_reporter, Start()).WillOnce(Return(true));
  EXPECT_CALL(*crash_reporter,
              RedirectUsingPipe(STDIN_FILENO, true /*is_input*/));
  EXPECT_CALL(*crash_reporter, GetPipe(STDIN_FILENO))
      .WillOnce(Return(dev_null.get()));
  EXPECT_CALL(*crash_reporter, Wait()).WillOnce(Return(true));

  EXPECT_TRUE(SendReport("This is a report", crash_reporter.get(),
                         true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  ignore_result(dev_null.release());
}

TEST(ReporterTest, StartFails) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::unique_ptr<brillo::ProcessMock> crash_reporter(
      new brillo::ProcessMock());

  base::ScopedFD dev_null = GetDevNullFd();

  EXPECT_CALL(*crash_reporter, Start()).WillOnce(Return(false));
  EXPECT_CALL(*crash_reporter,
              RedirectUsingPipe(STDIN_FILENO, true /*is_input*/));

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(),
                          true /*report_in_dev_mode*/));
}

TEST(ReporterTest, GetPipeFails) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::unique_ptr<brillo::ProcessMock> crash_reporter(
      new brillo::ProcessMock());

  EXPECT_CALL(*crash_reporter, Start()).WillOnce(Return(true));
  EXPECT_CALL(*crash_reporter,
              RedirectUsingPipe(STDIN_FILENO, true /*is_input*/));
  // Return -1 which is the error value for GetPipe().
  EXPECT_CALL(*crash_reporter, GetPipe(STDIN_FILENO)).WillOnce(Return(-1));

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(),
                          true /*report_in_dev_mode*/));
}

TEST(ReporterTest, WriteFileDescriptorFails) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::unique_ptr<brillo::ProcessMock> crash_reporter(
      new brillo::ProcessMock());

  base::ScopedFD dev_zero = GetDevZeroFd();

  EXPECT_CALL(*crash_reporter, Start()).WillOnce(Return(true));
  EXPECT_CALL(*crash_reporter,
              RedirectUsingPipe(STDIN_FILENO, true /*is_input*/));
  // /dev/zero cannot be written to, so attempting to write the report will
  // fail.
  EXPECT_CALL(*crash_reporter, GetPipe(STDIN_FILENO))
      .WillOnce(Return(dev_zero.get()));

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(),
                          true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  ignore_result(dev_zero.release());
}

TEST(ReporterTest, WaitFails) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::unique_ptr<brillo::ProcessMock> crash_reporter(
      new brillo::ProcessMock());

  base::ScopedFD dev_null = GetDevNullFd();

  EXPECT_CALL(*crash_reporter, Start()).WillOnce(Return(true));
  EXPECT_CALL(*crash_reporter,
              RedirectUsingPipe(STDIN_FILENO, true /*is_input*/));
  EXPECT_CALL(*crash_reporter, GetPipe(STDIN_FILENO))
      .WillOnce(Return(dev_null.get()));
  EXPECT_CALL(*crash_reporter, Wait()).WillOnce(Return(true));

  EXPECT_TRUE(SendReport("This is a report", crash_reporter.get(),
                         true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  ignore_result(dev_null.release());
}

}  // namespace secanomalyd
