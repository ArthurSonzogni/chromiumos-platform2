// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "system-proxy/system_proxy_adaptor.h"

#include <list>
#include <map>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/weak_ptr.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/message_loops/base_message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <dbus/kerberos/dbus-constants.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/object_path.h>
#include <dbus/system_proxy/dbus-constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "bindings/worker_common.pb.h"
#include "system-proxy/kerberos_client.h"
#include "system-proxy/protobuf_util.h"
#include "system-proxy/sandboxed_worker.h"
#include "system_proxy/proto_bindings/system_proxy_service.pb.h"

using testing::_;
using testing::Return;

namespace system_proxy {
namespace {
const char kUser[] = "proxy_user";
const char kPassword[] = "proxy_password";
const char kPrincipalName[] = "user@TEST";
const char kLocalProxyHostPort[] = "local.proxy.url:3128";
const char kObjectPath[] = "/object/path";

// Stub completion callback for RegisterAsync().
void DoNothing(bool /* unused */) {}

}  // namespace

class FakeSandboxedWorker : public SandboxedWorker {
 public:
  explicit FakeSandboxedWorker(base::WeakPtr<SystemProxyAdaptor> adaptor)
      : SandboxedWorker(adaptor) {}
  FakeSandboxedWorker(const FakeSandboxedWorker&) = delete;
  FakeSandboxedWorker& operator=(const FakeSandboxedWorker&) = delete;
  ~FakeSandboxedWorker() override = default;

  bool Start() override { return is_running_ = true; }
  bool Stop() override {
    is_running_ = false;
    return true;
  }
  bool IsRunning() override { return is_running_; }

  std::string local_proxy_host_and_port() override {
    return kLocalProxyHostPort;
  }

 private:
  bool is_running_;
};

class FakeSystemProxyAdaptor : public SystemProxyAdaptor {
 public:
  FakeSystemProxyAdaptor(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
      : SystemProxyAdaptor(std::move(dbus_object)), weak_ptr_factory_(this) {}
  FakeSystemProxyAdaptor(const FakeSystemProxyAdaptor&) = delete;
  FakeSystemProxyAdaptor& operator=(const FakeSystemProxyAdaptor&) = delete;
  ~FakeSystemProxyAdaptor() override = default;

  void SetResolvedProxyServers(std::vector<std::string> proxy_servers) {
    resolved_proxy_servers_ = std::move(proxy_servers);
  }

  void GetChromeProxyServersAsync(
      const std::string& target_url,
      brillo::http::GetChromeProxyServersCallback callback) override {
    std::move(callback).Run(/*success=*/true, resolved_proxy_servers_);
  }

  std::unique_ptr<SandboxedWorker> CreateWorker() override {
    ++create_worker_count_;
    return std::make_unique<FakeSandboxedWorker>(
        weak_ptr_factory_.GetWeakPtr());
  }

 protected:
  void ConnectNamespace(bool user_traffic) override {
    OnNamespaceConnected(GetWorker(user_traffic), user_traffic);
  }
  std::unique_ptr<patchpanel::Client> CreatePatchpanelClient() override {
    create_patchpanel_client_called_ = true;
    return SystemProxyAdaptor::CreatePatchpanelClient();
  }

  bool create_patchpanel_client_called_ = false;

 private:
  FRIEND_TEST(SystemProxyAdaptorTest, KerberosEnabled);
  FRIEND_TEST(SystemProxyAdaptorTest, ConnectNamespace);
  FRIEND_TEST(SystemProxyAdaptorTest, ConnectNamespaceTaskDestroyedWorker);
  FRIEND_TEST(SystemProxyAdaptorTest, ProxyResolutionFilter);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceAuthenticationRequired);
  FRIEND_TEST(SystemProxyAdaptorTest, ProtectionSpaceNoCredentials);
  FRIEND_TEST(SystemProxyAdaptorTest, ClearUserCredentials);
  FRIEND_TEST(SystemProxyAdaptorTest, ClearUserCredentialsRestartService);

