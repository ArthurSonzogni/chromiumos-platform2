// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for functionality in processes.h.

#include "secanomalyd/processes.h"

#include <string>

#include <gtest/gtest.h>

#include <base/optional.h>
#include <brillo/process/process_mock.h>

using testing::_;
using testing::Return;

namespace secanomalyd {

namespace {
constexpr char kProcesses[] =
    "  1 4026531836 init            /sbin/init\n"                     // 0
    "471 4026531836 agetty          agetty 115200 ttyS0 linux\n"      // 1
    "506 4026531836 auditd          /sbin/auditd -n -c /etc/audit\n"  // 2
    "516 4026531836 loop2           [loop2]\n"                        // 3

    // 4
    "535 4026531836 rsyslogd        "
    "/usr/sbin/rsyslogd -n -f /etc/rsyslog.chromeos -i /tmp/rsyslogd.pid\n"

    "553 4026531836 loop3           [loop3]\n"                      // 5
    "587 4026531836 loop4           [loop4]\n"                      // 6
    "589 4026531836 dbus-daemon     dbus-daemon --system --fork\n"  // 7

    // 8
    "739 4026532412 minijail-init   minijail0 -i -u iioservice -g iioservice "
    "-N --uts -e -p -P /mnt/empty -b / -b /sys -k tmpfs /run tmpfs MS_NOSUID "
    "MS_NODEV MS_NOEXEC -n -S /usr/share/policy/iioservice-seccomp.policy -b "
    "/sys/bus -b /sys/devices  1 -b /dev  1 -b /sys/firmware -b /sys/class "
    "-b /run/dbus -k tmpfs /var tmpfs MS_NOSUID MS_NODEV MS_NOEXEC -b "
    "/var/lib/metrics  1 -R 13 40 40 -- /usr/sbin/iioservice\n"

    // 9
    "741 4026532444 minijail-init   minijail0 -i -u hpsd -g hpsd -N --uts -e "
    "-p -P /mnt/empty -b / -b /sys -k tmpfs /run tmpfs MS_NOSUID MS_NODEV "
    "MS_NOEXEC -n -b /sys/bus -b /sys/devices  1 -b /dev  1 -b /sys/class -b "
    "/run/dbus -R 13 40 40 -- /usr/sbin/hpsd --skipboot --test --version=0 "
    "--mcu_path= --spi_path=\n"

    // 10
    "751 4026531836 wpa_supplicant  /usr/sbin/wpa_supplicant -u -s "
    "-O/run/wpa_supplicant\n"

    "753 4026532449 oobe_config_res /usr/sbin/oobe_config_restore\n"  // 11
    "774 4026531836 trunksd         trunksd\n"                        // 12

