// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for crash reporting functionality.

#include "secanomalyd/reporter.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_split.h>
#include <brillo/process/process_mock.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "secanomalyd/mounts.h"
#include "secanomalyd/processes.h"

using ::testing::AnyOf;
using ::testing::Eq;
using ::testing::MatchesRegex;
using ::testing::Return;

namespace secanomalyd {

namespace {

constexpr int kWeight = 100;

constexpr char kUsrLocal[] = "/usr/local";

constexpr char kWxMountUsrLocal[] =
    "/dev/sda1 /usr/local ext4 "
    "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0";

constexpr char kWxMountUsrLocal_FullDescription[] =
    "/dev/sda1 /usr/local ext4 "
    "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered";

constexpr char kMounts[] =
    "/dev/root / ext2 rw,seclabel,relatime 0 0\n"
    //
    "proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0\n"
    //
    "tmpfs /run/namespaces tmpfs "
    "rw,seclabel,nosuid,nodev,noexec,relatime,mode=755 0 0\n"
    //
    "/dev/sdb1 /media/removable/USB\040Drive ext2 "
    "rw,dirsync,nosuid,nodev,noexec,seclabel,relatime,nosymfollow\n"
    //
    "fuse:/home/chronos/u-f0df208cd7759644d43f8d7c4c5900e4a4875275/MyFiles/"
    "Downloads/sample.rar /media/archive/sample.rar fuse.rarfs "
    "ro,dirsync,nosuid,nodev,noexec,relatime,nosymfollow,"
    "user_id=1000,group_id=1001,default_permissions,allow_other 0 0\n"
    //
    "/dev/sda1 /usr/local ext4 "
    "rw,seclabel,nodev,noatime,resgid=20119,commit=600,data=ordered 0 0";

struct Process {
  pid_t pid;
  pid_t ppid;
  ino_t pidns;
  ino_t mntns;
  ino_t userns;
  std::string comm;
  std::string args;
  ProcEntry::SandboxStatus sandbox;
};

std::map<std::string, Process> kProcesses = {
    {"init",
     {
         .pid = 1,
         .ppid = 0,
         .pidns = 4026531841,
         .mntns = 4026531836,
         .userns = 4026531837,
         .comm = "init",
         .args = "/sbin/init",
         .sandbox = 0b000000,
     }},
    {"agetty",
     {
         .pid = 471,
         .ppid = 1,
         .pidns = 4026531841,
         .mntns = 4026531836,
         .userns = 4026531837,
         .comm = "agetty",
         .args = "agetty 115200 ttyS0 linux",
         .sandbox = 0b000000,
     }},
    {"chrome",
     {
         .pid = 1111,
         .ppid = 1033,
         .pidns = 4026531841,
         .mntns = 4026531836,
         .userns = 4026531848,
         .comm = "chrome",
         .args = "/opt/google/chrome/chrome --type=renderer",
         .sandbox = 0b111010,
     }}};

constexpr char kInit_FullDescription[] =
    "init "
    "/sbin/init";
constexpr char kAgetty_FullDescription[] =
    "agetty "
    "agetty 115200 ttyS0 linux";

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

  std::string signature = GenerateMountSignature(wx_mounts);
  EXPECT_THAT(signature, MatchesRegex("-usr-local-[0-9A-F]{10}"));
}

TEST(SignatureTest, SignatureForRootMount) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace("/", "/dev/root / ext2 rw,seclabel,relatime 0 0");

  std::string signature = GenerateMountSignature(wx_mounts);
  EXPECT_THAT(signature, MatchesRegex("slashroot-[0-9A-F]{10}"));
}

TEST(SignatureTest, SignatureForMultipleMounts) {
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

  std::string signature = GenerateMountSignature(wx_mounts);
  EXPECT_THAT(signature,
              MatchesRegex("-media-removable-USB_Drive-[0-9A-F]{10}"));

  // Make sure the signature doesn't change when insertion order changes.
  wx_mounts.clear();
  wx_mounts.emplace(wx_usb.dest(), wx_usb);
  wx_mounts.emplace(wx_root.dest(), wx_root);
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  std::string new_signature = GenerateMountSignature(wx_mounts);
  ASSERT_THAT(new_signature,
              MatchesRegex("-media-removable-USB_Drive-[0-9A-F]{10}"));
  EXPECT_EQ(signature, new_signature);
}

