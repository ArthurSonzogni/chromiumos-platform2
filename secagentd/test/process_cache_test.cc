// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/process_cache.h"

#include <unistd.h>

#include <map>
#include <memory>
#include <string>

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/string_util.h"
#include "gmock/gmock.h"  // IWYU pragma: keep
#include "gtest/gtest.h"
#include "secagentd/bpf/process.h"
#include "secagentd/proto/security_xdr_events.pb.h"

namespace {

namespace pb = cros_xdr::reporting;

MATCHER_P(EqualsProto,
          message,
          "Match a proto Message equal to the matcher's argument.") {
  std::string expected_serialized, actual_serialized;
  message.SerializeToString(&expected_serialized);
  arg.SerializeToString(&actual_serialized);
  return expected_serialized == actual_serialized;
}

// Partially() protobuf matcher isn't available and importing it is more
// involved than copy-pasting a single macro. So improvise.
void ExpectPartialMatch(pb::Process& expected, pb::Process& actual) {
  EXPECT_EQ(expected.canonical_pid(), actual.canonical_pid());
  EXPECT_EQ(expected.commandline(), actual.commandline());
  if (expected.has_image()) {
    EXPECT_EQ(expected.image().pathname(), actual.image().pathname());
    EXPECT_EQ(expected.image().mnt_ns(), actual.image().mnt_ns());
    EXPECT_STRCASEEQ(expected.image().sha256().c_str(),
                     actual.image().sha256().c_str());
  }
}

void FillDynamicImageInfoFromMockFs(
    const char* filename, secagentd::bpf::cros_image_info* image_info) {
  base::stat_wrapper_t stat;
  ASSERT_EQ(0, base::File::Stat(filename, &stat));
  image_info->inode_device_id = stat.st_dev;
  image_info->inode = stat.st_ino;
  image_info->mtime.tv_sec = stat.st_mtim.tv_sec;
  image_info->mtime.tv_nsec = stat.st_mtim.tv_nsec;
  image_info->ctime.tv_sec = stat.st_ctim.tv_sec;
  image_info->ctime.tv_nsec = stat.st_ctim.tv_nsec;
}
}  // namespace

namespace secagentd::testing {

class ProcessCacheTestFixture : public ::testing::Test {
 protected:
  struct MockProcFsFile {
    std::string procstat;
    uint64_t starttime_ns;
    std::string cmdline;
    base::FilePath exe_path;
    std::string exe_contents;
    std::string exe_sha256;
    base::FilePath mnt_ns_symlink;
    pb::Process expected_proto;
  };

  struct MockBpfSpawnEvent {
    bpf::cros_process_start process_start;
    std::string exe_contents;
    std::string exe_sha256;
    pb::Process expected_proto;
  };

  static constexpr uint64_t kPidInit = 1;
  static constexpr uint64_t kPidKthreadd = 2;
  static constexpr uint64_t kPidChildOfInit = 962;
  static constexpr uint64_t kPidChildOfChild = 23888;
  static constexpr uint64_t kPidSiblingOfChildOfChild = 1234;
  static constexpr uint64_t kPidTrickyComm = 8934;
  static constexpr uint64_t kPidThermalProcess = 9843;
  static constexpr uint64_t kPidChildOfThermalProcess = 9024;
  static constexpr uint64_t kPidRecoverDutProcess = 9168;
  static constexpr uint64_t kPidChildOfRecoverDutProcess = 9114;

  void CreateFakeFs(const base::FilePath& root) {
    const base::FilePath proc_dir = root.Append("proc");
    ASSERT_TRUE(base::CreateDirectory(proc_dir));

    for (auto& p : mock_procfs_) {
      const base::FilePath pid_dir = proc_dir.Append(std::to_string(p.first));
      ASSERT_TRUE(base::CreateDirectory(pid_dir));
      ASSERT_TRUE(base::WriteFile(pid_dir.Append("stat"), p.second.procstat));
      ASSERT_TRUE(base::WriteFile(pid_dir.Append("cmdline"), p.second.cmdline));
      if (!p.second.exe_path.empty()) {
        ASSERT_TRUE(base::WriteFile(p.second.exe_path, p.second.exe_contents));
        ASSERT_TRUE(
            base::CreateSymbolicLink(p.second.exe_path, pid_dir.Append("exe")));
      }
      const base::FilePath ns_dir = pid_dir.Append("ns");
      ASSERT_TRUE(base::CreateDirectory(ns_dir));
      ASSERT_TRUE(base::CreateSymbolicLink(p.second.mnt_ns_symlink,
                                           ns_dir.Append("mnt")));
    }

    for (auto& p : mock_spawns_) {
      ASSERT_TRUE(base::WriteFile(
          base::FilePath(p.second.process_start.image_info.pathname),
          p.second.exe_contents));
    }
  }