  int create_worker_count_ = 0;
  std::vector<std::string> resolved_proxy_servers_;
  base::WeakPtrFactory<FakeSystemProxyAdaptor> weak_ptr_factory_;
};

class SystemProxyAdaptorTest : public ::testing::Test {
 public:
  SystemProxyAdaptorTest() {
    const dbus::ObjectPath object_path(kObjectPath);

    // Mock out D-Bus initialization.
    mock_exported_object_ =
        base::MakeRefCounted<dbus::MockExportedObject>(bus_.get(), object_path);

    EXPECT_CALL(*bus_, GetExportedObject(_))
        .WillRepeatedly(Return(mock_exported_object_.get()));

    EXPECT_CALL(*mock_exported_object_, ExportMethod(_, _, _, _))
        .Times(testing::AnyNumber());

    mock_kerberos_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), kerberos::kKerberosServiceName,
        dbus::ObjectPath(kerberos::kKerberosServicePath));
    EXPECT_CALL(*bus_, GetObjectProxy(kerberos::kKerberosServiceName, _))
        .WillRepeatedly(Return(mock_kerberos_proxy_.get()));

    adaptor_.reset(new FakeSystemProxyAdaptor(
        std::make_unique<brillo::dbus_utils::DBusObject>(nullptr, bus_,
                                                         object_path)));
    adaptor_->RegisterAsync(base::BindRepeating(&DoNothing));
    mock_patchpanel_proxy_ = base::MakeRefCounted<dbus::MockObjectProxy>(
        bus_.get(), patchpanel::kPatchPanelServiceName,
        dbus::ObjectPath(patchpanel::kPatchPanelServicePath));
    brillo_loop_.SetAsCurrent();
  }
  SystemProxyAdaptorTest(const SystemProxyAdaptorTest&) = delete;
  SystemProxyAdaptorTest& operator=(const SystemProxyAdaptorTest&) = delete;
  ~SystemProxyAdaptorTest() override = default;

  void AddCredentialsToAuthCache(
      const worker::ProtectionSpace& protection_space,
      const std::string& username,
      const std::string& password) {
    Credentials credentials;
    credentials.set_username(username);
    credentials.set_password(password);
    mock_auth_cache_[protection_space.SerializeAsString()] = credentials;
  }

  void OnWorkerActive(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.SystemProxy");
    EXPECT_EQ(signal->GetMember(), "WorkerActive");
    active_worker_signal_called_ = true;

    dbus::MessageReader signal_reader(signal);
    system_proxy::WorkerActiveSignalDetails details;
    EXPECT_TRUE(signal_reader.PopArrayOfBytesAsProto(&details));
    EXPECT_EQ(kLocalProxyHostPort, details.local_proxy_url());
  }

  void HandleAuthenticationRequiredSignal(dbus::Signal* signal) {
    EXPECT_EQ(signal->GetInterface(), "org.chromium.SystemProxy");
    EXPECT_EQ(signal->GetMember(), "AuthenticationRequired");

    dbus::MessageReader signal_reader(signal);
    system_proxy::AuthenticationRequiredDetails details;
    EXPECT_TRUE(signal_reader.PopArrayOfBytesAsProto(&details));

    Credentials credentials;

    auto it = mock_auth_cache_.find(
        details.proxy_protection_space().SerializeAsString());

    if (it != mock_auth_cache_.end()) {
      credentials = it->second;
    } else {
      credentials.set_username("");
      credentials.set_password("");
    }

    SetAuthenticationDetailsRequest request;
    *request.mutable_credentials() = credentials;
    *request.mutable_protection_space() = details.proxy_protection_space();
    request.set_traffic_type(TrafficOrigin::SYSTEM);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    adaptor_->SetAuthenticationDetails(proto_blob);
  }

  void OnAuthenticationRequired(dbus::Signal* signal) {
    HandleAuthenticationRequiredSignal(signal);
    ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));
  }

  struct WorkerTestPipes {
    base::ScopedFD stdin_read_fd;
    base::ScopedFD stdout_write_fd;
  };

  SandboxedWorker* StartWorkerForTest(bool user_traffic,
                                      WorkerTestPipes* pipes) {
    int stdin_fds[2];
    int stdout_fds[2];
    CHECK(base::CreateLocalNonBlockingPipe(stdin_fds));
    CHECK(base::CreateLocalNonBlockingPipe(stdout_fds));
    pipes->stdin_read_fd.reset(stdin_fds[0]);
    pipes->stdout_write_fd.reset(stdout_fds[1]);

    adaptor_->SetWorker(user_traffic, adaptor_->CreateWorker());
    base::WeakPtr<SandboxedWorker> worker = adaptor_->GetWorker(user_traffic);
    CHECK(worker);
    worker->Start();
    worker->SetPipesForTesting(base::ScopedFD(stdin_fds[1]),
                               base::ScopedFD(stdout_fds[0]));
    return worker.get();
  }

  bool ResetWorkerForTest(bool user_traffic) {
    return adaptor_->ResetWorker(user_traffic);
  }

  void SendProxyResolutionRequest(int stdout_write_fd,
                                  const std::string& target_url) {
    worker::WorkerRequest request;
    request.mutable_proxy_resolution_request()->set_target_url(target_url);
    ASSERT_TRUE(WriteProtobuf(stdout_write_fd, request));
    brillo_loop_.RunOnce(/*may_block=*/false);
  }

  void SendAuthRequiredRequest(int stdout_write_fd,
                               const worker::ProtectionSpace& protection_space,
                               bool bad_cached_credentials = false) {
    worker::WorkerRequest request;
    auto* auth_request = request.mutable_auth_required_request();
    *auth_request->mutable_protection_space() = protection_space;
    auth_request->set_bad_cached_credentials(bad_cached_credentials);
    ASSERT_TRUE(WriteProtobuf(stdout_write_fd, request));
    // First iteration dispatches the readable pipe watcher; second iteration
    // runs any SetCredentialsTask posted in response to AuthenticationRequired.
    brillo_loop_.RunOnce(/*may_block=*/false);
    brillo_loop_.RunOnce(/*may_block=*/false);
  }

 protected:
  bool active_worker_signal_called_ = false;
  std::map<std::string, Credentials> mock_auth_cache_;
  scoped_refptr<dbus::MockBus> bus_ = new dbus::MockBus(dbus::Bus::Options());
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  // SystemProxyAdaptor instance that creates fake worker processes.
  std::unique_ptr<FakeSystemProxyAdaptor> adaptor_;

  scoped_refptr<dbus::MockObjectProxy> mock_patchpanel_proxy_;
  scoped_refptr<dbus::MockObjectProxy> mock_kerberos_proxy_;

  base::SingleThreadTaskExecutor task_executor_{base::MessagePumpType::IO};
  brillo::BaseMessageLoop brillo_loop_{task_executor_.task_runner()};
};

