// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/openvpn_management_server.h"

#include <netinet/in.h>

#include <memory>
#include <string_view>
#include <utility>

#include <base/containers/span.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/service_constants.h>
#include <gtest/gtest.h>
#include <net-base/mock_process_manager.h>
#include <net-base/mock_socket.h>

#include "shill/manager.h"
#include "shill/mock_control.h"
#include "shill/mock_event_dispatcher.h"
#include "shill/mock_metrics.h"
#include "shill/store/key_value_store.h"
#include "shill/vpn/mock_openvpn_driver.h"

using testing::_;
using testing::Assign;
using testing::Eq;
using testing::InSequence;
using testing::Return;
using testing::StrEq;

namespace shill {
namespace {
MATCHER_P(StringEq, value, "") {
  return std::string(reinterpret_cast<const char*>(arg.data()), arg.size()) ==
         value;
}
}  // namespace

class OpenVPNManagementServerTest : public testing::Test {
 public:
  OpenVPNManagementServerTest()
      : manager_(&control_, &dispatcher_, &metrics_, "", "", ""),
        driver_(&manager_, &process_manager_),
        server_(&driver_) {
    auto socket_factory = std::make_unique<net_base::MockSocketFactory>();
    socket_factory_ = socket_factory.get();
    server_.socket_factory_ = std::move(socket_factory);
  }

  ~OpenVPNManagementServerTest() override = default;

 protected:
  void SetSocket(std::unique_ptr<net_base::MockSocket> socket) {
    server_.socket_ = std::move(socket);
  }

  void SetConnectedSocket(std::unique_ptr<net_base::MockSocket> socket) {
    server_.connected_socket_ = std::move(socket);
  }

  void ExpectSend(net_base::MockSocket& connected_socket,
                  std::string_view value) {
    EXPECT_CALL(connected_socket, Send(StringEq(value), _))
        .WillOnce(Return(value.size()));
  }

  void ExpectOTPStaticChallengeResponse(
      net_base::MockSocket& connected_socket) {
    driver_.args()->Set<std::string>(kOpenVPNUserProperty, "jojo");
    driver_.args()->Set<std::string>(kOpenVPNPasswordProperty, "yoyo");
    driver_.args()->Set<std::string>(kOpenVPNOTPProperty, "123456");
    ExpectSend(connected_socket, "username \"Auth\" \"jojo\"\n");
    ExpectSend(connected_socket,
               "password \"Auth\" \"SCRV1:eW95bw==:MTIzNDU2\"\n");
  }

  void ExpectTokenStaticChallengeResponse(
      net_base::MockSocket& connected_socket) {
    driver_.args()->Set<std::string>(kOpenVPNUserProperty, "jojo");
    driver_.args()->Set<std::string>(kOpenVPNTokenProperty, "toto");
    ExpectSend(connected_socket, "username \"Auth\" \"jojo\"\n");
    ExpectSend(connected_socket, "password \"Auth\" \"toto\"\n");
  }

  void ExpectAuthenticationResponse(net_base::MockSocket& connected_socket) {
    driver_.args()->Set<std::string>(kOpenVPNUserProperty, "jojo");
    driver_.args()->Set<std::string>(kOpenVPNPasswordProperty, "yoyo");
    ExpectSend(connected_socket, "username \"Auth\" \"jojo\"\n");
    ExpectSend(connected_socket, "password \"Auth\" \"yoyo\"\n");
  }

  void ExpectPinResponse(net_base::MockSocket& connected_socket) {
    driver_.args()->Set<std::string>(kOpenVPNPinProperty, "987654");
    ExpectSend(connected_socket,
               "password \"User-Specific TPM Token FOO\" \"987654\"\n");
  }

  void ExpectHoldRelease(net_base::MockSocket& connected_socket) {
    ExpectSend(connected_socket, "hold release\n");
  }

  void ExpectRestart(net_base::MockSocket& connected_socket) {
    ExpectSend(connected_socket, "signal SIGUSR1\n");
  }

  base::span<const uint8_t> CreateSpanFromString(std::string_view str) {
    return {reinterpret_cast<const uint8_t*>(str.data()), str.size()};
  }

  void SendSignal(std::string_view signal) { server_.SendSignal(signal); }