  void ClearInternalCache() { process_cache_->process_cache_->Clear(); }

  void SetUp() override {
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    const base::FilePath& root = fake_root_.GetPath();
    process_cache_ = ProcessCache::CreateForTesting(root);

    mock_procfs_ = {
        {kPidInit,
         {.procstat =
              "1 (init) S 0 1 1 0 -1 4194560 52789 185694 61 508 25 147 624 "
              "595 20 0 1 0 2 5705728 1114 184 46744073709551615 "
              "93986791456768 93986791580992 140721417359440 0 0 0 0 4096 "
              "536946211 1 0 0 17 4 0 0 2 0 0 93986791594336 939867915 95104 "
              "93986819518464 140721417363254 140721417363304 140721417363304 "
              "140721417363437 0 ",
          .starttime_ns = 20000000,
          .cmdline = "/sbin/init",
          .exe_path = root.Append("sbin_init"),
          .exe_contents = "This is the init binary",
          // echo -ne "This is the init binary" | sha256sum -
          // 4d4328fb2f25759a7bd95772f2caf19af15ad7722c4105dd403a391a6e795b88  -
          .exe_sha256 = "4D4328FB2F25759A7BD95772F2CAF19AF15AD7722C4105DD403A39"
                        "1A6E795B88",
          .mnt_ns_symlink = base::FilePath("mnt:[402653184]"),
          .expected_proto = pb::Process()}},
        {kPidKthreadd,
         {.procstat = "2 (kthreadd) S 0 0 0 0 -1 2129984 0 0 0 0 0 22 0 0 20 0 "
                      "1 0 2 0 0 18446744073709551615 0 0 0 0 0 0 0 2147483647 "
                      "0 1 0 0 0 4 0 0 0 0 0 0 0 0 0 0 0 0 0",
          .starttime_ns = 20000001,
          .cmdline = "",
          .exe_path = base::FilePath(),
          .exe_contents = "",
          .mnt_ns_symlink = base::FilePath("mnt:[402653184]"),
          .expected_proto = pb::Process()}},
        {kPidChildOfInit,
         {.procstat =
              "962 (cryptohomed) S 1 962 962 0 -1 1077936192 131232 1548267 2 "
              "0 111 322 2065 1451 20 0 5 0 378 408432640 3365 "
              "18446744073709551615 97070014267392 97070015746192 "
              "140737338593200 0 0 0 16387 0 0 0 0 0 17 7 0 0 0 0 0 97070015 "
              "$ 98896 97070015799432 97070032941056 140737338596688 "
              "140737338596750 140737338596750 140737338597346 0 ",
          .starttime_ns = 3780000000,
          .cmdline = std::string("cryptohomed\0--noclose\0--direncryption\0--"
                                 "fscrypt_v2\0--vmodule=",
                                 61),
          .exe_path = root.Append("usr_sbin_cryptohomed"),
          .exe_contents = "This is the cryptohome binary",
          // # echo -ne "This is the cryptohome binary" | sha256sum -
          // 6923461afaed79a0ecd65048f47524fd7b873d7ff9e164b09b5d9a1d4b5e54f2  -
          .exe_sha256 = "6923461AFAED79A0ECD65048F47524FD7B873D7FF9E164B09B5D9A"
                        "1D4B5E54F2",
          .mnt_ns_symlink = base::FilePath("mnt:[402653184]"),
          .expected_proto = pb::Process()}},
        {kPidTrickyComm,
         {.procstat =
              "962 (crypto (home) d) S 1 962 962 0 -1 1077936192 131232 "
              "1548267 2 "
              "0 111 322 2065 1451 20 0 5 0 978 408432640 3365 "
              "18446744073709551615 97070014267392 97070015746192 "
              "140737338593200 0 0 0 16387 0 0 0 0 0 17 7 0 0 0 0 0 97070015 "
              "$ 98896 97070015799432 97070032941056 140737338596688 "
              "140737338596750 140737338596750 140737338597346 0 ",
          .starttime_ns = 9780000000,
          .cmdline = "commspoofer",
          .exe_path = root.Append("tmp_commspoofer"),
          .exe_contents = "unused",
          .exe_sha256 = "unused",
          .mnt_ns_symlink = base::FilePath("mnt:[402653184]"),
          .expected_proto = pb::Process()}},
        {kPidThermalProcess,
         {
             .procstat =
                 "9843 (temp_logger.sh) S 9842 9841 9841 0 -1 4194560 118 142 "
                 "0 0 0 0 0 0 20 0 1 0 978 2838528 306 "
                 "18446744073709551615 97600878653440 97600878726528 "
                 "140731096167264 0 0 0 0 0 65538 1 0 0 17 2 0 0 0 0 0 "
                 "97600878738944 97600878739424 97600907300864 140731096170227 "
                 "140731096170271 140731096170271 140731096170452 0",
             .starttime_ns = 9780000000,
             .cmdline = std::string(
                 "/bin/sh\0/usr/share/cros/init/temp_logger.sh", 43),
             .exe_path = root.Append("bin_sh"),
             .exe_contents = "This is the shell binary",
             // echo -ne "This is the shell binary" | sha256sum
             // 9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE4037550F831392FF81
             .exe_sha256 = "9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE40375"
                           "50F831392FF81",
             .mnt_ns_symlink = base::FilePath("mnt:[4026532856]"),
             .expected_proto = pb::Process(),
         }},
        {kPidRecoverDutProcess,
         {
             .procstat =
                 "24498 (recover_duts) T 5038 24498 5038 34816 24639 "
                 "1077936128 118 234 0 0 0 0 0 0 20 0 1 0 0 2654208 226 "
                 "18446744073709551615 94131781091328 94131781165952 "
                 "140735961348096 0 0 0 0 0 65538 1 0 0 17 0 0 0 0 0 0 "
                 "94131781178368 94131781178848 94131798298624 140735961351745 "
                 "140735961351798 140735961351798 140735961354187 0",
             .starttime_ns = 0,
             .cmdline = std::string(
                 "/bin/sh\0/usr/local/libexec/recover-duts/recover_duts", 52),
             .exe_path = root.Append("bin_sh"),
             .exe_contents = "This is the shell binary",
             // echo -ne "This is the shell binary" | sha256sum
             // 9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE4037550F831392FF81
             .exe_sha256 = "9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE40375"
                           "50F831392FF81",
             .mnt_ns_symlink = base::FilePath("mnt:[4026531840]"),
             .expected_proto = pb::Process(),
         }}};

    // ParseFromString unfortunately doesn't work with Lite protos.
    mock_procfs_[kPidInit].expected_proto.set_canonical_pid(kPidInit);
    mock_procfs_[kPidInit].expected_proto.set_commandline("'/sbin/init'");
    mock_procfs_[kPidInit].expected_proto.mutable_image()->set_pathname(
        root.Append("sbin_init").value());
    mock_procfs_[kPidInit].expected_proto.mutable_image()->set_mnt_ns(
        402653184);
    mock_procfs_[kPidInit].expected_proto.mutable_image()->set_sha256(
        mock_procfs_[kPidInit].exe_sha256);
    mock_procfs_[kPidKthreadd].expected_proto.set_canonical_pid(kPidKthreadd);
    mock_procfs_[kPidKthreadd].expected_proto.set_commandline("[kthreadd]");
    mock_procfs_[kPidChildOfInit].expected_proto.set_canonical_pid(
        kPidChildOfInit);
    mock_procfs_[kPidChildOfInit].expected_proto.set_commandline(
        "'cryptohomed' '--noclose' '--direncryption' '--fscrypt_v2' "
        "'--vmodule='");
    mock_procfs_[kPidChildOfInit].expected_proto.mutable_image()->set_pathname(
        root.Append("usr_sbin_cryptohomed").value());
    mock_procfs_[kPidChildOfInit].expected_proto.mutable_image()->set_mnt_ns(
        402653184);
    mock_procfs_[kPidChildOfInit].expected_proto.mutable_image()->set_sha256(
        mock_procfs_[kPidChildOfInit].exe_sha256);
    mock_procfs_[kPidTrickyComm].expected_proto.set_canonical_pid(
        kPidTrickyComm);
    mock_procfs_[kPidTrickyComm].expected_proto.set_commandline(
        "'commspoofer'");
    mock_procfs_[kPidTrickyComm].expected_proto.mutable_image()->set_pathname(
        root.Append("tmp_comspoofer").value());
    mock_procfs_[kPidChildOfInit].expected_proto.mutable_image()->set_mnt_ns(
        402653184);
    mock_procfs_[kPidTrickyComm].expected_proto.mutable_image()->set_sha256(
        mock_procfs_[kPidTrickyComm].exe_sha256);

    mock_spawns_ = {
        {kPidChildOfRecoverDutProcess,
         {.process_start =
              {.task_info =
                   {.pid = kPidChildOfRecoverDutProcess,
                    .ppid = kPidRecoverDutProcess,
                    .start_time = 5029384029,
                    .parent_start_time =
                        mock_procfs_[kPidRecoverDutProcess].starttime_ns,
                    .commandline =
                        "/bin/sh\0/usr/local/libexec/recover-duts/recover_duts",
                    .commandline_len = 52,
                    .uid = 0,
                    .gid = 0},
               .image_info =
                   {
                       .pathname = "bin_sh",
                       .mnt_ns = 4026531840,
                       .inode_device_id = 0,
                       .inode = 0,
                       .uid = 0,
                       .gid = 0,
                       .mode = 0100755,
                   },
               .spawn_namespace =
                   {
                       .cgroup_ns = 4026532932,
                       .pid_ns = 4026532856,
                       .user_ns = 4026531837,
                       .uts_ns = 4026532858,
                       .mnt_ns = 4026532857,
                       .net_ns = 4026532859,
                       .ipc_ns = 4026533674,
                   }},
          .exe_contents = "This is the recover dut binary",
          // # echo -ne "This is the recover dut binary" | sha256sum -
          // 370EF140032B15E038FC673568221074D40153DB7EF61297B63276107714A6B8
          .exe_sha256 = "370EF140032B15E038FC673568221074D40153DB7EF61297B63276"
                        "107714A6B8",
          .expected_proto = pb::Process()}},
        {kPidChildOfThermalProcess,
         {.process_start =
              {.task_info =
                   {.pid = kPidChildOfThermalProcess,
                    .ppid = kPidThermalProcess,
                    .start_time = 5029384029,
                    .parent_start_time =
                        mock_procfs_[kPidThermalProcess].starttime_ns,
                    .commandline =
                        "/usr/bin/logger\0-t\0temp_logger\0\"Exiting "
                        "temp_logger, system does not have any temp sensor.\"",
                    .commandline_len = 58,
                    .uid = 0,
                    .gid = 0},
               .image_info =
                   {
                       .pathname = "usr_bin_logger",
                       .mnt_ns = 4026531840,
                       .inode_device_id = 0,
                       .inode = 0,
                       .uid = 0,
                       .gid = 0,
                       .mode = 0100755,
                   },
               .spawn_namespace =
                   {
                       .cgroup_ns = 4026532932,
                       .pid_ns = 4026532856,
                       .user_ns = 4026531837,
                       .uts_ns = 4026532858,
                       .mnt_ns = 4026532857,
                       .net_ns = 4026532859,
                       .ipc_ns = 4026533674,
                   }},
          .exe_contents = "This is the logger binary",
          // # echo -ne "This is the logger binary" | sha256sum -
          // D1F76C43FB64CDCB35DE37F518C4AD1EE8EE247D540B6F2C07358657E4AA2F59
          .exe_sha256 = "D1F76C43FB64CDCB35DE37F518C4AD1EE8EE247D540B6F2C073586"
                        "57E4AA2F59",
          .expected_proto = pb::Process()}},
        {kPidSiblingOfChildOfChild,
         {.process_start =
              {.task_info = {.pid = kPidSiblingOfChildOfChild,
                             .ppid = kPidChildOfInit,
                             .start_time = 5029384029,
                             .parent_start_time =
                                 mock_procfs_[kPidChildOfInit].starttime_ns,
                             .commandline =
                                 "/bin/sh\0/usr/share/cros/init/temp_logger.sh",
                             .commandline_len = 43,
                             .uid = 0,
                             .gid = 0},
               .image_info =
                   {
                       .pathname = "bin_sh",
                       .mnt_ns = 4026531840,
                       .inode_device_id = 0,
                       .inode = 0,
                       .uid = 0,
                       .gid = 0,
                       .mode = 0100755,
                   },
               .spawn_namespace =
                   {
                       .cgroup_ns = 4026531835,
                       .pid_ns = 4026531836,
                       .user_ns = 4026531837,
                       .uts_ns = 4026531838,
                       .mnt_ns = 4026531840,
                       .net_ns = 4026531999,
                       .ipc_ns = 4026531839,
                   }},
          .exe_contents = "This is the shell binary",
          // echo -ne "This is the shell binary" | sha256sum
          // 9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE4037550F831392FF81
          .exe_sha256 = "9DF8B99E5B9F67AAD3F2382F7633BDE35EE032881F7FFE40375"
                        "50F831392FF81",
          .expected_proto = pb::Process()}},
        {kPidChildOfChild,
         {.process_start =
              {.task_info = {.pid = kPidChildOfChild,
                             .ppid = kPidChildOfInit,
                             .start_time = 5029384029,
                             .parent_start_time =
                                 mock_procfs_[kPidChildOfInit].starttime_ns,
                             .commandline =
                                 "/usr/sbin/spaced_cli\0"
                                 "--get_free_disk_space=/home/.shadow",
                             .commandline_len = 57,
                             .uid = 0,
                             .gid = 0},
               .image_info =
                   {
                       .pathname = "usr_sbin_spaced_cli",
                       .mnt_ns = 4026531840,
                       .inode_device_id = 0,
                       .inode = 0,
                       .uid = 0,
                       .gid = 0,
                       .mode = 0100755,
                   },
               .spawn_namespace =
                   {
                       .cgroup_ns = 4026531835,
                       .pid_ns = 4026531836,
                       .user_ns = 4026531837,
                       .uts_ns = 4026531838,
                       .mnt_ns = 4026531840,
                       .net_ns = 4026531999,
                       .ipc_ns = 4026531839,
                   }},
          .exe_contents = "This is the spaced_cli binary",
          // # echo -ne "This is the spaced_cli binary" | sha256sum -
          // 7c3ad304a78de0191f3c682d84f22787ad1085ae1cf1c158544b097556dcf408  -
          .exe_sha256 = "7C3AD304A78DE0191F3C682D84F22787AD1085AE1CF1C158544B09"
                        "7556DCF408",
          .expected_proto = pb::Process()}}};
    // Prefix each pathname with mock fs root. Awkward to do this at
    // initialization due to pathname being a char array.
    for (auto& p : mock_spawns_) {
      base::strlcpy(p.second.process_start.image_info.pathname,
                    root.Append(p.second.process_start.image_info.pathname)
                        .value()
                        .c_str(),
                    sizeof(p.second.process_start.image_info.pathname));
    }

    CreateFakeFs(root);

    FillDynamicImageInfoFromMockFs(
        mock_spawns_[kPidChildOfChild].process_start.image_info.pathname,
        &mock_spawns_[kPidChildOfChild].process_start.image_info);
    mock_spawns_[kPidChildOfChild].expected_proto.set_canonical_pid(
        kPidChildOfChild);
    mock_spawns_[kPidChildOfChild].expected_proto.set_canonical_uid(0);
    mock_spawns_[kPidChildOfChild].expected_proto.set_rel_start_time_s(
        ProcessCache::ClockTToSeconds(
            ProcessCache::LossyNsecToClockT(5029384029)));
    mock_spawns_[kPidChildOfChild].expected_proto.set_commandline(
        "'/usr/sbin/spaced_cli' '--get_free_disk_space=/home/.shadow'");
    mock_spawns_[kPidChildOfChild].expected_proto.mutable_image()->set_pathname(
        root.Append("usr_sbin_spaced_cli").value());
    mock_spawns_[kPidChildOfChild].expected_proto.mutable_image()->set_mnt_ns(
        4026531840);
    mock_spawns_[kPidChildOfChild]
        .expected_proto.mutable_image()
        ->set_inode_device_id(mock_spawns_[kPidChildOfChild]
                                  .process_start.image_info.inode_device_id);
    mock_spawns_[kPidChildOfChild].expected_proto.mutable_image()->set_inode(
        mock_spawns_[kPidChildOfChild].process_start.image_info.inode);
    mock_spawns_[kPidChildOfChild]
        .expected_proto.mutable_image()
        ->set_canonical_uid(0);
    mock_spawns_[kPidChildOfChild]
        .expected_proto.mutable_image()
        ->set_canonical_gid(0);
    mock_spawns_[kPidChildOfChild].expected_proto.mutable_image()->set_mode(
        0100755);
    mock_spawns_[kPidChildOfChild].expected_proto.mutable_image()->set_sha256(
        mock_spawns_[kPidChildOfChild].exe_sha256);
  }