// Verifies if System-proxy starts the system and user traffic workers and sets
// the credentials for both workers.
TEST_F(SystemProxyAdaptorTest, SetAuthenticationDetails) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .Times(2)
      .WillRepeatedly(Return(mock_patchpanel_proxy_.get()));

  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetAuthenticationDetailsRequest request;
  Credentials credentials;
  credentials.set_username(kUser);
  credentials.set_password(kPassword);
  credentials.add_policy_credentials_auth_schemes("basic");
  credentials.add_policy_credentials_auth_schemes("digest");

  *request.mutable_credentials() = credentials;
  request.set_traffic_type(TrafficOrigin::ALL);

  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetAuthenticationDetails(proto_blob);
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));

  ASSERT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());
  ASSERT_TRUE(adaptor_->arc_worker_.get());
  EXPECT_TRUE(adaptor_->arc_worker_->IsRunning());

  // Verify that credentials were set for user and system traffic.
  int fds_system[2], fds_user[2];
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(fds_system));
  base::ScopedFD read_scoped_fd_system(fds_system[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds_system[1]);
  EXPECT_TRUE(base::CreateLocalNonBlockingPipe(fds_user));
  base::ScopedFD read_scoped_fd_user(fds_user[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->arc_worker_->stdin_pipe_.reset(fds_user[1]);

  adaptor_->SetAuthenticationDetails(proto_blob);
  // Process the tasks which send credentials to both workers via communication
  // pipes.
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd_system.get(), &config));
  EXPECT_TRUE(config.has_credentials());
  EXPECT_EQ(config.credentials().username(), kUser);
  EXPECT_EQ(config.credentials().password(), kPassword);

  worker::WorkerConfigs config_user;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd_user.get(), &config_user));
  EXPECT_TRUE(config_user.has_credentials());
  EXPECT_EQ(config_user.credentials().username(), kUser);
  EXPECT_EQ(config_user.credentials().password(), kPassword);
  EXPECT_GT(credentials.policy_credentials_auth_schemes().size(), 0);
  EXPECT_EQ(credentials.policy_credentials_auth_schemes().Get(0), "basic");
  EXPECT_EQ(credentials.policy_credentials_auth_schemes().Get(1), "digest");
}

