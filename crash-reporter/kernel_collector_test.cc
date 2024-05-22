// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/kernel_collector_test.h"

#include <unistd.h>

#include <cinttypes>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/strcat.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/syslog_logging.h>
#include <gtest/gtest.h>

#include "crash-reporter/constants.h"
#include "crash-reporter/crash_collection_status.h"
#include "crash-reporter/test_util.h"

using base::FilePath;
using base::StrCat;
using base::StringPrintf;
using brillo::FindLog;
using brillo::GetLog;

namespace {

const int kMaxEfiParts = 100;
const int kMaxRamoopsIds = 2;

}  // namespace

class KernelCollectorTest : public ::testing::Test {
 protected:
  static constexpr const char kSuccessfulCollectContents[] =
      "<4>[  230.564891] something";

  void TestGetEfiCrashType(const char* driver_name);
  void TestLoadEfiCrash(const char* driver_name);
  void TestRemoveEfiCrash(const char* driver_name);
  void TestCollectEfiCrashFile(const char* driver_name);
  void SetUpSuccessfulCollect();
  void SetUpWatchdog0BootstatusInvalidNotInteger(const FilePath& path);
  void SetUpWatchdog0BootstatusUnknownInteger(const FilePath& path);
  void SetUpWatchdog0BootstatusCardReset(const FilePath& path);
  void SetUpWatchdog0BootstatusCardResetFanFault(const FilePath& path);
  void SetUpWatchdog0BootstatusNoResetFwHwReset(const FilePath& path);
  void SetUpSuccessfulWatchdog(const FilePath&);
  void WatchdogOptedOutHelper(const FilePath&);
  void WatchdogOKHelper(const FilePath&);
  void WatchdogOnlyLastBootHelper(const FilePath&);

  const FilePath& console_ramoops_file() const { return test_console_ramoops_; }
  const FilePath& eventlog_file() const { return test_eventlog_; }
  const FilePath& bios_log_file() const { return test_bios_log_; }
  const FilePath& ramoops_file(int id) const { return test_ramoops_[id]; }
  const FilePath& corrupt_ramoops_file() const { return test_corrupt_ramoops_; }
  const uint64_t efi_crash_id(int part) const {
    return (9876543210 * KernelCollector::EfiCrash::kMaxPart + part) *
               KernelCollector::EfiCrash::kMaxDumpRecord +
           1;
  }
  const FilePath efipstore_file(int part, const char* driver_name) const {
    return test_pstore_.Append(
        StringPrintf("dmesg-%s-%" PRIu64, driver_name, efi_crash_id(part)));
  }
  const FilePath& test_crash_directory() const { return test_crash_directory_; }
  const FilePath& bootstatus_file() const { return test_bootstatus_; }
  const FilePath& temp_dir() const { return scoped_temp_dir_.GetPath(); }

  KernelCollectorMock collector_;

 private:
  void SetUp() override {
    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_pstore_ = scoped_temp_dir_.GetPath().Append("test_pstore");
    ASSERT_TRUE(base::CreateDirectory(test_pstore_));

    test_console_ramoops_ = test_pstore_.Append("console-ramoops-0");
    ASSERT_FALSE(base::PathExists(test_console_ramoops_));

    test_corrupt_ramoops_ = test_pstore_.Append("dmesg-ramoops-0.enc.z");
    ASSERT_FALSE(base::PathExists(test_corrupt_ramoops_));
    for (int i = 0; i < kMaxRamoopsIds; ++i) {
      test_ramoops_[i] =
          test_pstore_.Append(StringPrintf("dmesg-ramoops-%d", i));
      ASSERT_FALSE(base::PathExists(test_ramoops_[i]));
    }

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append("crash_directory");
    ASSERT_TRUE(base::CreateDirectory(test_crash_directory_));

    test_eventlog_ = scoped_temp_dir_.GetPath().Append("eventlog.txt");
    ASSERT_FALSE(base::PathExists(test_eventlog_));

    // The watchdog sysfs directory structure is:
    // watchdogsys_path_ + "watchdogN/bootstatus"
    // Testing uses "watchdog0".
    FilePath test_watchdog0 = scoped_temp_dir_.GetPath().Append("watchdog0");
    ASSERT_TRUE(base::CreateDirectory(test_watchdog0));
    test_bootstatus_ = test_watchdog0.Append("bootstatus");
    ASSERT_FALSE(base::PathExists(test_bootstatus_));

    test_bios_log_ = scoped_temp_dir_.GetPath().Append("bios_log");
    ASSERT_FALSE(base::PathExists(test_bios_log_));

    brillo::ClearLog();

    // set up collector
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());
    collector_.Initialize(false);
    collector_.OverridePreservedDumpPath(test_pstore_);
    collector_.OverrideEventLogPath(test_eventlog_);
    collector_.OverrideWatchdogSysPath(scoped_temp_dir_.GetPath());
    collector_.OverrideBiosLogPath(test_bios_log_);
  }

  FilePath test_console_ramoops_;
  FilePath test_eventlog_;
  FilePath test_bios_log_;
  FilePath test_ramoops_[kMaxRamoopsIds];
  FilePath test_corrupt_ramoops_;
  FilePath test_pstore_;
  FilePath test_crash_directory_;
  FilePath test_bootstatus_;
  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(KernelCollectorTest, StringToPstoreRecordType) {
  EXPECT_EQ(KernelCollector::StringToPstoreRecordType("Oops"),
            PstoreRecordType::kOops);
  EXPECT_EQ(KernelCollector::StringToPstoreRecordType("Emergency"),
            PstoreRecordType::kEmergency);
  EXPECT_EQ(KernelCollector::StringToPstoreRecordType("Shutdown"),
            PstoreRecordType::kShutdown);
  EXPECT_EQ(KernelCollector::StringToPstoreRecordType("Unknown"),
            PstoreRecordType::kUnknown);
  EXPECT_EQ(KernelCollector::StringToPstoreRecordType("Bad Header"),
            PstoreRecordType::kParseFailed);
}