  scoped_refptr<ProcessCache> process_cache_;
  base::ScopedTempDir fake_root_;
  std::map<uint64_t, MockProcFsFile> mock_procfs_;
  std::map<uint64_t, MockBpfSpawnEvent> mock_spawns_;
};

TEST_F(ProcessCacheTestFixture, TestStableUuid) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(mock_spawns_[kPidChildOfChild].process_start);
  auto before = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  ClearInternalCache();
  process_cache_->PutFromBpfExec(process_start);
  auto after = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  EXPECT_EQ(before[0]->process_uuid(), after[0]->process_uuid());
  EXPECT_EQ(before[1]->process_uuid(), after[1]->process_uuid());
  // Might as well check that the UUIDs are somewhat unique.
  EXPECT_NE(before[0]->process_uuid(), before[1]->process_uuid());
}

TEST_F(ProcessCacheTestFixture, TestUuidBpfVsProcfs) {
  const bpf::cros_process_task_info task_info = {
      .pid = kPidChildOfInit,
      .start_time = mock_procfs_[kPidChildOfInit].starttime_ns,
  };
  cros_xdr::reporting::Process bpf_process_proto;
  ProcessCache::PartiallyFillProcessFromBpfTaskInfo(task_info,
                                                    &bpf_process_proto);
  EXPECT_TRUE(bpf_process_proto.has_process_uuid());
  auto procfs_process_proto = process_cache_->GetProcessHierarchy(
      kPidChildOfInit, mock_procfs_[kPidChildOfInit].starttime_ns, 1);
  EXPECT_EQ(1, procfs_process_proto.size());
  EXPECT_TRUE(procfs_process_proto[0]->has_process_uuid());

  EXPECT_EQ(bpf_process_proto.process_uuid(),
            procfs_process_proto[0]->process_uuid());
}