// Verifies if System-proxy only starts the worker which tunnels system traffic.
TEST_F(SystemProxyAdaptorTest, SetAuthenticationDetailsOnlySystemTraffic) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));

  EXPECT_FALSE(adaptor_->system_services_worker_.get());
  SetAuthenticationDetailsRequest request;
  Credentials credentials;
  credentials.set_username(kUser);
  credentials.set_password(kPassword);
  *request.mutable_credentials() = credentials;
  request.set_traffic_type(TrafficOrigin::SYSTEM);

  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  adaptor_->SetAuthenticationDetails(proto_blob);
  ASSERT_TRUE(brillo_loop_.RunOnce(/*may_block=*/false));

  ASSERT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_TRUE(adaptor_->system_services_worker_->IsRunning());
  EXPECT_FALSE(adaptor_->arc_worker_);
}

TEST_F(SystemProxyAdaptorTest, KerberosEnabled) {
  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  ASSERT_TRUE(adaptor_->system_services_worker_.get());

  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);

  SetAuthenticationDetailsRequest request;
  request.set_kerberos_enabled(true);
  request.set_active_principal_name(kPrincipalName);
  request.set_traffic_type(TrafficOrigin::SYSTEM);

  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  // First create a worker object.
  adaptor_->SetAuthenticationDetails(proto_blob);
  brillo_loop_.RunOnce(false);

  // Expect that the availability of kerberos auth has been sent to the worker.
  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_kerberos_config());
  EXPECT_TRUE(config.kerberos_config().enabled());
  EXPECT_EQ(config.kerberos_config().krb5cc_path(), "/tmp/ccache");
  EXPECT_EQ(config.kerberos_config().krb5conf_path(), "/tmp/krb5.conf");

  // Expect that the availability of kerberos auth has been sent to the kerberos
  // client.
  EXPECT_TRUE(adaptor_->kerberos_client_->kerberos_enabled_);
  EXPECT_EQ(adaptor_->kerberos_client_->principal_name_, kPrincipalName);
}

TEST_F(SystemProxyAdaptorTest, ShutDownProcess) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .Times(2)
      .WillRepeatedly(Return(mock_patchpanel_proxy_.get()));
  adaptor_->CreateWorkerIfNeeded(/*user_traffic=*/false);
  adaptor_->CreateWorkerIfNeeded(/*user_traffic=*/true);
  EXPECT_TRUE(adaptor_->system_services_worker_);
  EXPECT_TRUE(adaptor_->arc_worker_);

  ShutDownRequest request;
  request.set_traffic_type(TrafficOrigin::ALL);
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());
  adaptor_->ShutDownProcess(proto_blob);

  EXPECT_FALSE(adaptor_->system_services_worker_);
  EXPECT_FALSE(adaptor_->arc_worker_);
}