TEST_F(KernelCollectorTest, PstoreRecordTypeToString) {
  EXPECT_EQ(KernelCollector::PstoreRecordTypeToString(PstoreRecordType::kOops),
            "Oops");
  EXPECT_EQ(
      KernelCollector::PstoreRecordTypeToString(PstoreRecordType::kEmergency),
      "Emergency");
  EXPECT_EQ(
      KernelCollector::PstoreRecordTypeToString(PstoreRecordType::kShutdown),
      "Shutdown");
  EXPECT_EQ(
      KernelCollector::PstoreRecordTypeToString(PstoreRecordType::kUnknown),
      "Unknown");
  EXPECT_EQ(
      KernelCollector::PstoreRecordTypeToString(PstoreRecordType::kParseFailed),
      "ParseFailed");
  EXPECT_EQ(
      KernelCollector::PstoreRecordTypeToString(static_cast<PstoreRecordType>(
          static_cast<int>(PstoreRecordType::kParseFailed) + 1)),
      "Unknown enum");
}

TEST_F(KernelCollectorTest, ParseEfiCrashId) {
  uint64_t test_efi_crash_id = 150989600314002;
  EXPECT_EQ(1509896003,
            KernelCollector::EfiCrash::GetTimestamp(test_efi_crash_id));
  EXPECT_EQ(14, KernelCollector::EfiCrash::GetPart(test_efi_crash_id));
  EXPECT_EQ(2, KernelCollector::EfiCrash::GetCrashCount(test_efi_crash_id));
  EXPECT_EQ(test_efi_crash_id,
            KernelCollector::EfiCrash::GenerateId(1509896003, 14, 2));
}

void KernelCollectorTest::TestGetEfiCrashType(const char* driver_name) {
  ASSERT_FALSE(base::PathExists(efipstore_file(1, driver_name)));
  uint64_t test_efi_crash_id = efi_crash_id(1);
  // Write header.
  ASSERT_TRUE(
      test_util::CreateFile(efipstore_file(1, driver_name), "Panic#1 Part#20"));
  KernelCollector::EfiCrash efi_crash(test_efi_crash_id, driver_name,
                                      &collector_);
  EXPECT_EQ(efi_crash.GetType(), PstoreRecordType::kPanic);
}

TEST_F(KernelCollectorTest, GetEfiCrashType) {
  TestGetEfiCrashType("efi");
}

TEST_F(KernelCollectorTest, GetEfiPstoreCrashType) {
  TestGetEfiCrashType("efi_pstore");
}

void KernelCollectorTest::TestLoadEfiCrash(const char* driver_name) {
  int efi_part_count = kMaxEfiParts - 1;
  std::string efi_part[kMaxEfiParts];
  std::string expected_dump;
  std::string dump;
  uint64_t test_efi_crash_id = efi_crash_id(1);

  for (int i = 1; i <= efi_part_count; i++) {
    ASSERT_FALSE(base::PathExists(efipstore_file(i, driver_name)));
    efi_part[i] = StringPrintf("Panic#100 Part#%d\n", i);
    for (int j = 0; j < i; j++) {
      efi_part[i].append(StringPrintf("random blob %d\n", j));
    }
    ASSERT_TRUE(test_util::CreateFile(efipstore_file(i, driver_name),
                                      efi_part[i].c_str()));
  }
  KernelCollector::EfiCrash efi_crash(test_efi_crash_id, driver_name,
                                      &collector_);
  efi_crash.UpdateMaxPart(efi_crash.GetIdForPart(efi_part_count));
  ASSERT_TRUE(efi_crash.Load(dump));

  // Stitch parts in reverse order.
  for (int i = efi_part_count; i > 0; i--) {
    // Strip first line since it contains header.
    expected_dump.append(efi_part[i], efi_part[i].find('\n') + 1,
                         std::string::npos);
  }
  EXPECT_EQ(expected_dump, dump);
}

