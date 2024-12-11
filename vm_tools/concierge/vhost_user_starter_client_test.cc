// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vhost_user_starter_client.h"

#include <sys/socket.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace vm_tools::concierge {
namespace {

class VhostUserStarterClientTest : public testing::Test {
 public:
  VhostUserStarterClientTest() = default;
  void SetUp() override {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    mock_bus_ = new dbus::MockBus(options);

    vhost_user_starter_proxy_ = new dbus::MockObjectProxy(
        mock_bus_.get(), vm_tools::vhost_user_starter::kVhostUserStarterName,
        dbus::ObjectPath(vm_tools::vhost_user_starter::kVhostUserStarterPath));

    EXPECT_CALL(*mock_bus_.get(),
                GetObjectProxy(
                    vm_tools::vhost_user_starter::kVhostUserStarterName,
                    dbus::ObjectPath(
                        vm_tools::vhost_user_starter::kVhostUserStarterPath)))
        .WillOnce(testing::Return(vhost_user_starter_proxy_.get()));

    // Sets an expectation that the mock proxy's CallMethod() will use
    // CreateMockProxyResponse() to return responses.
    EXPECT_CALL(*vhost_user_starter_proxy_.get(),
                DoCallMethodWithErrorCallback(_, _, _, _))
        .WillOnce(
            Invoke(this, &VhostUserStarterClientTest::CreateMockProxyResponse));
  }

 protected:
  void CreateMockProxyResponse(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback* response_callback,
      dbus::ObjectProxy::ErrorCallback* error_callback) {
    EXPECT_EQ(method_call->GetInterface(),
              vm_tools::vhost_user_starter::kVhostUserStarterInterface);
    EXPECT_EQ(method_call->GetMember(),
              vm_tools::vhost_user_starter::kStartVhostUserFsMethod);

    std::unique_ptr<dbus::Response> response = dbus::Response::CreateEmpty();
    vhost_user_starter::StartVhostUserFsResponse reply;

    if (!dbus::MessageWriter(response.get()).AppendProtoAsArrayOfBytes(reply)) {
      LOG(ERROR) << "Failed to encode RegisterSuspendDelayReply";
    }

    std::move(*response_callback).Run(response.get());
  }

  base::test::TaskEnvironment task_environment_;

  scoped_refptr<dbus::MockBus> mock_bus_;
  scoped_refptr<dbus::MockObjectProxy> vhost_user_starter_proxy_;
};

// Tests that the StartVhostUserFs method successfully sends the
// StartVhostUserFs request.
TEST_F(VhostUserStarterClientTest, StartVhostUserFs) {
  std::unique_ptr<VhostUserStarterClient> client =
      std::make_unique<VhostUserStarterClient>(mock_bus_);

  base::FilePath data_dir = base::FilePath("test");
  const std::vector<uid_t> privileged_quota_uids = {0};
  SharedDataParam test_param{.data_dir = data_dir,
                             .tag = "stub",
                             .uid_map = "0 0 1",
                             .gid_map = "0 0 1",
                             .enable_caches = SharedDataParam::Cache::kAuto,
                             .ascii_casefold = true,
                             .posix_acl = false,
                             .privileged_quota_uids = privileged_quota_uids};
  // Set up vhost-user-virtio-fs device
  int fds[2];
  socketpair(AF_UNIX, SOCK_STREAM, 0 /* protocol */, fds);
  base::ScopedFD _ = base::ScopedFD(fds[0]);
  client->StartVhostUserFs(base::ScopedFD(fds[1]), test_param);

  task_environment_.RunUntilIdle();
  EXPECT_EQ(client->GetStartedDeviceCount(), 1);
}
}  // namespace
}  // namespace vm_tools::concierge