// Verifies that only the worker that tunnels ARC traffic is shut down.
TEST_F(SystemProxyAdaptorTest, ShutDownArc) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .Times(2)
      .WillRepeatedly(Return(mock_patchpanel_proxy_.get()));
  adaptor_->CreateWorkerIfNeeded(/*user_traffic=*/false);
  adaptor_->CreateWorkerIfNeeded(/*user_traffic=*/true);
  EXPECT_TRUE(adaptor_->system_services_worker_);
  EXPECT_TRUE(adaptor_->arc_worker_);

  ShutDownRequest request;
  request.set_traffic_type(TrafficOrigin::USER);
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());
  adaptor_->ShutDownProcess(proto_blob);

  EXPECT_TRUE(adaptor_->system_services_worker_);
  EXPECT_FALSE(adaptor_->arc_worker_);
}

TEST_F(SystemProxyAdaptorTest, ConnectNamespace) {
  EXPECT_FALSE(active_worker_signal_called_);
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(Invoke(this, &SystemProxyAdaptorTest::OnWorkerActive));

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  adaptor_->ConnectNamespace(/* user_traffic= */ false);
  EXPECT_TRUE(active_worker_signal_called_);
}

TEST_F(SystemProxyAdaptorTest, ConnectNamespaceTaskDestroyedWorker) {
  EXPECT_FALSE(active_worker_signal_called_);
  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  adaptor_->netns_reconnect_attempts_available_ = 1;

  adaptor_->SystemProxyAdaptor::ConnectNamespace(/* user_traffic= */ false);

  // Destroy the worker while the delayed task is pending in the message loop,
  // simulating ShutDownProcess being called during the 1-second delay window.
  adaptor_->ResetWorker(/* user_traffic= */ false);
  EXPECT_FALSE(adaptor_->GetWorker(/* user_traffic= */ false));

  adaptor_->ConnectNamespaceTask(base::WeakPtr<SandboxedWorker>(),
                                 /* user_traffic= */ false);
  EXPECT_FALSE(adaptor_->create_patchpanel_client_called_);
  EXPECT_FALSE(active_worker_signal_called_);
}

// Test that verifies that authentication requests are result in sending a
// signal to notify credentials are missing and credentials and protection space
// if correctly forwarded to the worker processes.
TEST_F(SystemProxyAdaptorTest, ProtectionSpaceAuthenticationRequired) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(
          Invoke(this, &SystemProxyAdaptorTest::OnAuthenticationRequired));

  worker::ProtectionSpace protection_space;
  protection_space.set_origin("http://test.proxy.com");
  protection_space.set_realm("my realm");
  protection_space.set_scheme("basic");
  std::string msg;
  protection_space.SerializeToString(&msg);
  AddCredentialsToAuthCache(protection_space, kUser, kPassword);

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);
  adaptor_->RequestAuthenticationCredentials(protection_space,
                                             /* bad_credentials = */ false);

  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());

  const worker::Credentials& reply = config.credentials();
  EXPECT_TRUE(reply.has_protection_space());
  EXPECT_EQ(reply.username(), kUser);
  EXPECT_EQ(reply.password(), kPassword);
  EXPECT_EQ(reply.protection_space().SerializeAsString(),
            protection_space.SerializeAsString());
}

// Test that verifies that authentication requests that resolve to an empty
// credentials set are forwarded to the worker processes.
TEST_F(SystemProxyAdaptorTest, ProtectionSpaceNoCredentials) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(
          Invoke(this, &SystemProxyAdaptorTest::OnAuthenticationRequired));

  worker::ProtectionSpace protection_space;
  protection_space.set_origin("http://test.proxy.com");
  protection_space.set_realm("my realm");
  protection_space.set_scheme("basic");
  std::string msg;
  protection_space.SerializeToString(&msg);

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);
  adaptor_->RequestAuthenticationCredentials(protection_space,
                                             /* bad_credentials = */ false);

  brillo_loop_.RunOnce(false);

  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_credentials());

  const worker::Credentials& reply = config.credentials();
  EXPECT_TRUE(reply.has_protection_space());
  EXPECT_EQ(reply.username(), "");
  EXPECT_EQ(reply.password(), "");
  EXPECT_EQ(reply.protection_space().SerializeAsString(),
            protection_space.SerializeAsString());
}