TEST_F(KernelCollectorTest, LoadEfiCrash) {
  TestLoadEfiCrash("efi");
}

TEST_F(KernelCollectorTest, LoadEfiPstoreCrash) {
  TestLoadEfiCrash("efi_pstore");
}

void KernelCollectorTest::TestRemoveEfiCrash(const char* driver_name) {
  int efi_part_count = kMaxEfiParts - 1;
  std::string efi_part[kMaxEfiParts];
  uint64_t test_efi_crash_id = efi_crash_id(1);

  for (int i = 1; i <= efi_part_count; i++) {
    ASSERT_FALSE(base::PathExists(efipstore_file(i, driver_name)));
    efi_part[i] = StringPrintf("Panic#100 Part#%d\n", i);
    for (int j = 0; j < i; j++) {
      efi_part[i].append(StringPrintf("random blob %d\n", j));
    }
    ASSERT_TRUE(test_util::CreateFile(efipstore_file(i, driver_name),
                                      efi_part[i].c_str()));
  }
  KernelCollector::EfiCrash efi_crash(test_efi_crash_id, driver_name,
                                      &collector_);
  efi_crash.UpdateMaxPart(efi_crash.GetIdForPart(efi_part_count));

  efi_crash.Remove();
  for (int i = 1; i <= efi_part_count; i++) {
    EXPECT_FALSE(base::PathExists(efipstore_file(i, driver_name)));
  }
}

TEST_F(KernelCollectorTest, RemoveEfiCrash) {
  TestRemoveEfiCrash("efi");
}

TEST_F(KernelCollectorTest, RemoveEfiPstoreCrash) {
  TestRemoveEfiCrash("efi_pstore");
}

TEST_F(KernelCollectorTest, CollectEfiCrashFilesMissing) {
  std::vector<CrashCollectionStatus> results;

  results = collector_.CollectEfiCrashes(/*use_saved_lsb=*/true);
  EXPECT_THAT(results,
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
}

void KernelCollectorTest::TestCollectEfiCrashFile(const char* driver_name) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  int efi_part_count = kMaxEfiParts - 1;
  std::string efi_part[kMaxEfiParts];

  for (int i = 1; i <= efi_part_count; i++) {
    ASSERT_FALSE(base::PathExists(efipstore_file(i, driver_name)));
    efi_part[i] = StringPrintf("Panic#2 Part#%d\n", i);
    if (i == 1) {
      efi_part[i].append(
          "<4>[  230.807132]  test_efi_function+0x48/0x238\n <remaining log "
          "chunk>");
    } else {
      for (int j = 0; j < i; j++) {
        efi_part[i].append(StringPrintf("random blob %d\n", j));
      }
    }
    ASSERT_TRUE(test_util::CreateFile(efipstore_file(i, driver_name),
                                      efi_part[i].c_str()));
  }

  std::vector<CrashCollectionStatus> results;
  results = collector_.CollectEfiCrashes(/*use_saved_lsb=*/true);
  EXPECT_THAT(results, testing::ElementsAre(CrashCollectionStatus::kSuccess));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.meta",
      "sig=kernel-test_efi_function-"));
  for (int i = 1; i <= efi_part_count; i++) {
    EXPECT_FALSE(base::PathExists(efipstore_file(i, driver_name)));
  }
}

TEST_F(KernelCollectorTest, CollectEfiCrashFile) {
  TestCollectEfiCrashFile("efi");
}

TEST_F(KernelCollectorTest, CollectEfiPstoreCrashFile) {
  TestCollectEfiCrashFile("efi_pstore");
}

TEST_F(KernelCollectorTest, GetRamoopsCrashType) {
  ASSERT_FALSE(base::PathExists(ramoops_file(0)));
  std::string type;
  // Write header.
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0), "Panic#4 Part#1"));
  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, false);
  EXPECT_EQ(ramoops_crash.GetType(), PstoreRecordType::kPanic);
}

TEST_F(KernelCollectorTest, GetCorruptRamoopsCrashType) {
  ASSERT_FALSE(base::PathExists(corrupt_ramoops_file()));
  std::string type;
  // Write random data.
  ASSERT_TRUE(test_util::CreateFile(corrupt_ramoops_file(),
                                    "\x45\x32\xab\xde\xf0\x0d"));
  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, true);
  EXPECT_EQ(ramoops_crash.GetType(), PstoreRecordType::kCorrupt);
}

TEST_F(KernelCollectorTest, LoadRamoopsCrash) {
  std::string expected_dump;
  std::string dump;
  std::string header;
  std::string contents;

  ASSERT_FALSE(base::PathExists(ramoops_file(0)));
  header = "Panic#10 Part#1\n";
  expected_dump = "random blob\nand some more data";
  contents.append(header);
  contents.append(expected_dump);
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0), contents));

  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, false);
  ASSERT_TRUE(ramoops_crash.Load(dump));

  EXPECT_EQ(expected_dump, dump);
}