    // 13
    "788 4026531836 tpm_managerd    /usr/sbin/tpm_managerd "
    "--wait_for_ownership_trigger\n";

constexpr char kHpsdArgs[] =
    "minijail0 -i -u hpsd -g hpsd -N --uts -e "
    "-p -P /mnt/empty -b / -b /sys -k tmpfs /run tmpfs MS_NOSUID MS_NODEV "
    "MS_NOEXEC -n -b /sys/bus -b /sys/devices 1 -b /dev 1 -b /sys/class -b "
    "/run/dbus -R 13 40 40 -- /usr/sbin/hpsd --skipboot --test --version=0 "
    "--mcu_path= --spi_path=";
}  // namespace

TEST(ProcEntry, DefaultValues) {
  ProcEntry pe("this is not a string");
  ASSERT_EQ(pe.pid(), -1);
  ASSERT_EQ(pe.pidns(), 0u);
}

TEST(ProcEntry, FromStringPiece) {
  ProcEntry pe("3295 4026531836 ps              ps ax -o pid,pidns,comm,args");
  ASSERT_EQ(pe.pid(), 3295);
  ASSERT_EQ(pe.pidns(), 4026531836u);
  ASSERT_EQ(pe.comm(), "ps");
  ASSERT_EQ(pe.args(), "ps ax -o pid,pidns,comm,args");
}

TEST(ProcessesTest, EmptyString) {
  MaybeProcEntries entries = ReadProcessesFromString("", ProcessFilter::kAll);
  ASSERT_FALSE(entries.has_value());
}

TEST(ProcessesTest, InvalidString) {
  MaybeProcEntries entries = ReadProcessesFromString(
      "this is not a valid string\ndef not", ProcessFilter::kAll);
  // ASSERT_EQ(entries, base::nullopt);
  ASSERT_FALSE(entries.has_value());
}

TEST(ProcessesTest, InvalidPid) {
  MaybeProcEntries entries = ReadProcessesFromString(
      "0 4026531836 ps              ps ax -o pid,pidns,comm,args",
      ProcessFilter::kAll);
  ASSERT_FALSE(entries.has_value());
}

TEST(ProcessesTest, InvalidPidns) {
  MaybeProcEntries entries = ReadProcessesFromString(
      "3295 0 ps              ps ax -o pid,pidns,comm,args",
      ProcessFilter::kAll);
  ASSERT_FALSE(entries.has_value());
}

TEST(ProcessesTest, ActualProcesses) {
  MaybeProcEntries maybe_entries =
      ReadProcessesFromString(kProcesses, ProcessFilter::kAll);
  ASSERT_TRUE(maybe_entries.has_value());
  ProcEntries entries = maybe_entries.value();
  ASSERT_EQ(entries.size(), 14u);

  ASSERT_EQ(entries[1].pid(), 471);
  ASSERT_EQ(entries[1].comm(), "agetty");
  ASSERT_EQ(entries[2].pid(), 506);
  ASSERT_EQ(entries[2].pidns(), 4026531836);
  ASSERT_EQ(entries[2].comm(), "auditd");
  ASSERT_EQ(entries[3].comm(), "loop2");
  ASSERT_EQ(
      entries[4].args(),
      "/usr/sbin/rsyslogd -n -f /etc/rsyslog.chromeos -i /tmp/rsyslogd.pid");
  ASSERT_EQ(entries[6].comm(), "loop4");
  ASSERT_EQ(entries[9].args(), kHpsdArgs);
}

TEST(ProcessesTest, PsSucceeds) {
  std::unique_ptr<brillo::ProcessMock> reader(new brillo::ProcessMock());

  EXPECT_CALL(*reader, RedirectUsingMemory(STDOUT_FILENO));
  EXPECT_CALL(*reader, Run()).WillOnce(Return(0));
  EXPECT_CALL(*reader, GetOutputString(STDOUT_FILENO))
      .WillOnce(Return(std::string(kProcesses)));

  MaybeProcEntries maybe_entries =
      ReadProcesses(reader.get(), ProcessFilter::kAll);
  ASSERT_TRUE(maybe_entries.has_value());
}

TEST(ProcessesTest, PsFails) {
  std::unique_ptr<brillo::ProcessMock> reader(new brillo::ProcessMock());

  EXPECT_CALL(*reader, Run()).WillOnce(Return(1));
  MaybeProcEntries maybe_entries =
      ReadProcesses(reader.get(), ProcessFilter::kAll);
  ASSERT_FALSE(maybe_entries.has_value());
}

TEST(ProcessesTest, EmptyOutputStringFails) {
  std::unique_ptr<brillo::ProcessMock> reader(new brillo::ProcessMock());

  EXPECT_CALL(*reader, RedirectUsingMemory(STDOUT_FILENO));
  EXPECT_CALL(*reader, Run()).WillOnce(Return(0));
  EXPECT_CALL(*reader, GetOutputString(STDOUT_FILENO))
      .WillOnce(Return(std::string()));

  MaybeProcEntries maybe_entries =
      ReadProcesses(reader.get(), ProcessFilter::kAll);
  ASSERT_FALSE(maybe_entries.has_value());
}

TEST(ProcessesTest, FilterInitPidNs) {
  MaybeProcEntries maybe_entries =
      ReadProcessesFromString(kProcesses, ProcessFilter::kInitPidNamespaceOnly);
  ASSERT_TRUE(maybe_entries.has_value());
  ProcEntries entries = maybe_entries.value();
  ASSERT_EQ(entries.size(), 11u);

  // Check that all entries match the pidns of init.
  for (const auto& entry : entries) {
    EXPECT_EQ(entry.pidns(), 4026531836);
  }
}

TEST(ProcessesTest, FilterInitPidNsWithNoInitProcess) {
  std::string processes = std::string(kProcesses);
  std::string::size_type line_break_index = processes.find("\n");
  ASSERT_NE(line_break_index, std::string::npos);

  // Parse processes starting on the second line.
  MaybeProcEntries maybe_entries =
      ReadProcessesFromString(processes.substr(line_break_index + 1),
                              ProcessFilter::kInitPidNamespaceOnly);
  ASSERT_FALSE(maybe_entries.has_value());
}

}  // namespace secanomalyd