TEST(SignatureTest, SignatureForPathWithHash) {
  MountEntryMap wx_mounts;

  MountEntry wx_shadow_root_1(
      "/dev/sda1 /home/root/deadbeef1234567890badbeef1234567890deadb ext4 "
      "rw,nosuid,nodev,noatime,nosymfollow");
  MountEntry wx_shadow_root_2(
      "/dev/sda1 /home/root/1234567890badbeefdeadbeef1234567890badbe ext4 "
      "rw,nosuid,nodev,noatime,nosymfollow");

  wx_mounts.emplace(wx_shadow_root_1.dest(), wx_shadow_root_1);
  std::string signature1 = GenerateMountSignature(wx_mounts);

  wx_mounts.clear();
  wx_mounts.emplace(wx_shadow_root_2.dest(), wx_shadow_root_2);
  std::string signature2 = GenerateMountSignature(wx_mounts);

  ASSERT_EQ(signature1, signature2);
}

TEST(SignatureTest, SignatureEmptyProcs) {
  ProcEntries procs;
  std::optional<std::string> signature = GenerateProcSignature(procs);
  EXPECT_EQ(signature, std::nullopt);
}

TEST(SignatureTest, SignatureForOneProc) {
  ProcEntries procs;
  Process proc = kProcesses["init"];
  procs.emplace_back(ProcEntry(proc.pid, proc.ppid, proc.pidns, proc.mntns,
                               proc.userns, proc.comm, proc.args,
                               proc.sandbox));
  std::optional<std::string> signature = GenerateProcSignature(procs);
  ASSERT_TRUE(signature.has_value());
  EXPECT_EQ(signature.value(), proc.comm);
}

TEST(SignatureTest, SignatureForMultipleProcs) {
  ProcEntries procs;
  Process init_proc = kProcesses["init"];
  Process agetty_proc = kProcesses["agetty"];
  procs.emplace_back(ProcEntry(
      init_proc.pid, init_proc.ppid, init_proc.pidns, init_proc.mntns,
      init_proc.userns, init_proc.comm, init_proc.args, init_proc.sandbox));
  procs.emplace_back(ProcEntry(agetty_proc.pid, agetty_proc.ppid,
                               agetty_proc.pidns, agetty_proc.mntns,
                               agetty_proc.userns, agetty_proc.comm,
                               agetty_proc.args, agetty_proc.sandbox));
  std::optional<std::string> signature = GenerateProcSignature(procs);

  ASSERT_TRUE(signature.has_value());
  // The signature is the comm value of a randomly chosen processes.
  EXPECT_THAT(signature.value(),
              AnyOf(Eq(init_proc.comm), Eq(agetty_proc.comm)));
}

// A simple W+X mount report will only contain one W+X mount and empty
// accompanying sections.
TEST(ReporterTest, SimpleWxMountReport) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);
  ProcEntries proc_entries;

  MaybeReport report = GenerateAnomalousSystemReport(
      wx_mounts, proc_entries, std::nullopt, std::nullopt);

  ASSERT_TRUE(report.has_value());

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      report.value(), "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // One signature line, one metadata line, four headers, one anomalous mount
  // and two "empty section" messages.
  ASSERT_EQ(lines.size(), 9u);

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
  EXPECT_EQ(std::string(lines[3]), "=== W+X mounts ===");
  EXPECT_EQ(std::string(lines[5]), "=== All mounts ===");
  EXPECT_EQ(std::string(lines[7]), "=== All processes ===");

  // Anomalous mount.
  EXPECT_EQ(std::string(lines[4]), kWxMountUsrLocal_FullDescription);

  // Empty sections.
  EXPECT_EQ(std::string(lines[6]), "Could not obtain mounts");
  EXPECT_EQ(std::string(lines[8]), "Could not obtain processes");
}