TEST_F(KernelCollectorTest, LoadCorruptRamoopsCrash) {
  std::string expected_dump;
  std::string dump;
  std::string header;
  std::string contents;

  ASSERT_FALSE(base::PathExists(corrupt_ramoops_file()));
  expected_dump = "\x3\x45\x14\x41\x12\x58\xf3\xf7\xd0\xfe";
  ASSERT_TRUE(test_util::CreateFile(corrupt_ramoops_file(), expected_dump));

  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, true);
  ASSERT_TRUE(ramoops_crash.Load(dump));

  EXPECT_EQ(dump, StrCat({constants::kCorruptPstore, expected_dump}));
}

TEST_F(KernelCollectorTest, RemoveRamoopsCrash) {
  ASSERT_FALSE(base::PathExists(ramoops_file(0)));
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0),
                                    "Panic#10 Part#1\nblob data for panic"));

  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, false);
  ASSERT_TRUE(base::PathExists(ramoops_file(0)));

  ramoops_crash.Remove();
  EXPECT_FALSE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, RemoveCorruptRamoopsCrash) {
  ASSERT_FALSE(base::PathExists(corrupt_ramoops_file()));
  ASSERT_TRUE(test_util::CreateFile(
      corrupt_ramoops_file(), "\x14\x41\x12\x58\xf3\xf7\xd0\xf0\x37\x45\xed"));

  KernelCollector::RamoopsCrash ramoops_crash(0, &collector_, true);
  ASSERT_TRUE(base::PathExists(corrupt_ramoops_file()));

  ramoops_crash.Remove();
  EXPECT_FALSE(base::PathExists(corrupt_ramoops_file()));
}

TEST_F(KernelCollectorTest, ComputeKernelStackSignatureBase) {
  // Make sure the normal build architecture is detected
  EXPECT_NE(kernel_util::kArchUnknown, collector_.arch());
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashSkipOops) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  std::string contents = "<6>[    0.078852] some oops record";
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0),
                                    StrCat({"Oops#1 Part#10\n", contents})));

  // TODO(swboyd): Change expected collection status once oops are skipped.
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.kcrash", contents));
  EXPECT_FALSE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashSinglePanic) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(
      ramoops_file(0),
      StrCat({"Panic#2 Part#3\n", kSuccessfulCollectContents})));

  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.kcrash", kSuccessfulCollectContents));
  EXPECT_FALSE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashRandomBlob) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(
      test_util::CreateFile(ramoops_file(0), "\x01\x02\xfe\xff random blob"));

  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory(), "kernel.*.kcrash", nullptr));
  // TODO(swboyd): Change to expect false once corrupted files are still
  // removed.
  EXPECT_TRUE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashLarge) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  std::string large(1024 * 1024 + 1, 'x');  // 1MiB + 1 byte.
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0),
                                    StrCat({"Panic#1 Part#1\n", large})));

  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
  // TODO(swboyd): Change to expect false once large files are still removed.
  EXPECT_TRUE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashFile) {
  std::string kcrash_contents =
      "<4>[  303.619415]  test_ramoops_func+0x858/0x2338\n <remaining log "
      "chunk>";
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(
      ramoops_file(0), StrCat({"Panic#2 Part#1\n", kcrash_contents})));

  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.meta",
      "sig=kernel-test_ramoops_func-"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.kcrash", kcrash_contents));
  EXPECT_FALSE(base::PathExists(ramoops_file(0)));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashCorrupt) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  std::string contents = "A bunch of binary \x1\x2\x3\x4 ...";

  ASSERT_TRUE(test_util::CreateFile(corrupt_ramoops_file(), contents));

  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.meta", "sig=kernel-CorruptDump"));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "kernel.*.kcrash",
      StrCat({constants::kCorruptPstore, contents})));
  EXPECT_FALSE(base::PathExists(corrupt_ramoops_file()));
}

TEST_F(KernelCollectorTest, CollectRamoopsCrashOnlyRecordMatters) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // BIOS log is almost always present. Make one.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(), "coreboot-d041fe8 Sat Oct 20 bootblock starting...\n"));
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory(), "kernel.*.kcrash", nullptr));
}

TEST_F(KernelCollectorTest, LoadCorruptDump) {
  ASSERT_FALSE(base::PathExists(corrupt_ramoops_file()));
  std::string dump;
  dump.clear();

  ASSERT_TRUE(test_util::CreateFile(corrupt_ramoops_file(),
                                    "A bunch of binary \x1\x2\x3\x4 ..."));
  ASSERT_TRUE(collector_.LoadParameters());
  ASSERT_TRUE(collector_.LoadPreservedDump(&dump));

  ASSERT_EQ(
      StrCat({constants::kCorruptPstore, "A bunch of binary \x1\x2\x3\x4 ..."}),
      dump);

  ASSERT_FALSE(base::PathExists(corrupt_ramoops_file()));
}