TEST_F(ProcessCacheTestFixture, ProcfsCacheHit) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto before = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  EXPECT_EQ(3, before.size());
  // Verify and unset this metadata separately since it's expected to change
  // between calls.
  for (auto& proc : before) {
    // Verify that start_times set in the mock procfs spawns are earlier than
    // the first BPF exec here. This bypasses the heuristic which is covered in
    // a separate test case.
    ASSERT_LE(proc->rel_start_time_s(), process_start.task_info.start_time);
    EXPECT_TRUE(proc->has_meta_first_appearance());
    EXPECT_TRUE(proc->meta_first_appearance());
    proc->clear_meta_first_appearance();
  }

  ASSERT_TRUE(fake_root_.Delete());
  bpf::cros_process_start process_start_sibling = process_start;
  process_start_sibling.task_info.pid = process_start.task_info.pid + 1;
  process_start_sibling.task_info.start_time =
      process_start.task_info.start_time + 1;
  process_cache_->PutFromBpfExec(process_start_sibling);
  auto after = process_cache_->GetProcessHierarchy(
      process_start_sibling.task_info.pid,
      process_start_sibling.task_info.start_time, 3);
  EXPECT_EQ(3, after.size());
  // We've only seen after[1] and after[2] earlier as before[1] and before[2]
  // respectively.
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(after[i]->has_meta_first_appearance());
    if (i == 0) {
      EXPECT_TRUE(after[i]->meta_first_appearance());
    } else {
      // Verify and clear this volatile metadata as done earlier with before.
      EXPECT_FALSE(after[i]->meta_first_appearance());
      after[i]->clear_meta_first_appearance();
      EXPECT_THAT(*before[i], EqualsProto(*after[i]));
    }
  }

  ExpectPartialMatch(mock_procfs_[kPidChildOfInit].expected_proto, *before[1]);
  ExpectPartialMatch(mock_procfs_[kPidInit].expected_proto, *before[2]);
}