  void OnInput(base::span<const uint8_t> data) { server_.OnInput(data); }

  void ProcessMessage(std::string_view message) {
    server_.ProcessMessage(message);
  }

  bool ProcessSuccessMessage(std::string_view message) {
    return server_.ProcessSuccessMessage(message);
  }

  bool ProcessStateMessage(std::string_view message) {
    return server_.ProcessStateMessage(message);
  }

  bool ProcessAuthTokenMessage(std::string_view message) {
    return server_.ProcessAuthTokenMessage(message);
  }

  bool GetHoldWaiting() { return server_.hold_waiting_; }

  static std::string_view ParseSubstring(std::string_view message,
                                         std::string_view start,
                                         std::string_view end) {
    return OpenVPNManagementServer::ParseSubstring(message, start, end);
  }

  static std::string_view ParsePasswordTag(std::string_view message) {
    return OpenVPNManagementServer::ParsePasswordTag(message);
  }

  static std::string_view ParsePasswordFailedReason(std::string_view message) {
    return OpenVPNManagementServer::ParsePasswordFailedReason(message);
  }

  void SetClientState(std::string_view state) {
    server_.state_ = std::string(state);
  }

  // required by base::FileDescriptorWatcher.
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::IO};

  MockControl control_;
  MockEventDispatcher dispatcher_;
  MockMetrics metrics_;
  net_base::MockProcessManager process_manager_;
  Manager manager_;
  MockOpenVPNDriver driver_;
  OpenVPNManagementServer server_;  // Destroy before anything it references.
  net_base::MockSocketFactory* socket_factory_;  // Owned by |server_|.
};

TEST_F(OpenVPNManagementServerTest, StartStarted) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  EXPECT_TRUE(server_.Start(nullptr));
}

TEST_F(OpenVPNManagementServerTest, StartSocketFail) {
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP))
      .WillOnce(Return(nullptr));

  EXPECT_FALSE(server_.Start(nullptr));
  EXPECT_FALSE(server_.IsStarted());
}

TEST_F(OpenVPNManagementServerTest, StartGetSockNameFail) {
  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Bind).WillOnce(Return(true));
        EXPECT_CALL(*socket, Listen(1)).WillOnce(Return(true));
        EXPECT_CALL(*socket, GetSockName).WillOnce(Return(false));
        return socket;
      });

  EXPECT_FALSE(server_.Start(nullptr));
  EXPECT_FALSE(server_.IsStarted());
}

TEST_F(OpenVPNManagementServerTest, Start) {
  const std::string kStaticChallenge = "static-challenge";
  driver_.args()->Set<std::string>(kOpenVPNStaticChallengeProperty,
                                   kStaticChallenge);

  EXPECT_CALL(*socket_factory_,
              Create(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP))
      .WillOnce([]() {
        auto socket = std::make_unique<net_base::MockSocket>();
        EXPECT_CALL(*socket, Bind).WillOnce(Return(true));
        EXPECT_CALL(*socket, Listen(1)).WillOnce(Return(true));
        EXPECT_CALL(*socket, GetSockName).WillOnce(Return(true));
        return socket;
      });

  std::vector<std::vector<std::string>> options;
  EXPECT_TRUE(server_.Start(&options));
  std::vector<std::vector<std::string>> expected_options{
      {"management", "127.0.0.1", "0"},
      {"management-client"},
      {"management-hold"},
      {"management-query-passwords"},
      {"static-challenge", kStaticChallenge, "1"}};
  EXPECT_EQ(expected_options, options);
}

TEST_F(OpenVPNManagementServerTest, Stop) {
  EXPECT_TRUE(server_.state().empty());

  SetSocket(std::make_unique<net_base::MockSocket>());
  SetConnectedSocket(std::make_unique<net_base::MockSocket>());

  SetClientState(OpenVPNManagementServer::kStateReconnecting);
  server_.Stop();
  EXPECT_EQ(nullptr, server_.connected_socket_);
  EXPECT_EQ(nullptr, server_.socket_);
  EXPECT_TRUE(server_.state().empty());
  EXPECT_FALSE(server_.IsStarted());
}

