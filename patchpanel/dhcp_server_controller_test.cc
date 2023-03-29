// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <utility>
#include <vector>

#include <base/check.h>
#include <base/files/file_path.h>
#include <gtest/gtest.h>
#include <shill/net/ip_address.h>
#include <shill/net/mock_process_manager.h>

using testing::_;
using testing::Return;
using testing::StrictMock;
using testing::WithArg;

namespace patchpanel {
namespace {

using Config = DHCPServerController::Config;

// Returns a IPv4 IPAddress, or crashes if the arguments are not valid.
shill::IPAddress CreateAndUnwrapIPAddress(const std::string& ip,
                                          unsigned int prefix = 0) {
  const auto ret = shill::IPAddress::CreateFromStringAndPrefix(
      ip, prefix, shill::IPAddress::kFamilyIPv4);
  CHECK(ret.has_value()) << ip << "/" << prefix << " is not a valid IP";
  return *ret;
}

class MockCallback {
 public:
  MOCK_METHOD(void, OnProcessExited, (int));
};

}  // namespace

class DHCPServerControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    dhcp_server_controller_.set_process_manager_for_testing(&process_manager_);
  }

  Config CreateValidConfig() {
    const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
    const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.50");
    const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.100");
    return Config::Create(host_ip, start_ip, end_ip).value();
  }

  StrictMock<shill::MockProcessManager> process_manager_;
  DHCPServerController dhcp_server_controller_{"wlan0"};
};

TEST_F(DHCPServerControllerTest, ConfigWithWrongSubnet) {
  // start_ip and end_ip are not in 192.168.1.0 subnet.
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.5.50");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.5.100");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip), std::nullopt);
}

TEST_F(DHCPServerControllerTest, ConfigWithWrongRange) {
  // end_ip is smaller than start_ip.
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.100");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.50");

  EXPECT_EQ(Config::Create(host_ip, start_ip, end_ip), std::nullopt);
}

TEST_F(DHCPServerControllerTest, ValidConfig) {
  const auto host_ip = CreateAndUnwrapIPAddress("192.168.1.1", 24);
  const auto start_ip = CreateAndUnwrapIPAddress("192.168.1.50");
  const auto end_ip = CreateAndUnwrapIPAddress("192.168.1.100");
  const auto config = Config::Create(host_ip, start_ip, end_ip);

  ASSERT_NE(config, std::nullopt);
  EXPECT_EQ(config->host_ip(), "192.168.1.1");
  EXPECT_EQ(config->netmask(), "255.255.255.0");
  EXPECT_EQ(config->start_ip(), "192.168.1.50");
  EXPECT_EQ(config->end_ip(), "192.168.1.100");
}

TEST_F(DHCPServerControllerTest, StartSuccessfulAtFirstTime) {
  const auto config = CreateValidConfig();
  const auto cmd_path = base::FilePath("/usr/sbin/dnsmasq");
  const std::vector<std::string> cmd_args = {
      "--dhcp-authoritative",
      "--keep-in-foreground",
      "--log-dhcp",
      "--no-ping",
      "--port=0",
      "--leasefile-ro",
      "--interface=wlan0",
      "--dhcp-range=192.168.1.50,192.168.1.100,255.255.255.0,12h",
      "--dhcp-option=option:netmask,255.255.255.0",
      "--dhcp-option=option:router,192.168.1.1"};
  constexpr pid_t pid = 5;

  EXPECT_CALL(process_manager_,
              StartProcessInMinijail(_, cmd_path, cmd_args, _, _, _))
      .WillOnce(Return(pid));
  EXPECT_CALL(process_manager_, StopProcess(pid)).WillOnce(Return(true));

  // Start() should be successful at the first time.
  EXPECT_EQ(dhcp_server_controller_.IsRunning(), false);
  EXPECT_TRUE(dhcp_server_controller_.Start(config, base::DoNothing()));
  EXPECT_TRUE(dhcp_server_controller_.IsRunning());

  // Start() should fail and do nothing when the previous one is still running.
  EXPECT_FALSE(dhcp_server_controller_.Start(config, base::DoNothing()));
}

TEST_F(DHCPServerControllerTest, StartFailed) {
  const auto config = CreateValidConfig();
  constexpr pid_t invalid_pid = shill::ProcessManager::kInvalidPID;

  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(invalid_pid));

  // Start() should fail if receiving invalid pid.
  EXPECT_FALSE(dhcp_server_controller_.Start(config, base::DoNothing()));
  EXPECT_FALSE(dhcp_server_controller_.IsRunning());
}

TEST_F(DHCPServerControllerTest, StartAndStop) {
  const auto config = CreateValidConfig();
  constexpr pid_t pid = 5;

  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(Return(pid));
  EXPECT_CALL(process_manager_, StopProcess(pid)).WillOnce(Return(true));

  EXPECT_TRUE(dhcp_server_controller_.Start(config, base::DoNothing()));
  EXPECT_TRUE(dhcp_server_controller_.IsRunning());

  // After the server is running, Stop() should make the server not running.
  dhcp_server_controller_.Stop();
  EXPECT_FALSE(dhcp_server_controller_.IsRunning());
}

TEST_F(DHCPServerControllerTest, OnProcessExited) {
  const auto config = CreateValidConfig();
  constexpr pid_t pid = 5;
  constexpr int exit_status = 7;

  // Store the exit callback at |exit_cb_at_process_manager|.
  base::OnceCallback<void(int)> exit_cb_at_process_manager;
  EXPECT_CALL(process_manager_, StartProcessInMinijail(_, _, _, _, _, _))
      .WillOnce(WithArg<5>([&exit_cb_at_process_manager](
                               base::OnceCallback<void(int)> exit_callback) {
        exit_cb_at_process_manager = std::move(exit_callback);
        return pid;
      }));

  // The callback from caller should be called with exit status when the process
  // is exited unexpectedly.
  MockCallback exit_cb_from_caller;
  EXPECT_CALL(exit_cb_from_caller, OnProcessExited(exit_status));
  EXPECT_TRUE(dhcp_server_controller_.Start(
      config, base::BindOnce(&MockCallback::OnProcessExited,
                             base::Unretained(&exit_cb_from_caller))));

  // Call the |exit_cb_at_process_manager| to simulate the process being exited
  // unexpectedly.
  std::move(exit_cb_at_process_manager).Run(exit_status);
}
}  // namespace patchpanel