TEST_F(ProcessCacheTestFixture, ProcfsScrapeButSeenBefore) {
  // Heuristic uses earliest_seen_exec_rel_s_. Set that to a very low value
  // first.
  const bpf::cros_process_start earliest_seen_exec = {
      .task_info = {
          .pid = 9999,
          .ppid = kPidInit,
          .start_time =
              mock_spawns_[kPidInit].process_start.task_info.start_time + 1,
          .parent_start_time =
              mock_spawns_[kPidInit].process_start.task_info.start_time}};
  process_cache_->PutFromBpfExec(earliest_seen_exec);

  // Spawn a second BPF process with parent that's younger than
  // earliest_seen_exec.
  const bpf::cros_process_start& exec_with_young_ancestors =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(exec_with_young_ancestors);
  auto actual = process_cache_->GetProcessHierarchy(
      exec_with_young_ancestors.task_info.pid,
      exec_with_young_ancestors.task_info.start_time, 2);
  EXPECT_EQ(2, actual.size());
  ASSERT_GT(actual[1]->rel_start_time_s(),
            earliest_seen_exec.task_info.start_time);
  EXPECT_TRUE(actual[1]->has_meta_first_appearance());
  EXPECT_FALSE(actual[1]->meta_first_appearance());
}

TEST_F(ProcessCacheTestFixture, ThermalLoggerChildrenExecEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfThermalProcess].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture,
       ThermalLoggerChildrenTerminateEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfThermalProcess].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, RecoverDutsChildrenExecEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfRecoverDutProcess].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, RecoverDutsChildrenTerminateEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfRecoverDutProcess].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, SpacedCliExecEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  // this is spaced_cli as called by cryptohome.
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, SpacedCliTerminateEventsAreFiltered) {
  // underscorify the filter paths.
  process_cache_->InitializeFilter(true);
  // this is spaced_cli as called by cryptohome.
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_TRUE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, NotEverythingIsFiltered) {
  process_cache_->InitializeFilter(true);
  // this is cryptohom
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidSiblingOfChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto hierarchy = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 2);
  EXPECT_GE(hierarchy.size(), 2);
  EXPECT_FALSE(
      process_cache_->IsEventFiltered(hierarchy[1].get(), hierarchy[0].get()));
}