TEST_F(OpenVPNManagementServerTest, OnReadyAcceptFail) {
  auto socket = std::make_unique<net_base::MockSocket>();
  EXPECT_CALL(*socket, Accept(nullptr, nullptr)).WillOnce(Return(nullptr));
  SetSocket(std::move(socket));

  server_.OnAcceptReady();
  EXPECT_EQ(nullptr, server_.connected_socket_);
}

TEST_F(OpenVPNManagementServerTest, OnSocketConnected) {
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  int connected_socket_fd = connected_socket->Get();
  ExpectSend(*connected_socket, "state on\n");

  auto socket = std::make_unique<net_base::MockSocket>();
  EXPECT_CALL(*socket, Accept(nullptr, nullptr))
      .WillOnce(Return(std::move(connected_socket)));
  SetSocket(std::move(socket));

  server_.OnAcceptReady();
  EXPECT_EQ(connected_socket_fd, server_.connected_socket_->Get());
}

TEST_F(OpenVPNManagementServerTest, OnInput) {
  {
    const char data[] = "";
    OnInput(CreateSpanFromString(data));
  }
  {
    const char data[] =
        "foo\n"
        ">INFO:...\n"
        ">PASSWORD:Need 'Auth' SC:user/password/otp\n"
        ">PASSWORD:Need 'User-Specific TPM Token FOO' ...\n"
        ">PASSWORD:Verification Failed: .\n"
        ">PASSWORD:Auth-Token:ToKeN==\n"
        ">STATE:123,RECONNECTING,detail,...,...\n"
        ">HOLD:Waiting for hold release\n"
        "SUCCESS: Hold released.";

    SetSocket(std::make_unique<net_base::MockSocket>());
    auto connected_socket = std::make_unique<net_base::MockSocket>();
    ExpectOTPStaticChallengeResponse(*connected_socket);
    ExpectPinResponse(*connected_socket);
    SetConnectedSocket(std::move(connected_socket));

    EXPECT_CALL(driver_, FailService(Service::kFailureConnect,
                                     StrEq(Service::kErrorDetailsNone)));
    EXPECT_CALL(driver_, OnReconnecting(_));
    EXPECT_FALSE(GetHoldWaiting());
    OnInput(CreateSpanFromString(data));
    EXPECT_TRUE(GetHoldWaiting());
  }
}

TEST_F(OpenVPNManagementServerTest, OnInputStop) {
  const char data[] =
      ">PASSWORD:Verification Failed: .\n"
      ">STATE:123,RECONNECTING,detail,...,...";

  SetSocket(std::make_unique<net_base::MockSocket>());

  // Stops the server after the first message is processed.
  EXPECT_CALL(driver_, FailService(Service::kFailureConnect,
                                   StrEq(Service::kErrorDetailsNone)))
      .WillOnce(Assign(&server_.socket_, nullptr));
  // The second message should not be processed.
  EXPECT_CALL(driver_, OnReconnecting(_)).Times(0);
  OnInput(CreateSpanFromString(data));
}

TEST_F(OpenVPNManagementServerTest, OnInputStatus) {
  const char data[] =
      "OpenVPN STATISTICS\n"
      "Updated,Wed Nov  3 14:11:13 2021\n"
      "TUN/TAP read bytes,0\n"
      "TUN/TAP write bytes,0\n"
      "TCP/UDP read bytes,3495\n"
      "TCP/UDP write bytes,3354\n"
      "Auth read bytes,0\n"
      "Data channel cipher,AES-256-GCM\n"
      "END";
  SetSocket(std::make_unique<net_base::MockSocket>());
  EXPECT_CALL(driver_, ReportCipherMetrics("AES-256-GCM"));
  OnInput(CreateSpanFromString(data));
}

TEST_F(OpenVPNManagementServerTest, ProcessMessage) {
  ProcessMessage("foo");
  ProcessMessage(">INFO:");

  EXPECT_CALL(driver_, OnReconnecting(_));
  ProcessMessage(">STATE:123,RECONNECTING,detail,...,...");
}

TEST_F(OpenVPNManagementServerTest, ProcessSuccessMessage) {
  EXPECT_FALSE(ProcessSuccessMessage("foo"));
  EXPECT_TRUE(ProcessSuccessMessage("SUCCESS: foo"));
}