// A simple forbidden intersection report will only contain one violating
// process and empty accompanying sections.
TEST(ReporterTest, SimpleForbiddenIntersectionReport) {
  MountEntryMap wx_mounts;
  ProcEntries anomalous_procs;
  Process init_proc = kProcesses["init"];
  anomalous_procs.emplace_back(ProcEntry(
      init_proc.pid, init_proc.ppid, init_proc.pidns, init_proc.mntns,
      init_proc.userns, init_proc.comm, init_proc.args, init_proc.sandbox));

  MaybeReport report = GenerateAnomalousSystemReport(
      wx_mounts, anomalous_procs, std::nullopt, std::nullopt);

  ASSERT_TRUE(report.has_value());

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      report.value(), "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // One signature line, one metadata line, four headers, one anomalous
  // process and two "empty section" messages.
  ASSERT_EQ(lines.size(), 9u);

  // Signature.
  EXPECT_EQ(std::string(lines[0]), init_proc.comm);

  // Metadata.
  base::StringPairs kvpairs;
  ASSERT_TRUE(
      base::SplitStringIntoKeyValuePairs(lines[1], '\x01', '\x02', &kvpairs));
  for (const auto& kv : kvpairs) {
    if (kv.first == "signal") {
      // The anomaly was a forbidden intersection process so the signal is
      // "forbidden-intersection-process".
      EXPECT_EQ(kv.second, "forbidden-intersection-process");
    } else if (kv.first == "comm") {
      // Metadata 'dest' key matches signature.
      EXPECT_EQ(kv.second, init_proc.comm);
    }
  }

  // Headers.
  EXPECT_EQ(std::string(lines[2]), "=== Anomalous conditions ===");
  EXPECT_EQ(std::string(lines[3]), "=== Forbidden intersection processes ===");
  EXPECT_EQ(std::string(lines[5]), "=== All mounts ===");
  EXPECT_EQ(std::string(lines[7]), "=== All processes ===");

  // Anomalous mount.
  EXPECT_EQ(std::string(lines[4]), kInit_FullDescription);

  // Empty sections.
  EXPECT_EQ(std::string(lines[6]), "Could not obtain mounts");
  EXPECT_EQ(std::string(lines[8]), "Could not obtain processes");
}