TEST_F(KernelCollectorTest, LoadBiosLog) {
  std::string dump;
  dump.clear();

  std::string bootblock_boot_1 =
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock starting"
      " (log level: 7)...\n"
      "This is boot 1 bootblock!\n"
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 verstage starting"
      " (log level: 7)...\n"
      "This is boot 1 verstage!\n";
  std::string romstage_boot_1 =
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 romstage starting...\n"
      "This is boot 1 romstage!\n"
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 ramstage starting...\n"
      "This is boot 1 ramstage!\n"
      "\n\nStarting depthcharge on kevin...\n"
      "This is boot 1 depthcharge!\n"
      "jumping to kernel\n"
      "Some more messages logged at runtime, maybe without terminating newline";
  std::string bootblock_boot_2 =
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock starting...\n"
      "This is boot 2 bootblock!\n"
      "\n\ncoreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 verstage starting...\n"
      "This is boot 2 verstage!\n";
  std::string romstage_boot_2 =
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 romstage starting...\n"
      "This is boot 2 romstage!\n"
      "\n\ncoreboot-e8dd2d8 Tue Mar 14 23:29:43 UTC 2017 ramstage starting...\n"
      "This is boot 2 ramstage!\n"
      "\n\nStarting depthcharge on kevin...\n"
      "This is boot 2 depthcharge!\n"
      "jumping to kernel\n"
      "Some more messages logged at runtime, maybe without terminating newline";
  std::string bootblock_boot_with_marker =
      "\n\n\025coreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock starting"
      " (log level: 7)...\n"
      "\027This is a bootblock with loglevel marker byte!\n";
  std::string bootblock_boot_with_prefix =
      "\n\n[NOTE ]  coreboot-dc417eb Tue Nov 2 20:47:41 UTC 2016 bootblock"
      " starting (log level: 7)...\n"
      "[DEBUG]  This is a bootblock with full loglevel prefixes!\n";
  std::string bootblock_overflow =
      "\n*** Pre-CBMEM bootblock console overflowed, log truncated! ***\n"
      "[DEBUG]  This is a bootblock where the pre-CBMEM console overflowed!\n";
  std::string romstage_overflow =
      "\n*** Pre-CBMEM romstage console overflowed, log truncated! ***\n"
      "[DEBUG]  This is a romstage where the pre-CBMEM console overflowed!\n";

  // Normal situation of multiple boots in log.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      ("Some old lines from boot N-3\n" +    // N-3
       bootblock_boot_2 + romstage_boot_2 +  // N-2
       bootblock_boot_1 + romstage_boot_1 +  // N-1 (the "last" boot)
       bootblock_boot_2 + romstage_boot_2)   // N ("current" boot, after crash)
          .c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_boot_1 + romstage_boot_1, "\n" + dump);

  // Same on a board that cannot log pre-romstage.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      (romstage_boot_2 + romstage_boot_1 + romstage_boot_2).c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(romstage_boot_1, "\n" + dump);

  // Logs from previous boot were lost.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(), (bootblock_boot_1 + romstage_boot_1).c_str()));
  ASSERT_FALSE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ("", dump);

  // No recognizable BIOS log.
  ASSERT_TRUE(test_util::CreateFile(bios_log_file(), "random crud\n"));
  ASSERT_FALSE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ("", dump);

  // BIOS log with raw loglevel marker bytes at the start of each line.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      (bootblock_boot_with_marker + bootblock_boot_with_marker).c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_boot_with_marker, "\n" + dump);

  // BIOS log with full loglevel prefix strings at the start of each line.
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      (bootblock_boot_with_prefix + bootblock_boot_with_prefix).c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_boot_with_prefix, "\n" + dump);

  // BIOS log where bootblock overflowed but romstage is normal.
  ASSERT_TRUE(test_util::CreateFile(bios_log_file(),
                                    (bootblock_overflow + romstage_boot_1 +
                                     bootblock_overflow + romstage_boot_2)
                                        .c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_overflow + romstage_boot_1, "\n" + dump);

  // BIOS log where bootblock is normal but romstage overflowed.
  ASSERT_TRUE(test_util::CreateFile(bios_log_file(),
                                    (bootblock_boot_1 + romstage_overflow +
                                     bootblock_boot_2 + romstage_overflow)
                                        .c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_boot_1 + romstage_overflow, "\n" + dump);

  // BIOS log where both bootblock and romstage overflowed.
  ASSERT_TRUE(test_util::CreateFile(bios_log_file(),
                                    (bootblock_overflow + romstage_overflow +
                                     bootblock_overflow + romstage_overflow)
                                        .c_str()));
  ASSERT_TRUE(collector_.LoadLastBootBiosLog(&dump));
  ASSERT_EQ(bootblock_overflow + romstage_overflow, "\n" + dump);
}

TEST_F(KernelCollectorTest, EnableMissingKernel) {
  ASSERT_FALSE(collector_.Enable());
  ASSERT_FALSE(collector_.is_enabled());
  ASSERT_TRUE(FindLog("Kernel does not support crash dumping"));
}

TEST_F(KernelCollectorTest, EnableOK) {
  ASSERT_TRUE(test_util::CreateFile(ramoops_file(0), ""));
  EXPECT_CALL(collector_, DumpDirMounted()).WillOnce(::testing::Return(true));
  ASSERT_TRUE(collector_.Enable());
  ASSERT_TRUE(collector_.is_enabled());
  ASSERT_TRUE(FindLog("Enabling kernel crash handling"));
}

TEST_F(KernelCollectorTest, CollectPreservedFileMissing) {
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
  EXPECT_FALSE(FindLog("Stored kcrash to "));
}

TEST_F(KernelCollectorTest, CollectBadDirectory) {
  ASSERT_TRUE(test_util::CreateFile(
      ramoops_file(0),
      StrCat({"Panic#4 Part#42\n", kSuccessfulCollectContents})));

  EXPECT_THAT(
      collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
      testing::ElementsAre(CrashCollectionStatus::kCreateCrashDirectoryFailed));
  EXPECT_TRUE(FindLog("Unable to create crash directory"))
      << "Did not find expected error string in log: {\n"
      << GetLog() << "}";
}

void KernelCollectorTest::SetUpSuccessfulCollect() {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(
      ramoops_file(0),
      StrCat({"Panic#1 Part#1\n", kSuccessfulCollectContents})));
}