TEST_F(SystemProxyAdaptorTest, ClearUserCredentials) {
  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  ASSERT_TRUE(adaptor_->system_services_worker_.get());

  int fds[2];
  ASSERT_TRUE(base::CreateLocalNonBlockingPipe(fds));
  base::ScopedFD read_scoped_fd(fds[0]);
  // Reset the worker stdin pipe to read the input from the other endpoint.
  adaptor_->system_services_worker_->stdin_pipe_.reset(fds[1]);

  ClearUserCredentialsRequest request;
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());
  adaptor_->ClearUserCredentials(proto_blob);
  brillo_loop_.RunOnce(false);

  // Expect that a request to clear user credentials has been sent to the
  // worker.
  worker::WorkerConfigs config;
  ASSERT_TRUE(ReadProtobuf(read_scoped_fd.get(), &config));
  EXPECT_TRUE(config.has_clear_user_credentials());
}

// Tests that the sandboxed worker is restarted if the request to clear the
// credentials fails.
TEST_F(SystemProxyAdaptorTest, ClearUserCredentialsRestartService) {
  EXPECT_CALL(*bus_, GetObjectProxy(patchpanel::kPatchPanelServiceName, _))
      .WillOnce(Return(mock_patchpanel_proxy_.get()));

  adaptor_->system_services_worker_ = adaptor_->CreateWorker();
  ASSERT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_EQ(1, adaptor_->create_worker_count_);

  ClearUserCredentialsRequest request;
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());
  // This request will fail because we didn't set up a communication pipe.
  adaptor_->ClearUserCredentials(proto_blob);
  brillo_loop_.RunOnce(false);

  ASSERT_TRUE(adaptor_->system_services_worker_.get());
  EXPECT_EQ(2, adaptor_->create_worker_count_);
}

// Verifies that OnProxyResolved filters out unsupported proxy schemes, forwards
// HTTP and direct entries in ProxyResolutionReply, and only authorizes HTTP
// origins for subsequent AuthRequiredRequest messages.
TEST_F(SystemProxyAdaptorTest, ProxyResolutionFilter) {
  WorkerTestPipes pipes;
  StartWorkerForTest(/*user_traffic=*/false, &pipes);

  adaptor_->SetResolvedProxyServers({
      "http://proxy1.example.com:3128",
      "http://",
      std::string("http://\xff\xfe:3128"),
      "socks4://socks.example.com:1080",
      "socks5://socks5.example.com:1080",
      "https://secure-proxy.example.com:443",
      "http://proxy2.example.com:8080",
      "direct://",
  });

  SendProxyResolutionRequest(pipes.stdout_write_fd.get(),
                             "https://destination.example.com:443");

  worker::WorkerConfigs reply_config;
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &reply_config));
  ASSERT_TRUE(reply_config.has_proxy_resolution_reply());
  const auto& resolved = reply_config.proxy_resolution_reply().proxy_servers();
  ASSERT_EQ(3, resolved.size());
  EXPECT_EQ("http://proxy1.example.com:3128", resolved.Get(0));
  EXPECT_EQ("http://proxy2.example.com:8080", resolved.Get(1));
  EXPECT_EQ("direct://", resolved.Get(2));

  // Both resolved HTTP proxies should be authorized for AuthRequiredRequest.
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .Times(2)
      .WillRepeatedly(Invoke(
          this, &SystemProxyAdaptorTest::HandleAuthenticationRequiredSignal));

  for (const char* origin :
       {"http://proxy1.example.com:3128", "http://proxy2.example.com:8080"}) {
    worker::ProtectionSpace protection_space;
    protection_space.set_origin(origin);
    protection_space.set_realm("CorpRealm");
    protection_space.set_scheme("basic");
    AddCredentialsToAuthCache(protection_space, kUser, kPassword);

    SendAuthRequiredRequest(pipes.stdout_write_fd.get(), protection_space);

    worker::WorkerConfigs cred_config;
    ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &cred_config));
    ASSERT_TRUE(cred_config.has_credentials());
    EXPECT_EQ(kUser, cred_config.credentials().username());
    EXPECT_EQ(kPassword, cred_config.credentials().password());
  }
}