TEST_F(OpenVPNManagementServerTest, ProcessInfoMessage) {
  EXPECT_FALSE(server_.ProcessInfoMessage("foo"));
  EXPECT_TRUE(server_.ProcessInfoMessage(">INFO:foo"));
}

TEST_F(OpenVPNManagementServerTest, ProcessStateMessage) {
  EXPECT_TRUE(server_.state().empty());
  EXPECT_FALSE(ProcessStateMessage("foo"));
  EXPECT_TRUE(server_.state().empty());
  EXPECT_TRUE(ProcessStateMessage(">STATE:123,WAIT,detail,...,..."));
  EXPECT_EQ("WAIT", server_.state());
  {
    InSequence seq;
    EXPECT_CALL(driver_,
                OnReconnecting(OpenVPNDriver::kReconnectReasonUnknown));
    EXPECT_CALL(driver_,
                OnReconnecting(OpenVPNDriver::kReconnectReasonTLSError));
  }
  EXPECT_TRUE(ProcessStateMessage(">STATE:123,RECONNECTING,detail,...,..."));
  EXPECT_EQ(OpenVPNManagementServer::kStateReconnecting, server_.state());
  EXPECT_TRUE(ProcessStateMessage(">STATE:123,RECONNECTING,tls-error,...,..."));
}

TEST_F(OpenVPNManagementServerTest, ProcessStateMessageConnected) {
  EXPECT_TRUE(server_.state().empty());

  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, "status\n");
  SetConnectedSocket(std::move(connected_socket));

  EXPECT_TRUE(ProcessStateMessage(">STATE:123,CONNECTED,SUCCESS,...,..."));
}

TEST_F(OpenVPNManagementServerTest, ProcessNeedPasswordMessageAuthSC) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectOTPStaticChallengeResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  EXPECT_TRUE(server_.ProcessNeedPasswordMessage(
      ">PASSWORD:Need 'Auth' SC:user/password/otp"));
  EXPECT_FALSE(driver_.args()->Contains<std::string>(kOpenVPNOTPProperty));
}

TEST_F(OpenVPNManagementServerTest, ProcessNeedPasswordMessageAuth) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectAuthenticationResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  EXPECT_TRUE(server_.ProcessNeedPasswordMessage(
      ">PASSWORD:Need 'Auth' username/password"));
}

TEST_F(OpenVPNManagementServerTest, ProcessNeedPasswordMessageTPMToken) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectPinResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  EXPECT_TRUE(server_.ProcessNeedPasswordMessage(
      ">PASSWORD:Need 'User-Specific TPM Token FOO' ..."));
}

TEST_F(OpenVPNManagementServerTest, ProcessNeedPasswordMessageUnknown) {
  EXPECT_FALSE(server_.ProcessNeedPasswordMessage("foo"));
}

TEST_F(OpenVPNManagementServerTest, ParseSubstring) {
  EXPECT_EQ("", ParseSubstring("", "'", "'"));
  EXPECT_EQ("", ParseSubstring(" ", "'", "'"));
  EXPECT_EQ("", ParseSubstring("'", "'", "'"));
  EXPECT_EQ("", ParseSubstring("''", "'", "'"));
  EXPECT_EQ("", ParseSubstring("] [", "[", "]"));
  EXPECT_EQ("", ParseSubstring("[]", "[", "]"));
  EXPECT_EQ("bar", ParseSubstring("foo['bar']zoo", "['", "']"));
  EXPECT_EQ("bar", ParseSubstring("foo['bar']", "['", "']"));
  EXPECT_EQ("bar", ParseSubstring("['bar']zoo", "['", "']"));
  EXPECT_EQ("bar", ParseSubstring("['bar']['zoo']", "['", "']"));
}

TEST_F(OpenVPNManagementServerTest, ParsePasswordTag) {
  EXPECT_EQ("", ParsePasswordTag(""));
  EXPECT_EQ("Auth", ParsePasswordTag(
                        ">PASSWORD:Verification Failed: 'Auth' "
                        "['REVOKED: client certificate has been revoked']"));
}