void KernelCollectorTest::SetUpWatchdog0BootstatusInvalidNotInteger(
    const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // Fill `bootstatus` with something other than an integer.
  ASSERT_TRUE(test_util::CreateFile(bootstatus_file(), "bad string\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

void KernelCollectorTest::SetUpWatchdog0BootstatusUnknownInteger(
    const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // Fill `bootstatus` with an unknown integer value, outside of good WDIOF_*.
  ASSERT_TRUE(test_util::CreateFile(bootstatus_file(), "268435456\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

void KernelCollectorTest::SetUpWatchdog0BootstatusCardReset(
    const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // WDIOF_CARDRESET = 0x0020 (32)
  ASSERT_TRUE(test_util::CreateFile(bootstatus_file(), "32\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

void KernelCollectorTest::SetUpWatchdog0BootstatusCardResetFanFault(
    const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // WDIOF_CARDRESET = 0x0020 (32)
  // WDIOF_FANFAULT = 0x0002 (2)
  ASSERT_TRUE(test_util::CreateFile(bootstatus_file(), "34\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

void KernelCollectorTest::SetUpWatchdog0BootstatusNoResetFwHwReset(
    const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  // 0: Normal boot
  ASSERT_TRUE(test_util::CreateFile(bootstatus_file(), "0\n"));
  ASSERT_TRUE(test_util::CreateFile(
      eventlog_file(),
      "112 | 2016-03-24 15:09:39 | System boot | 0\n"
      "113 | 2016-03-24 15:11:20 | System boot | 0\n"
      "114 | 2016-03-24 15:11:20 | Hardware watchdog reset\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

void KernelCollectorTest::SetUpSuccessfulWatchdog(const FilePath& path) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  ASSERT_TRUE(test_util::CreateFile(
      eventlog_file(),
      "112 | 2016-03-24 15:09:39 | System boot | 0\n"
      "113 | 2016-03-24 15:11:20 | System boot | 0\n"
      "114 | 2016-03-24 15:11:20 | Hardware watchdog reset\n"));
  ASSERT_TRUE(test_util::CreateFile(path, "\n[ 0.0000] I can haz boot!"));
}

TEST_F(KernelCollectorTest, CollectOK) {
  SetUpSuccessfulCollect();
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      "BIOS Messages"
      "\n\ncoreboot-dc417eb Tue Nov 2 bootblock starting...\n"));
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  static const char kNamePrefix[] = "Stored kcrash to ";
  std::string log = brillo::GetLog();
  size_t pos = log.find(kNamePrefix);
  ASSERT_NE(std::string::npos, pos)
      << "Did not find string \"" << kNamePrefix << "\" in log: {\n"
      << log << "}";
  pos += strlen(kNamePrefix);
  std::string filename = log.substr(pos, std::string::npos);
  // Take the name up until \n
  size_t end_pos = filename.find_first_of("\n");
  ASSERT_NE(std::string::npos, end_pos);
  filename = filename.substr(0, end_pos);
  ASSERT_EQ(0, filename.find(test_crash_directory().value()));
  FilePath path(filename);
  ASSERT_TRUE(base::PathExists(path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_EQ(kSuccessfulCollectContents, contents);
  // Check that BIOS log was collected as well.
  path = path.ReplaceExtension("bios_log");
  ASSERT_TRUE(base::PathExists(path));
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_EQ("BIOS Messages", contents);
  // Confirm that files are correctly described in .meta file.
  path = path.ReplaceExtension("meta");
  ASSERT_TRUE(base::PathExists(path));
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  ASSERT_TRUE(
      contents.find("payload=" +
                    path.ReplaceExtension("kcrash").BaseName().value()) !=
      std::string::npos);
  ASSERT_TRUE(
      contents.find("upload_file_bios_log=" +
                    path.ReplaceExtension("bios_log").BaseName().value()) !=
      std::string::npos);
}

TEST_F(KernelCollectorTest, LastRebootWasNoCError) {
  const char kNoCError[] =
      "QTISECLIB [05699b8d7]GEM_NOC ERROR: ERRLOG0_LOW = 0x0000010d\n"
      "QTISECLIB [05699bbfd]SYSTEM_NOC ERROR: ERRLOG1_LOW = 0x00000045\n"
      "QTISECLIB [05699bb44]CONFIG_NOC ERROR: ERRLOG1_LOW = 0x00000063\n"
      "QTISECLIB [05699bcc3]GEM_NOC ERROR: ERRLOG0_HIGH = 0x00000003\n"
      "QTISECLIB [05699be20]CONFIG_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699bd77]SYSTEM_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699bf82]CONFIG_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699becc]GEM_NOC ERROR: ERRLOG1_LOW = 0x00000013\n"
      "QTISECLIB [05699c0eb]CONFIG_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c036]SYSTEM_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699c259]CONFIG_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c2f3]SYSTEM_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c18a]GEM_NOC ERROR: ERRLOG1_HIGH = 0x00004626\n"
      "QTISECLIB [05699c48f]SYSTEM_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c47b]NOC error fatal\n"
      "QTISECLIB [05699c515]GEM_NOC ERROR: ERRLOG2_LOW = 0x00001000\n"
      "QTISECLIB [05699c653]NOC error fatal\n"
      "QTISECLIB [05699c70f]GEM_NOC ERROR: ERRLOG3_LOW = 0x00000028\n"
      "QTISECLIB [05699c828]GEM_NOC ERROR: SBM0 FAULTINSTATUS0_LOW = "
      "0x00000001\n"
      "QTISECLIB [05699c97e]NOC error fatal\n"
      "QTISECLIB [05699d606]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [0569b3675]SENDING NMI TO Q6 SUBSYSs!\n"
      "QTISECLIB [0569b367d]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [0569e1c48]SENDING NMI TO Q6 SUBSYSs!\n"
      "QTISECLIB [0569e1c4e]BL31 Error : NOC_ERROR\n"
      "QTISECLIB [056a101bb]SENDING NMI TO Q6 SUBSYSs!\n"
      "";

  ASSERT_TRUE(collector_.LastRebootWasNoCError(kNoCError));
}

void KernelCollectorTest::WatchdogOKHelper(const FilePath& path) {
  SetUpSuccessfulWatchdog(path);
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog("kernel-(WATCHDOG)-I can haz"));
}

TEST_F(KernelCollectorTest, BiosCrashArmOK) {
  collector_.set_crash_directory_for_test(test_crash_directory());
  collector_.set_arch(kernel_util::kArchArm);
  ASSERT_TRUE(test_util::CreateFile(
      bios_log_file(),
      "PANIC in EL3 at x30 = 0x00003698"
      "\n\ncoreboot-dc417eb Tue Nov 2 bootblock starting...\n"));
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog("bios-(PANIC)-0x00003698"));
}

TEST_F(KernelCollectorTest, Watchdog0BootstatusInvalidNotInteger) {
  const std::string signature = "kernel-(WATCHDOG)-";

  SetUpWatchdog0BootstatusInvalidNotInteger(console_ramoops_file());
  // Since the `bootstatus` file doesn't contain a valid integer, it can't
  // be parsed.
  EXPECT_THAT(
      collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
      testing::ElementsAre(CrashCollectionStatus::kCorruptWatchdogFile));
  ASSERT_TRUE(FindLog("Invalid bootstatus string"));
}

TEST_F(KernelCollectorTest, Watchdog0BootstatusInvalidUnknownInteger) {
  const std::string signature = "kernel-(UNKNOWN)-";

  SetUpWatchdog0BootstatusUnknownInteger(console_ramoops_file());
  // Collect a crash since the watchdog appears to have caused a reset,
  // we just don't know the reason why (yet).
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("unknown boot status value"));
  ASSERT_TRUE(FindLog(signature.c_str()));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "*.meta", signature));
}

TEST_F(KernelCollectorTest, Watchdog0BootstatusCardReset) {
  const std::string signature = "kernel-(WATCHDOG)-";

  SetUpWatchdog0BootstatusCardReset(console_ramoops_file());
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog(signature.c_str()));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "*.meta", signature));
}

TEST_F(KernelCollectorTest, Watchdog0BootstatusCardResetFanFault) {
  const std::string signature = "kernel-(FANFAULT)-(WATCHDOG)-";

  SetUpWatchdog0BootstatusCardResetFanFault(console_ramoops_file());
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog(signature.c_str()));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "*.meta", signature));
}

TEST_F(KernelCollectorTest, Watchdog0BootstatusNoResetFwHwReset) {
  const std::string signature = "kernel-(WATCHDOG)-";

  SetUpWatchdog0BootstatusNoResetFwHwReset(console_ramoops_file());
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));
  ASSERT_TRUE(FindLog("(handling)"));
  ASSERT_TRUE(FindLog(signature.c_str()));
  EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
      test_crash_directory(), "*.meta", signature));
}