// A full report will contain at least one instance of each anomaly type,
// complete with the accompanying sections for all mounts and all processes.
TEST(ReporterTest, FullReport) {
  MountEntryMap wx_mounts;
  wx_mounts.emplace(kUsrLocal, kWxMountUsrLocal);

  MaybeMountEntries all_mounts = ReadMountsFromString(kMounts);
  MaybeMountEntries uploadable_mounts = FilterPrivateMounts(all_mounts);

  ProcEntries anomalous_procs;
  Process init_proc = kProcesses["init"];
  Process agetty_proc = kProcesses["agetty"];
  anomalous_procs.emplace_back(ProcEntry(
      init_proc.pid, init_proc.ppid, init_proc.pidns, init_proc.mntns,
      init_proc.userns, init_proc.comm, init_proc.args, init_proc.sandbox));
  anomalous_procs.emplace_back(
      ProcEntry(agetty_proc.pid, agetty_proc.ppid, agetty_proc.pidns,
                agetty_proc.mntns, agetty_proc.userns, agetty_proc.comm,
                agetty_proc.args, agetty_proc.sandbox));

  ProcEntries all_procs;
  Process chrome_proc = kProcesses["chrome"];
  all_procs.emplace_back(ProcEntry(
      init_proc.pid, init_proc.ppid, init_proc.pidns, init_proc.mntns,
      init_proc.userns, init_proc.comm, init_proc.args, init_proc.sandbox));
  all_procs.emplace_back(ProcEntry(agetty_proc.pid, agetty_proc.ppid,
                                   agetty_proc.pidns, agetty_proc.mntns,
                                   agetty_proc.userns, agetty_proc.comm,
                                   agetty_proc.args, agetty_proc.sandbox));
  all_procs.emplace_back(ProcEntry(chrome_proc.pid, chrome_proc.ppid,
                                   chrome_proc.pidns, chrome_proc.mntns,
                                   chrome_proc.userns, chrome_proc.comm,
                                   chrome_proc.args, chrome_proc.sandbox));
  MaybeProcEntries maybe_procs = MaybeProcEntries(all_procs);

  MaybeReport report = GenerateAnomalousSystemReport(
      wx_mounts, anomalous_procs, uploadable_mounts, maybe_procs);

  ASSERT_TRUE(report.has_value());

  std::vector<base::StringPiece> lines = base::SplitStringPiece(
      report.value(), "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  // One signature line, one metadata line, four headers, one W+X mount
  // anomaly, four mounts, two process anomalies and three processes.
  ASSERT_EQ(lines.size(), 17u);

  // Signature.
  EXPECT_THAT(std::string(lines[0]), MatchesRegex("-usr-local-[0-9A-F]{10}"));

  // Metadata.
  base::StringPairs kvpairs;
  ASSERT_TRUE(
      base::SplitStringIntoKeyValuePairs(lines[1], '\x01', '\x02', &kvpairs));
  for (const auto& kv : kvpairs) {
    if (kv.first == "signal") {
      // Both anomaly types are present so the signal is "multiple-anomalies".
      EXPECT_EQ(kv.second, "multiple-anomalies");
    } else if (kv.first == "dest") {
      EXPECT_EQ(kv.second, "/usr/local");
    }
  }

  // Headers.
  EXPECT_EQ(std::string(lines[2]), "=== Anomalous conditions ===");
  EXPECT_EQ(std::string(lines[3]), "=== W+X mounts ===");
  EXPECT_EQ(std::string(lines[5]), "=== Forbidden intersection processes ===");
  EXPECT_EQ(std::string(lines[8]), "=== All mounts ===");
  EXPECT_EQ(std::string(lines[13]), "=== All processes ===");

  // Anomalous mount.
  EXPECT_EQ(std::string(lines[4]), kWxMountUsrLocal_FullDescription);

  // Anomalous processes.
  EXPECT_EQ(std::string(lines[6]), kInit_FullDescription);
  EXPECT_EQ(std::string(lines[7]), kAgetty_FullDescription);

  // Actual mounts.
  EXPECT_EQ(std::string(lines[9]), "/dev/root / ext2 rw,seclabel,relatime");
  EXPECT_EQ(std::string(lines[10]),
            "proc /proc proc rw,nosuid,nodev,noexec,relatime");
  EXPECT_EQ(std::string(lines[11]),
            "tmpfs /run/namespaces tmpfs "
            "rw,seclabel,nosuid,nodev,noexec,relatime,mode=755");
  EXPECT_EQ(std::string(lines[12]), kWxMountUsrLocal_FullDescription);

  // Actual processes.
  EXPECT_EQ(std::string(lines[14]), init_proc.args);
  EXPECT_EQ(std::string(lines[15]), agetty_proc.args);
  EXPECT_EQ(std::string(lines[16]), chrome_proc.args);
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
  EXPECT_CALL(*crash_reporter, Wait()).WillOnce(Return(0));

  EXPECT_TRUE(SendReport("This is a report", crash_reporter.get(), kWeight,
                         true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  std::ignore = dev_null.release();
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

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(), kWeight,
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

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(), kWeight,
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

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(), kWeight,
                          true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  std::ignore = dev_zero.release();
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
  EXPECT_CALL(*crash_reporter, Wait()).WillOnce(Return(1));

  EXPECT_FALSE(SendReport("This is a report", crash_reporter.get(), kWeight,
                          true /*report_in_dev_mode*/));

  // SendReport() puts the subprocess' stdin fd into a scoper class, so it's
  // been closed by the time SendReport() returns.
  std::ignore = dev_null.release();
}

}  // namespace secanomalyd