TEST_F(OpenVPNManagementServerTest, ParsePasswordFailedReason) {
  EXPECT_EQ("", ParsePasswordFailedReason(""));
  EXPECT_EQ("REVOKED: client certificate has been revoked",
            ParsePasswordFailedReason(
                ">PASSWORD:Verification Failed: 'Auth' "
                "['REVOKED: client certificate has been revoked']"));
}

TEST_F(OpenVPNManagementServerTest, PerformStaticChallengeNoCreds) {
  EXPECT_CALL(driver_, FailService(Service::kFailureInternal,
                                   StrEq(Service::kErrorDetailsNone)))
      .Times(4);
  server_.PerformStaticChallenge("Auth");
  driver_.args()->Set<std::string>(kOpenVPNUserProperty, "jojo");
  server_.PerformStaticChallenge("Auth");
  driver_.args()->Set<std::string>(kOpenVPNPasswordProperty, "yoyo");
  server_.PerformStaticChallenge("Auth");
  driver_.args()->Clear();
  driver_.args()->Set<std::string>(kOpenVPNTokenProperty, "toto");
  server_.PerformStaticChallenge("Auth");
}

TEST_F(OpenVPNManagementServerTest, PerformStaticChallengeOTP) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectOTPStaticChallengeResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.PerformStaticChallenge("Auth");
  EXPECT_FALSE(driver_.args()->Contains<std::string>(kOpenVPNOTPProperty));
}

TEST_F(OpenVPNManagementServerTest, PerformStaticChallengeToken) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectTokenStaticChallengeResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.PerformStaticChallenge("Auth");
  EXPECT_FALSE(driver_.args()->Contains<std::string>(kOpenVPNTokenProperty));
}

TEST_F(OpenVPNManagementServerTest, PerformAuthenticationNoCreds) {
  EXPECT_CALL(driver_, FailService(Service::kFailureInternal,
                                   StrEq(Service::kErrorDetailsNone)))
      .Times(2);
  server_.PerformAuthentication("Auth");
  driver_.args()->Set<std::string>(kOpenVPNUserProperty, "jojo");
  server_.PerformAuthentication("Auth");
}

TEST_F(OpenVPNManagementServerTest, PerformAuthentication) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectAuthenticationResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.PerformAuthentication("Auth");
}

TEST_F(OpenVPNManagementServerTest, ProcessHoldMessage) {
  EXPECT_FALSE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);

  EXPECT_FALSE(server_.ProcessHoldMessage("foo"));

  EXPECT_TRUE(server_.ProcessHoldMessage(">HOLD:Waiting for hold release"));
  EXPECT_FALSE(server_.hold_release_);
  EXPECT_TRUE(server_.hold_waiting_);

  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectHoldRelease(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.hold_release_ = true;
  server_.hold_waiting_ = false;
  EXPECT_TRUE(server_.ProcessHoldMessage(">HOLD:Waiting for hold release"));
  EXPECT_TRUE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);
}

TEST_F(OpenVPNManagementServerTest, SupplyTPMTokenNoPin) {
  EXPECT_CALL(driver_, FailService(Service::kFailureInternal,
                                   StrEq(Service::kErrorDetailsNone)));
  server_.SupplyTPMToken("User-Specific TPM Token FOO");
}

TEST_F(OpenVPNManagementServerTest, SupplyTPMToken) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectPinResponse(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.SupplyTPMToken("User-Specific TPM Token FOO");
}

TEST_F(OpenVPNManagementServerTest, Send) {
  constexpr char kMessage[] = "foo\n";

  // Even |connected_socket_| is not set, Send() should not crash.
  SetSocket(std::make_unique<net_base::MockSocket>());
  server_.Send(kMessage);

  // After |connected_socket_| is set, Send() method should send the message
  // through the socket.
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, kMessage);
  SetConnectedSocket(std::move(connected_socket));

  server_.Send(kMessage);
}

TEST_F(OpenVPNManagementServerTest, SendState) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, "state off\n");
  SetConnectedSocket(std::move(connected_socket));

  server_.SendState("off");
}

TEST_F(OpenVPNManagementServerTest, SendUsername) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, "username \"Auth\" \"joesmith\"\n");
  SetConnectedSocket(std::move(connected_socket));

  server_.SendUsername("Auth", "joesmith");
}

