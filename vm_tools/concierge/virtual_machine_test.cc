// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <stdint.h>
#include <sys/mount.h>

#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/files/scoped_temp_dir.h>
#include <base/guid.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/single_thread_task_runner.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <google/protobuf/message.h>
#include <google/protobuf/repeated_field.h>
#include <google/protobuf/util/message_differencer.h>
#include <grpc++/grpc++.h>
#include <gtest/gtest.h>

#include "vm_tools/concierge/mac_address_generator.h"
#include "vm_tools/concierge/subnet_pool.h"
#include "vm_tools/concierge/virtual_machine.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

#include "vm_guest.grpc.pb.h"  // NOLINT(build/include)

using std::string;

namespace pb = google::protobuf;

namespace vm_tools {
namespace concierge {
namespace {

using ProcessExitBehavior = VirtualMachine::ProcessExitBehavior;
using ProcessStatus = VirtualMachine::ProcessStatus;
using Subnet = SubnetPool::Subnet;

// Converts an IPv4 address in network byte order into a string.
bool IPv4AddressToString(uint32_t addr, string* address) {
  CHECK(address);

  char buf[INET_ADDRSTRLEN];
  struct in_addr in = {
      .s_addr = addr,
  };
  if (inet_ntop(AF_INET, &in, buf, sizeof(buf)) == nullptr) {
    PLOG(ERROR) << "Failed to convert " << addr << " into a string";
    return false;
  }

  *address = buf;
  return true;
}

// Name of the unix domain socket for the grpc server.
constexpr char kServerSocket[] = "server";

// Test fixture for actually testing the VirtualMachine functionality.
class VirtualMachineTest : public ::testing::Test {
 public:
  VirtualMachineTest() = default;
  ~VirtualMachineTest() override = default;

  // Called by FakeMaitredService to indicate a test failure.
  void TestFailed(string reason);

  // Called by FakeMaitredService for checking the ConfigureNetwork RPC.
  const string& address() const { return address_; }
  const string& netmask() const { return netmask_; }
  const string& gateway() const { return gateway_; }

  // Called by FakeMaitredService the first time it receives a LaunchProcess
  // RPC.
  std::deque<vm_tools::LaunchProcessRequest> launch_requests() {
    return std::move(launch_requests_);
  }

  // Called by FakeMaitredService the first time it receives a Mount RPC.
  std::deque<vm_tools::MountRequest> mount_requests() {
    return std::move(mount_requests_);
  }

 protected:
  // ::testing::Test overrides.
  void SetUp() override;
  void TearDown() override;

 private:
  // Posted back to the main thread by the grpc thread after starting the
  // server.
  void ServerStartCallback(base::Closure quit,
                           std::shared_ptr<grpc::Server> server);

  // The message loop for the current thread.  Declared here because it must be
  // the last thing to be cleaned up.
  base::MessageLoopForIO message_loop_;

 protected:
  // Actual virtual machine being tested.
  std::unique_ptr<VirtualMachine> vm_;

  // Expected LaunchProcessRequests. Tests should fill this with all the
  // LaunchProcessRequests that it expects the VirtualMachine to receive. These
  // will be moved into the FakeMaitredService the first time it receives a
  // LaunchProcess RPC.
  std::deque<vm_tools::LaunchProcessRequest> launch_requests_;

  // Expected MountRequests.  Tests should fill this with all the MountRequests
  // that they expect a VirtualMachine to receive.  These will be moved to the
  // FakeMaitredService the firest time it receives a Mount RPC.
  std::deque<vm_tools::MountRequest> mount_requests_;

  // Set when a failure occurs.
  bool failed_{false};
  string failure_reason_;

 private:
  // Temporary directory where we will store our socket.
  base::ScopedTempDir temp_dir_;

  // Resource allocators for the VM.
  MacAddressGenerator mac_address_generator_;
  SubnetPool subnet_pool_;
  VsockCidPool vsock_cid_pool_;

  // Addresses assigned to the VM.
  string address_;
  string netmask_;
  string gateway_;

  // The thread on which the server will run.
  base::Thread server_thread_{"gRPC maitre'd thread"};

  // grpc::Server that will handle the requests.
  std::shared_ptr<grpc::Server> server_;