TEST_F(ProcessCacheTestFixture, BpfCacheHit) {
  const bpf::cros_process_start bpf_child = {
      .task_info = {
          .pid = 9999,
          .ppid = kPidChildOfChild,
          .start_time = 999999999,
          .parent_start_time = mock_spawns_[kPidChildOfChild]
                                   .process_start.task_info.start_time}};
  process_cache_->PutFromBpfExec(mock_spawns_[kPidChildOfChild].process_start);
  auto before = process_cache_->GetProcessHierarchy(
      kPidChildOfChild,
      mock_spawns_[kPidChildOfChild].process_start.task_info.start_time, 2);
  EXPECT_EQ(2, before.size());
  for (auto& proc : before) {
    EXPECT_TRUE(proc->has_meta_first_appearance());
    EXPECT_TRUE(proc->meta_first_appearance());
  }

  process_cache_->PutFromBpfExec(bpf_child);
  auto after = process_cache_->GetProcessHierarchy(
      bpf_child.task_info.pid, bpf_child.task_info.start_time, 4);
  EXPECT_EQ(4, after.size());
  // We've seen after[1] and after[2] earlier as before[0] and before[1]
  // respectively.
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(after[i]->has_meta_first_appearance());
    bool expected_first_appearance = (i == 0 || i == 3);
    EXPECT_EQ(expected_first_appearance, after[i]->meta_first_appearance());
    // Clearing this volatile metadata as it's not present in the
    // expected_proto.
    after[i]->clear_meta_first_appearance();
  }
  // Cheat and copy the UUID because we don't have a real Partial matcher.
  mock_spawns_[kPidChildOfChild].expected_proto.set_process_uuid(
      after[1]->process_uuid());
  EXPECT_THAT(mock_spawns_[kPidChildOfChild].expected_proto,
              EqualsProto(*after[1]));
  ExpectPartialMatch(mock_procfs_[kPidChildOfInit].expected_proto, *after[2]);
  ExpectPartialMatch(mock_procfs_[kPidInit].expected_proto, *after[3]);
}