TEST_F(OpenVPNManagementServerTest, SendUsernameWithSpecialCharacters) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket,
             "username \"\\\\ and \\\"\" \"joesmith with \\\" and \\\\\"\n");
  SetConnectedSocket(std::move(connected_socket));

  // Verify that \ and " are escaped as \\ and \" in tag and username.
  server_.SendUsername("\\ and \"", "joesmith with \" and \\");
}

TEST_F(OpenVPNManagementServerTest, SendPassword) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, "password \"Auth\" \"foobar\"\n");
  SetConnectedSocket(std::move(connected_socket));

  server_.SendPassword("Auth", "foobar");
}

TEST_F(OpenVPNManagementServerTest, SendPasswordWithSpecialCharacters) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket,
             "password \"\\\\ and \\\"\" \"foobar with \\\" and \\\\\"\n");
  SetConnectedSocket(std::move(connected_socket));

  // Verify that \ and " are escaped as \\ and \" in tag and password.
  server_.SendPassword("\\ and \"", "foobar with \" and \\");
}

TEST_F(OpenVPNManagementServerTest, ProcessFailedPasswordMessage) {
  EXPECT_FALSE(server_.ProcessFailedPasswordMessage("foo"));
  EXPECT_CALL(driver_, FailService(Service::kFailureConnect,
                                   StrEq(Service::kErrorDetailsNone)))
      .Times(3);
  EXPECT_CALL(driver_, FailService(Service::kFailureConnect, Eq("Revoked.")));
  EXPECT_TRUE(
      server_.ProcessFailedPasswordMessage(">PASSWORD:Verification Failed: ."));
  EXPECT_TRUE(server_.ProcessFailedPasswordMessage(
      ">PASSWORD:Verification Failed: 'Private Key' ['Reason']"));
  EXPECT_TRUE(server_.ProcessFailedPasswordMessage(
      ">PASSWORD:Verification Failed: 'Auth'"));
  EXPECT_TRUE(server_.ProcessFailedPasswordMessage(
      ">PASSWORD:Verification Failed: 'Auth' ['Revoked.']"));
}

TEST_F(OpenVPNManagementServerTest, ProcessAuthTokenMessage) {
  EXPECT_FALSE(ProcessAuthTokenMessage("foo"));
  EXPECT_TRUE(ProcessAuthTokenMessage(">PASSWORD:Auth-Token:ToKeN=="));
}

TEST_F(OpenVPNManagementServerTest, SendSignal) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectSend(*connected_socket, "signal SIGUSR2\n");
  SetConnectedSocket(std::move(connected_socket));

  SendSignal("SIGUSR2");
}

TEST_F(OpenVPNManagementServerTest, Restart) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectRestart(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.Restart();
}

TEST_F(OpenVPNManagementServerTest, SendHoldRelease) {
  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectHoldRelease(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.SendHoldRelease();
}

TEST_F(OpenVPNManagementServerTest, Hold) {
  EXPECT_FALSE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);

  server_.ReleaseHold();
  EXPECT_TRUE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);

  server_.Hold();
  EXPECT_FALSE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);

  server_.hold_waiting_ = true;

  SetSocket(std::make_unique<net_base::MockSocket>());
  auto connected_socket = std::make_unique<net_base::MockSocket>();
  ExpectHoldRelease(*connected_socket);
  SetConnectedSocket(std::move(connected_socket));

  server_.ReleaseHold();
  EXPECT_TRUE(server_.hold_release_);
  EXPECT_FALSE(server_.hold_waiting_);
}

TEST_F(OpenVPNManagementServerTest, EscapeToQuote) {
  EXPECT_EQ("", OpenVPNManagementServer::EscapeToQuote(""));
  EXPECT_EQ("foo './", OpenVPNManagementServer::EscapeToQuote("foo './"));
  EXPECT_EQ("\\\\", OpenVPNManagementServer::EscapeToQuote("\\"));
  EXPECT_EQ("\\\"", OpenVPNManagementServer::EscapeToQuote("\""));
  EXPECT_EQ("\\\\\\\"foo\\\\bar\\\"",
            OpenVPNManagementServer::EscapeToQuote("\\\"foo\\bar\""));
}

}  // namespace shill