TEST_F(KernelCollectorTest, WatchdogOK) {
  WatchdogOKHelper(console_ramoops_file());
}

void KernelCollectorTest::WatchdogOnlyLastBootHelper(const FilePath& path) {
  char next[] = "115 | 2016-03-24 15:24:27 | System boot | 0";
  SetUpSuccessfulWatchdog(path);
  ASSERT_TRUE(test_util::CreateFile(eventlog_file(), next));
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/true),
              testing::ElementsAre(CrashCollectionStatus::kNoCrashFound));
}

TEST_F(KernelCollectorTest, WatchdogOnlyLastBoot) {
  WatchdogOnlyLastBootHelper(console_ramoops_file());
}

TEST_F(KernelCollectorTest, ComputeSeverity) {
  CrashCollector::ComputedCrashSeverity computed_severity =
      collector_.ComputeSeverity("any executable");

  EXPECT_EQ(computed_severity.crash_severity,
            CrashCollector::CrashSeverity::kFatal);
  EXPECT_EQ(computed_severity.product_group,
            CrashCollector::Product::kPlatform);
}

TEST_F(KernelCollectorTest, WasKernelCrashIfRamoopsCollected) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kSuccess};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfRamoopsFailed) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kOutOfCapacity};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfOneEfiCollected) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kSuccess};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kNoCrashFound};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfMultipleEfiCollected) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kSuccess, CrashCollectionStatus::kSuccess};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kNoCrashFound};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfMultipleEfiFailed) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kOutOfCapacity,
      CrashCollectionStatus::kFailureLoadingPstoreCrash};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kNoCrashFound};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfOneRamoopsCollected) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kSuccess};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfMultipleRamoopsCollected) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kSuccess, CrashCollectionStatus::kSuccess};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasKernelCrashIfMultipleRamoopsFailed) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kFailureGettingPstoreType,
      CrashCollectionStatus::kFailureLoadingPstoreCrash};
  EXPECT_TRUE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