TEST_F(ProcessCacheTestFixture, TruncateAtInit) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 5);
  // Asked for 5, got 3 including init.
  EXPECT_EQ(3, actual.size());
}

TEST_F(ProcessCacheTestFixture, TruncateOnBpfParentPidReuse) {
  bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_start.task_info.parent_start_time -= 10;
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  // Asked for 3, got 1 because parent start time didn't match.
  EXPECT_EQ(1, actual.size());
}

TEST_F(ProcessCacheTestFixture, TruncateOnBpfParentNotFound) {
  bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_start.task_info.ppid -= 10;
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  // Asked for 3, got 1 because parent pid doesn't exist in procfs.
  EXPECT_EQ(1, actual.size());
}

TEST_F(ProcessCacheTestFixture, DontFailProcfsIfParentLinkageNotFound) {
  bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  // "Kill" init
  base::DeletePathRecursively(
      fake_root_.GetPath().Append("proc").Append(std::to_string(kPidInit)));
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  // Asked for 3, got 2. Init doesn't exist but we at least got "child" even
  // though we failed to resolve its parent linkage.
  EXPECT_EQ(2, actual.size());
}

TEST_F(ProcessCacheTestFixture, ParseTrickyComm) {
  bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_start.task_info.ppid = kPidTrickyComm;
  process_start.task_info.parent_start_time =
      mock_procfs_[kPidTrickyComm].starttime_ns;
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  // Asked for 3, got 3. I.e we were able to parse commspoofer's stat to find
  // its parent.
  EXPECT_EQ(3, actual.size());
}

TEST_F(ProcessCacheTestFixture, TestChildOfKthread) {
  bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_start.task_info.ppid = kPidKthreadd;
  process_start.task_info.parent_start_time =
      mock_procfs_[kPidKthreadd].starttime_ns;
  process_cache_->PutFromBpfExec(process_start);
  auto actual = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 3);
  // Kthread doesn't have a parent. So we only get one ancestral process despite
  // asking for 3 as usual.
  EXPECT_EQ(2, actual.size());
  ExpectPartialMatch(mock_procfs_[kPidKthreadd].expected_proto, *actual[1]);
}

TEST_F(ProcessCacheTestFixture, TestErase) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  process_cache_->PutFromBpfExec(process_start);
  auto before = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 1);
  EXPECT_EQ(1, before.size());

  process_cache_->EraseProcess(process_start.task_info.pid,
                               process_start.task_info.start_time);
  auto after = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 1);
  EXPECT_EQ(0, after.size());
}

TEST_F(ProcessCacheTestFixture, TestProcessEraseNotInCache) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  // Nothing explodes if we call erase on an uncached process.
  process_cache_->EraseProcess(process_start.task_info.pid,
                               process_start.task_info.start_time);
}