// Verifies that AuthRequiredRequest messages from a worker with origins that
// were not resolved for that worker (including before any resolution, forged
// cross-origin proxies, "direct://", bare "http://", port/scheme/case
// mismatches, missing protection_space, or non-UTF-8 bytes) are rejected and do
// not emit the AuthenticationRequired D-Bus signal.
TEST_F(SystemProxyAdaptorTest, AuthRequiredRejectedForUnauthorizedOrigins) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_)).Times(0);

  WorkerTestPipes pipes;
  StartWorkerForTest(/*user_traffic=*/false, &pipes);

  // 1. Forged request before any ProxyResolutionRequest is ever made.
  worker::ProtectionSpace victim_ps;
  victim_ps.set_origin("http://victim-proxy.corp:3128");
  victim_ps.set_realm("CorpNet");
  victim_ps.set_scheme("basic");
  AddCredentialsToAuthCache(victim_ps, kUser, kPassword);
  SendAuthRequiredRequest(pipes.stdout_write_fd.get(), victim_ps);

  // 2. Resolve an allowed proxy (plus malformed bare "http://" and
  // "direct://").
  adaptor_->SetResolvedProxyServers(
      {"http://allowed-proxy.corp:3128", "http://", "direct://"});
  SendProxyResolutionRequest(pipes.stdout_write_fd.get(),
                             "https://example.com:443");
  worker::WorkerConfigs reply_config;
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &reply_config));
  ASSERT_EQ(2, reply_config.proxy_resolution_reply().proxy_servers_size());
  EXPECT_EQ("http://allowed-proxy.corp:3128",
            reply_config.proxy_resolution_reply().proxy_servers(0));
  EXPECT_EQ("direct://",
            reply_config.proxy_resolution_reply().proxy_servers(1));

  // 3. Forged / mismatched origins must all be rejected.
  const std::vector<std::string> invalid_origins = {
      "http://victim-proxy.corp:3128",
      "direct://",
      "http://",
      "",
      "http://allowed-proxy.corp:8080",
      "https://allowed-proxy.corp:3128",
      "HTTP://allowed-proxy.corp:3128",
      "http://allowed-proxy.corp:3128/",
      "http://evil.corp:3128@allowed-proxy.corp:3128",
      std::string("http://\xff\xfe:3128"),
  };
  for (const auto& origin : invalid_origins) {
    worker::ProtectionSpace ps;
    ps.set_origin(origin);
    ps.set_realm("CorpNet");
    ps.set_scheme("basic");
    SendAuthRequiredRequest(pipes.stdout_write_fd.get(), ps);
  }

  // 4. Request missing protection_space altogether must also be rejected.
  worker::WorkerRequest missing_ps_request;
  missing_ps_request.mutable_auth_required_request()
      ->set_bad_cached_credentials(false);
  ASSERT_TRUE(WriteProtobuf(pipes.stdout_write_fd.get(), missing_ps_request));
  brillo_loop_.RunOnce(/*may_block=*/false);

  // Confirm no credentials message was written back to the worker pipe.
  worker::WorkerConfigs unexpected_config;
  EXPECT_FALSE(ReadProtobuf(pipes.stdin_read_fd.get(), &unexpected_config));
}