TEST_F(KernelCollectorTest, WasNotKernelCrashIfBothFoundNothing) {
  std::vector<CrashCollectionStatus> efi_statuses{
      CrashCollectionStatus::kNoCrashFound};
  std::vector<CrashCollectionStatus> ramoops_statuses{
      CrashCollectionStatus::kNoCrashFound};
  EXPECT_FALSE(KernelCollector::WasKernelCrash(efi_statuses, ramoops_statuses));
}

class KernelCollectorSavedLsbTest : public KernelCollectorTest,
                                    public ::testing::WithParamInterface<bool> {
};

TEST_P(KernelCollectorSavedLsbTest, UsesSavedLsbRamoops) {
  FilePath lsb_release = temp_dir().Append("lsb-release");
  collector_.set_lsb_release_for_test(lsb_release);
  const char kLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=6727.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=82\n"
      "CHROMEOS_RELEASE_TRACK=testimage-channel\n"
      "CHROMEOS_RELEASE_DESCRIPTION=6727.0.2015_01_26_0853 (Test Build - foo)";
  ASSERT_TRUE(test_util::CreateFile(lsb_release, kLsbContents));

  FilePath saved_lsb_dir = temp_dir().Append("crash-reporter-state");
  ASSERT_TRUE(base::CreateDirectory(saved_lsb_dir));
  collector_.set_reporter_state_directory_for_test(saved_lsb_dir);

  const char kSavedLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=12345.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=81\n"
      "CHROMEOS_RELEASE_TRACK=beta-channel\n"
      "CHROMEOS_RELEASE_DESCRIPTION=12345.0.2015_01_26_0853 (Test Build - foo)";
  base::FilePath saved_lsb = saved_lsb_dir.Append("lsb-release");
  ASSERT_TRUE(test_util::CreateFile(saved_lsb, kSavedLsbContents));

  SetUpSuccessfulCollect();
  EXPECT_THAT(collector_.CollectRamoopsCrashes(/*use_saved_lsb=*/GetParam()),
              testing::ElementsAre(CrashCollectionStatus::kSuccess));

  if (GetParam()) {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
        test_crash_directory(), "*.meta", "ver=12345.0.2015_01_26_0853\n"));
  } else {
    EXPECT_TRUE(test_util::DirectoryHasFileWithPatternAndContents(
        test_crash_directory(), "*.meta", "ver=6727.0.2015_01_26_0853\n"));
  }
}

INSTANTIATE_TEST_SUITE_P(KernelCollectorSavedLsbTest,
                         KernelCollectorSavedLsbTest,
                         testing::Bool());