  base::WeakPtrFactory<VirtualMachineTest> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(VirtualMachineTest);
};

// Test server that verifies the RPCs it receives with the expected RPCs.
class FakeMaitredService final : public vm_tools::Maitred::Service {
 public:
  explicit FakeMaitredService(VirtualMachineTest* vm_test);
  ~FakeMaitredService() override = default;

  // Maitred::Service overrides.
  grpc::Status LaunchProcess(
      grpc::ServerContext* ctx,
      const vm_tools::LaunchProcessRequest* request,
      vm_tools::LaunchProcessResponse* response) override;
  grpc::Status ConfigureNetwork(grpc::ServerContext* ctx,
                                const vm_tools::NetworkConfigRequest* request,
                                vm_tools::EmptyMessage* response) override;
  grpc::Status Mount(grpc::ServerContext* ctx,
                     const vm_tools::MountRequest* request,
                     vm_tools::MountResponse* response) override;
  grpc::Status Shutdown(grpc::ServerContext* ctx,
                        const vm_tools::EmptyMessage* request,
                        vm_tools::EmptyMessage* response) override;

 private:
  // Populated the first time this class receives a LaunchProcess RPC.
  std::deque<vm_tools::LaunchProcessRequest> launch_requests_;

  // Set to true when launch_requests_ has been populated.
  bool launch_requests_initialized_;

  // Populated the first time this class receives a MountProcess RPC.
  std::deque<vm_tools::MountRequest> mount_requests_;

  // Set to true when mount_requests_ has been populated.
  bool mount_requests_initialized_;

  // Non-owning pointer to the test fixture.  Valid for the lifetime of this
  // object because this lives on the grpc thread, which is a member of the test
  // fixture.  The cross-thread access is safe because all the VirtualMachine
  // RPCs are synchronous, which means the main thread will be blocked while the
  // grpc thread is processing the RPC.
  VirtualMachineTest* vm_test_;