// Verifies that allowed proxy origins are tracked per SandboxedWorker instance
// (not shared between system_services_worker_ and arc_worker_, nor retained
// across worker restarts).
TEST_F(SystemProxyAdaptorTest, AuthRequiredOriginAllowlistIsolatedPerWorker) {
  EXPECT_CALL(*mock_exported_object_, SendSignal(_)).Times(0);

  WorkerTestPipes system_pipes;
  WorkerTestPipes arc_pipes;
  StartWorkerForTest(/*user_traffic=*/false, &system_pipes);
  StartWorkerForTest(/*user_traffic=*/true, &arc_pipes);

  adaptor_->SetResolvedProxyServers(
      {"http://system-proxy.corp:3128", "direct://"});
  SendProxyResolutionRequest(system_pipes.stdout_write_fd.get(),
                             "https://system-update.example.com:443");
  worker::WorkerConfigs reply_config;
  ASSERT_TRUE(ReadProtobuf(system_pipes.stdin_read_fd.get(), &reply_config));

  // ARC worker never resolved "http://system-proxy.corp:3128", so its auth
  // request for that origin must be rejected.
  worker::ProtectionSpace ps;
  ps.set_origin("http://system-proxy.corp:3128");
  ps.set_realm("CorpNet");
  ps.set_scheme("basic");
  SendAuthRequiredRequest(arc_pipes.stdout_write_fd.get(), ps);

  // Restarting the system worker must also clear its previous allowlist.
  ASSERT_TRUE(ResetWorkerForTest(/*user_traffic=*/false));
  WorkerTestPipes restarted_system_pipes;
  StartWorkerForTest(/*user_traffic=*/false, &restarted_system_pipes);
  SendAuthRequiredRequest(restarted_system_pipes.stdout_write_fd.get(), ps);
}

// Verifies that the per-worker allowed proxy origins set enforces an LRU bound
// and refreshes recently used origins.
TEST_F(SystemProxyAdaptorTest, AuthRequiredOriginAllowlistLruEviction) {
  WorkerTestPipes pipes;
  SandboxedWorker* worker = StartWorkerForTest(/*user_traffic=*/false, &pipes);
  worker->SetMaxAllowedProxyOriginsForTesting(2);

  // Resolve proxy1 and proxy2 (allowlist now holds [proxy2, proxy1]).
  for (const char* proxy :
       {"http://proxy1.corp:3128", "http://proxy2.corp:3128"}) {
    adaptor_->SetResolvedProxyServers({proxy, "direct://"});
    SendProxyResolutionRequest(pipes.stdout_write_fd.get(),
                               "https://example.com:443");
    worker::WorkerConfigs reply;
    ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &reply));
  }

  // Touch proxy1 via AuthRequiredRequest so proxy1 becomes MRU and proxy2
  // becomes LRU.
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .WillOnce(Invoke(
          this, &SystemProxyAdaptorTest::HandleAuthenticationRequiredSignal));
  worker::ProtectionSpace ps1;
  ps1.set_origin("http://proxy1.corp:3128");
  ps1.set_realm("CorpNet");
  ps1.set_scheme("basic");
  SendAuthRequiredRequest(pipes.stdout_write_fd.get(), ps1);
  worker::WorkerConfigs cred_reply;
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &cred_reply));

  // Resolve proxy3 -> evicts LRU entry (proxy2), retaining [proxy3, proxy1].
  adaptor_->SetResolvedProxyServers({"http://proxy3.corp:3128", "direct://"});
  SendProxyResolutionRequest(pipes.stdout_write_fd.get(),
                             "https://example.com:443");
  worker::WorkerConfigs reply3;
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &reply3));

  // Evicted proxy2 must now be rejected.
  EXPECT_CALL(*mock_exported_object_, SendSignal(_)).Times(0);
  worker::ProtectionSpace ps2;
  ps2.set_origin("http://proxy2.corp:3128");
  ps2.set_realm("CorpNet");
  ps2.set_scheme("basic");
  SendAuthRequiredRequest(pipes.stdout_write_fd.get(), ps2);
  testing::Mock::VerifyAndClearExpectations(mock_exported_object_.get());

  // Both proxy1 and proxy3 must still be accepted.
  EXPECT_CALL(*mock_exported_object_, SendSignal(_))
      .Times(2)
      .WillRepeatedly(Invoke(
          this, &SystemProxyAdaptorTest::HandleAuthenticationRequiredSignal));
  worker::ProtectionSpace ps3;
  ps3.set_origin("http://proxy3.corp:3128");
  ps3.set_realm("CorpNet");
  ps3.set_scheme("basic");
  SendAuthRequiredRequest(pipes.stdout_write_fd.get(), ps1);
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &cred_reply));
  SendAuthRequiredRequest(pipes.stdout_write_fd.get(), ps3);
  ASSERT_TRUE(ReadProtobuf(pipes.stdin_read_fd.get(), &cred_reply));
}

}  // namespace system_proxy