TEST_F(ProcessCacheTestFixture, ImageCacheMissThenHit) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  // The following call will cause the image file to be read and checksummed.
  process_cache_->PutFromBpfExec(process_start);
  auto before = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 1);
  EXPECT_STRCASEEQ(mock_spawns_[kPidChildOfChild].exe_sha256.c_str(),
                   before[0]->image().sha256().c_str());

  // Delete the file image to verify that we're then onward getting cached
  // information. Note that the file deletion is a bit of a cheat because
  // there's otherwise no externally visible signal for a cache hit. We'll never
  // get an exec from BPF for a deleted file.
  ASSERT_TRUE(base::DeleteFile(base::FilePath(
      mock_spawns_[kPidChildOfChild].process_start.image_info.pathname)));

  // Make this a "new" process spawn so that we also miss the process cache.
  bpf::cros_process_start new_proc_same_image = process_start;
  new_proc_same_image.task_info.pid += 1;
  new_proc_same_image.task_info.start_time += 1;
  process_cache_->PutFromBpfExec(new_proc_same_image);
  auto after = process_cache_->GetProcessHierarchy(
      new_proc_same_image.task_info.pid,
      new_proc_same_image.task_info.start_time, 1);
  EXPECT_STRCASEEQ(mock_spawns_[kPidChildOfChild].exe_sha256.c_str(),
                   after[0]->image().sha256().c_str());
}

TEST_F(ProcessCacheTestFixture, ImageCacheMissDueToModification) {
  const bpf::cros_process_start& process_start =
      mock_spawns_[kPidChildOfChild].process_start;
  // The following call will cause the image file to be read and checksummed.
  process_cache_->PutFromBpfExec(process_start);
  auto before = process_cache_->GetProcessHierarchy(
      process_start.task_info.pid, process_start.task_info.start_time, 1);
  EXPECT_STRCASEEQ(mock_spawns_[kPidChildOfChild].exe_sha256.c_str(),
                   before[0]->image().sha256().c_str());

  bpf::cros_process_start new_proc_modified_image = process_start;
  new_proc_modified_image.task_info.pid += 1;
  new_proc_modified_image.task_info.start_time += 1;
  // Modify the file and update the mtime as the BPF usually would. Unsure why
  // but tmpfs needs some persuasion to update mtime. CrOS by design doesn't run
  // any executables from tmpfs.
  while ((new_proc_modified_image.image_info.mtime.tv_sec ==
          process_start.image_info.mtime.tv_sec) &&
         (new_proc_modified_image.image_info.mtime.tv_nsec ==
          process_start.image_info.mtime.tv_nsec)) {
    ASSERT_TRUE(base::WriteFile(
        base::FilePath(new_proc_modified_image.image_info.pathname),
        "This file has been altered"));
    FillDynamicImageInfoFromMockFs(new_proc_modified_image.image_info.pathname,
                                   &new_proc_modified_image.image_info);
  }
  ASSERT_EQ(new_proc_modified_image.image_info.inode_device_id,
            process_start.image_info.inode_device_id);
  ASSERT_EQ(new_proc_modified_image.image_info.inode,
            process_start.image_info.inode);

  process_cache_->PutFromBpfExec(new_proc_modified_image);
  auto after = process_cache_->GetProcessHierarchy(
      new_proc_modified_image.task_info.pid,
      new_proc_modified_image.task_info.start_time, 1);
  EXPECT_NE(mock_spawns_[kPidChildOfChild].exe_sha256,
            after[0]->image().sha256());
  // # echo -ne "This file has been altered" | sha256sum -
  // f55fb515f7ba4ed5e619e266168fde201e16da809f3e71438be84f435a160678  -
  EXPECT_STRCASEEQ(
      "F55FB515F7BA4ED5E619E266168FDE201E16DA809F3E71438BE84F435A160678",
      after[0]->image().sha256().c_str());
}

TEST_F(ProcessCacheTestFixture, ImageCacheHashAFileLargerThanBuf) {
  bpf::cros_process_start proc_with_large_image =
      mock_spawns_[kPidChildOfChild].process_start;
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(
          mock_spawns_[kPidChildOfChild].process_start.image_info.pathname),
      std::string(9999, '.')));
  FillDynamicImageInfoFromMockFs(proc_with_large_image.image_info.pathname,
                                 &proc_with_large_image.image_info);

  process_cache_->PutFromBpfExec(proc_with_large_image);
  auto actual = process_cache_->GetProcessHierarchy(
      proc_with_large_image.task_info.pid,
      proc_with_large_image.task_info.start_time, 1);
  // # printf '.%.0s' {1..9999} | sha256sum -
  // 6c9c6e06f2269516f665541d40859dc514fa7ab87c114c6fdfae4bbdd6a93416  -
  EXPECT_STRCASEEQ(
      "6C9C6E06F2269516F665541D40859DC514FA7AB87C114C6FDFAE4BBDD6A93416",
      actual[0]->image().sha256().c_str());
}

}  // namespace secagentd::testing