  DISALLOW_COPY_AND_ASSIGN(FakeMaitredService);
};

FakeMaitredService::FakeMaitredService(VirtualMachineTest* vm_test)
    : launch_requests_initialized_(false),
      mount_requests_initialized_(false),
      vm_test_(vm_test) {}

grpc::Status FakeMaitredService::LaunchProcess(
    grpc::ServerContext* ctx,
    const vm_tools::LaunchProcessRequest* request,
    vm_tools::LaunchProcessResponse* response) {
  if (!launch_requests_initialized_) {
    launch_requests_ = vm_test_->launch_requests();
    launch_requests_initialized_ = true;
  }

  if (launch_requests_.empty()) {
    vm_test_->TestFailed("Received LaunchProcessRequest with empty deque");
    return grpc::Status::OK;
  }

  vm_tools::LaunchProcessRequest expected = std::move(launch_requests_.front());
  launch_requests_.pop_front();

  string difference;
  pb::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  if (!differencer.Compare(expected, *request)) {
    vm_test_->TestFailed("Mismatched LaunchProcessRequests: " + difference);
  }
  return grpc::Status::OK;
}

grpc::Status FakeMaitredService::ConfigureNetwork(
    grpc::ServerContext* ctx,
    const vm_tools::NetworkConfigRequest* request,
    vm_tools::EmptyMessage* response) {
  string address;
  if (!IPv4AddressToString(request->ipv4_config().address(), &address)) {
    vm_test_->TestFailed(
        base::StringPrintf("Failed to parse address %u into a string",
                           request->ipv4_config().address()));
    return grpc::Status::OK;
  }
  if (address != vm_test_->address()) {
    vm_test_->TestFailed(
        base::StringPrintf("Mismatched addresses: expected %s got %s",
                           vm_test_->address().c_str(), address.c_str()));
    return grpc::Status::OK;
  }

  string netmask;
  if (!IPv4AddressToString(request->ipv4_config().netmask(), &netmask)) {
    vm_test_->TestFailed(
        base::StringPrintf("Failed to parse netmask %u into a string",
                           request->ipv4_config().netmask()));
    return grpc::Status::OK;
  }
  if (netmask != vm_test_->netmask()) {
    vm_test_->TestFailed(
        base::StringPrintf("Mismatched netmasks: expected %s got %s",
                           vm_test_->netmask().c_str(), netmask.c_str()));
    return grpc::Status::OK;
  }

  string gateway;
  if (!IPv4AddressToString(request->ipv4_config().gateway(), &gateway)) {
    vm_test_->TestFailed(
        base::StringPrintf("Failed to parse gateway %u into a string",
                           request->ipv4_config().gateway()));
    return grpc::Status::OK;
  }
  if (gateway != vm_test_->gateway()) {
    vm_test_->TestFailed(
        base::StringPrintf("Mismatched gateways: expected %s got %s",
                           vm_test_->gateway().c_str(), gateway.c_str()));
    return grpc::Status::OK;
  }

  return grpc::Status::OK;
}

grpc::Status FakeMaitredService::Mount(grpc::ServerContext* ctx,
                                       const vm_tools::MountRequest* request,
                                       vm_tools::MountResponse* response) {
  if (!mount_requests_initialized_) {
    mount_requests_ = vm_test_->mount_requests();
    mount_requests_initialized_ = true;
  }

  if (mount_requests_.empty()) {
    vm_test_->TestFailed("Received MountRequest with empty deque");
    return grpc::Status::OK;
  }

  vm_tools::MountRequest expected = std::move(mount_requests_.front());
  mount_requests_.pop_front();

  string difference;
  pb::util::MessageDifferencer differencer;
  differencer.ReportDifferencesToString(&difference);
  if (!differencer.Compare(expected, *request)) {
    vm_test_->TestFailed("Mismatched MountRequests: " + difference);
  }
  return grpc::Status::OK;
}

grpc::Status FakeMaitredService::Shutdown(grpc::ServerContext* ctx,
                                          const vm_tools::EmptyMessage* request,
                                          vm_tools::EmptyMessage* response) {
  return grpc::Status::OK;
}

// Runs on the grpc thread and starts the grpc server.
void StartFakeMaitredService(
    VirtualMachineTest* vm_test,
    scoped_refptr<base::SingleThreadTaskRunner> main_task_runner,
    base::FilePath listen_path,
    base::Callback<void(std::shared_ptr<grpc::Server>)> server_cb) {
  FakeMaitredService maitred(vm_test);

  grpc::ServerBuilder builder;
  builder.AddListeningPort("unix:" + listen_path.value(),
                           grpc::InsecureServerCredentials());
  builder.RegisterService(&maitred);

  std::shared_ptr<grpc::Server> server(builder.BuildAndStart().release());
  main_task_runner->PostTask(FROM_HERE, base::Bind(server_cb, server));

  if (server) {
    // This will not race with shutdown because the grpc server code includes a
    // check to see if the server has already been shut down (or is shutting
    // down) when Wait is called.
    server->Wait();
  }
}

void VirtualMachineTest::SetUp() {
  // Create the temporary directory.
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

  // Start the FakeMaitredService on a different thread.
  base::RunLoop run_loop;

  ASSERT_TRUE(server_thread_.Start());
  server_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &StartFakeMaitredService, this, base::ThreadTaskRunnerHandle::Get(),
          temp_dir_.GetPath().Append(kServerSocket),
          base::Bind(&VirtualMachineTest::ServerStartCallback,
                     weak_factory_.GetWeakPtr(), run_loop.QuitClosure())));

  run_loop.Run();

  ASSERT_TRUE(server_);

  // Create the stub to the FakeMaitredService.
  std::unique_ptr<vm_tools::Maitred::Stub> stub =
      vm_tools::Maitred::NewStub(grpc::CreateChannel(
          "unix:" + temp_dir_.GetPath().Append(kServerSocket).value(),
          grpc::InsecureChannelCredentials()));
  ASSERT_TRUE(stub);

  // Allocate resources for the VM.
  MacAddress mac_addr = mac_address_generator_.Generate();
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  std::unique_ptr<Subnet> subnet = subnet_pool_.AllocateVM();

  ASSERT_TRUE(subnet);

  ASSERT_TRUE(IPv4AddressToString(subnet->AddressAtOffset(1), &address_));
  ASSERT_TRUE(IPv4AddressToString(subnet->Netmask(), &netmask_));
  ASSERT_TRUE(IPv4AddressToString(subnet->AddressAtOffset(0), &gateway_));

  // Create the VirtualMachine.
  vm_ = VirtualMachine::CreateForTesting(std::move(mac_addr), std::move(subnet),
                                         vsock_cid, temp_dir_.GetPath(),
                                         std::move(stub));
  ASSERT_TRUE(vm_);
}

void VirtualMachineTest::TearDown() {
  // Do the opposite of SetUp to make sure things get cleaned up in the right
  // order.
  vm_.reset();
  server_->Shutdown();
  server_.reset();
  server_thread_.Stop();
}

void VirtualMachineTest::ServerStartCallback(
    base::Closure quit, std::shared_ptr<grpc::Server> server) {
  server_.swap(server);
  quit.Run();
}

void VirtualMachineTest::TestFailed(string reason) {
  failed_ = true;
  failure_reason_ = std::move(reason);
}

}  // namespace

TEST_F(VirtualMachineTest, ConfigureNetwork) {
  ASSERT_TRUE(vm_->ConfigureNetwork({"8.8.8.8"}, {}));

  EXPECT_FALSE(failed_) << "Failure reason: " << failure_reason_;
}

TEST_F(VirtualMachineTest, LaunchProcess) {
  struct {
    std::vector<string> argv;
    std::map<string, string> env;
    bool respawn;
    bool wait_for_exit;
  } messages[] = {
      {
          .argv = {"test", "program"},
          .env = {{"HELLO", "WORLD"}},
          .respawn = true,
          .wait_for_exit = false,
      },
      {
          .argv = {"other", "program"},
          .env = {},
          .respawn = false,
          .wait_for_exit = false,
      },
      {
          .argv = {"synchronous", "program"},
          .env = {{"test", "env"}},
          .respawn = false,
          .wait_for_exit = true,
      },
  };

  // Build up the expected protobufs.
  for (const auto& msg : messages) {
    vm_tools::LaunchProcessRequest request;

    google::protobuf::RepeatedPtrField<string> argv(msg.argv.begin(),
                                                    msg.argv.end());
    request.mutable_argv()->Swap(&argv);
    google::protobuf::Map<string, string> env(msg.env.begin(), msg.env.end());
    request.mutable_env()->swap(env);
    request.set_respawn(msg.respawn);
    request.set_wait_for_exit(msg.wait_for_exit);

    launch_requests_.emplace_back(std::move(request));
  }

  // Now make the requests.
  for (auto& msg : messages) {
    if (msg.wait_for_exit) {
      vm_->RunProcess(std::move(msg.argv), std::move(msg.env));
    } else {
      vm_->StartProcess(std::move(msg.argv), std::move(msg.env),
                        msg.respawn ? ProcessExitBehavior::RESPAWN_ON_EXIT
                                    : ProcessExitBehavior::ONE_SHOT);
    }

    EXPECT_FALSE(failed_) << "Failure reason: " << failure_reason_;
  }
}

TEST_F(VirtualMachineTest, Mount) {
  struct {
    const char* source;
    const char* target;
    const char* fstype;
    uint64_t flags;
    const char* opts;
  } mounts[] = {
      {
          .source = "100.115.92.5:/my/home/directory",
          .target = "/mnt/shared",
          .fstype = "nfs",
          .flags = 0,
          .opts = "nolock,vers=3,addr=100.115.92.5",
      },
      {
          .source = "/dev/vdb",
          .target = "/mnt/container_rootfs",
          .fstype = "ext4",
          .flags = MS_RDONLY,
          .opts = "",
      },
  };

  // Build the expected protobufs.
  for (const auto& mt : mounts) {
    vm_tools::MountRequest request;

    request.set_source(mt.source);
    request.set_target(mt.target);
    request.set_fstype(mt.fstype);
    request.set_mountflags(mt.flags);
    request.set_options(mt.opts);

    mount_requests_.emplace_back(std::move(request));
  }

  // Make the requests.
  for (const auto& mt : mounts) {
    ASSERT_TRUE(vm_->Mount(mt.source, mt.target, mt.fstype, mt.flags, mt.opts));

    EXPECT_FALSE(failed_) << "Failure reason: " << failure_reason_;
  }
}

}  // namespace concierge
}  // namespace vm_tools
