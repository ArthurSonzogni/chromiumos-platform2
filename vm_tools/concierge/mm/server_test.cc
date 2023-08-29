// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>

#include <utility>

#include <gtest/gtest.h>

#include <base/test/task_environment.h>

#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/mm/fake_vm_socket.h"
#include "vm_tools/concierge/mm/kills_server.h"
#include "vm_tools/concierge/mm/mglru_test_util.h"
#include "vm_tools/concierge/mm/reclaim_server.h"
#include "vm_tools/concierge/mm/server.h"

namespace vm_tools::concierge::mm {
namespace {

// A bare implementation of the Server to allow for testing
// the base class.
class ServerImpl : public Server {
 public:
  ServerImpl(int port, SocketFactory socket_factory)
      : Server(port, socket_factory) {}

 protected:
  void HandlePacket(const Connection& connection,
                    const VmMemoryManagementPacket& packet) override {
    return;
  }
};

class ServerTest : public ::testing::Test {
 public:
  void SetUp() override {
    server_ = std::make_unique<ServerImpl>(
        port_, base::BindRepeating(&ServerTest::FakeVmSocketFactory,
                                   base::Unretained(this)));

    server_->SetClientDisconnectedNotification(base::BindRepeating(
        &ServerTest::OnClientDisconnected, base::Unretained(this)));
    server_->SetClientConnectionNotification(base::BindRepeating(
        &ServerTest::OnClientConnected, base::Unretained(this)));
  }

 protected:
  std::unique_ptr<VmSocket> FakeVmSocketFactory(base::ScopedFD fd) {
    // These fds must be kept alive until the end of the test so unique values
    // are used for each new socket fd.
    socket_fds_.emplace_back(std::move(fd));

    if (staged_fake_socket_) {
      return std::move(staged_fake_socket_);
    }

    // Error if no socket was ready.
    EXPECT_TRUE(false);
    return {};
  }

  void AssertListeningSucceeds(Server& server) {
    staged_fake_socket_ = std::make_unique<FakeVmSocket>();
    leaked_server_socket_ = staged_fake_socket_.get();

    ASSERT_TRUE(server.StartListening());

    ASSERT_EQ(leaked_server_socket_->listen_call_count_, 1);
    ASSERT_EQ(leaked_server_socket_->listen_port_, port_);
    ASSERT_EQ(leaked_server_socket_->listen_backlog_size_, 1);
    ASSERT_EQ(leaked_server_socket_->on_readable_call_count_, 1);
  }

  void AcceptNewClient(Server& server, int cid) {
    size_t initial_instance_count = FakeVmSocket::instance_count_;

    // Register the VM so that the connection isn't rejected.
    server.RegisterVm(cid);
    leaked_server_socket_->connected_cid_ = cid;

    // The "fd" for the new connection. Even though this won't be written to or
    // read from, it needs to be an actual FD so that when the scoped fd goes
    // out of scope it can close the fd properly.
    leaked_server_socket_->accept_fd_ =
        base::ScopedFD(open("/dev/null", O_RDONLY));

    staged_fake_socket_ = std::make_unique<FakeVmSocket>();
    leaked_client_sockets_.emplace_back(staged_fake_socket_.get());

    leaked_server_socket_->on_readable_.Run();
    ASSERT_FALSE(staged_fake_socket_);

    ASSERT_EQ(FakeVmSocket::instance_count_, initial_instance_count + 1);
  }

  void ConnectNewClient(Server& server, int cid, ConnectionType type) {
    AcceptNewClient(server, cid);

    FakeVmSocket* client_socket = leaked_client_sockets_.back();

    VmMemoryManagementPacket handshake_packet;
    handshake_packet.set_type(PacketType::PACKET_TYPE_HANDSHAKE);
    handshake_packet.mutable_handshake()->set_type(type);
    client_socket->packet_to_read_ = handshake_packet;

    client_socket->on_readable_.Run();

    ASSERT_EQ(disconnected_count_, 0);
    ASSERT_EQ(FakeVmSocket::instance_count_, 2u);

    ASSERT_EQ(connected_count_, 1);
  }

  void OnClientConnected(Client) { connected_count_++; }

  void OnClientDisconnected(Client) { disconnected_count_++; }

  int port_ = 230;

  std::unique_ptr<Server> server_{};
  std::unique_ptr<FakeVmSocket> staged_fake_socket_{};
  std::vector<FakeVmSocket*> leaked_client_sockets_{};
  FakeVmSocket* leaked_server_socket_{};
  std::vector<base::ScopedFD> socket_fds_{};

  int connected_count_ = 0;
  int disconnected_count_ = 0;

  base::test::TaskEnvironment task_environment_;
};

TEST_F(ServerTest, TestListenFailureFails) {
  staged_fake_socket_ = std::make_unique<FakeVmSocket>();
  FakeVmSocket* leaked_fake_socket = staged_fake_socket_.get();

  leaked_fake_socket->listen_result_ = false;

  ASSERT_FALSE(server_->StartListening());
}

TEST_F(ServerTest, TestOnReadableFailureFails) {
  staged_fake_socket_ = std::make_unique<FakeVmSocket>();
  FakeVmSocket* leaked_fake_socket = staged_fake_socket_.get();

  leaked_fake_socket->on_readable_result_ = false;

  ASSERT_FALSE(server_->StartListening());
}

TEST_F(ServerTest, TestSecondStartListeningFails) {
  staged_fake_socket_ = std::make_unique<FakeVmSocket>();

  ASSERT_TRUE(server_->StartListening());

  ASSERT_FALSE(server_->StartListening());
}

TEST_F(ServerTest, TestListeningSucceeds) {
  AssertListeningSucceeds(*server_);
}

TEST_F(ServerTest, TestAcceptInvalidSocketResult) {
  AssertListeningSucceeds(*server_);

  staged_fake_socket_ = std::make_unique<FakeVmSocket>();
  staged_fake_socket_->is_valid_ = false;

  leaked_server_socket_->on_readable_.Run();
  ASSERT_FALSE(staged_fake_socket_);
  // Since the connection was rejected, only one socket should remain (the
  // server socket).
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestAcceptRejectsUnregisteredCid) {
  AssertListeningSucceeds(*server_);

  staged_fake_socket_ = std::make_unique<FakeVmSocket>();

  leaked_server_socket_->on_readable_.Run();
  ASSERT_FALSE(staged_fake_socket_);
  // Since the connection was rejected, only one socket should remain (the
  // server socket).
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestRegisterUnregisterCidRejects) {
  AssertListeningSucceeds(*server_);

  server_->RegisterVm(10);
  server_->RemoveVm(10);

  leaked_server_socket_->connected_cid_ = 10;

  staged_fake_socket_ = std::make_unique<FakeVmSocket>();

  leaked_server_socket_->on_readable_.Run();
  ASSERT_FALSE(staged_fake_socket_);
  // Since the connection was rejected, only one socket should remain (the
  // server socket).
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestOnReadableFailureFailsAccept) {
  AssertListeningSucceeds(*server_);

  int cid = 10;

  // Register a VM so that the connection isn't rejected.
  server_->RegisterVm(cid);
  leaked_server_socket_->connected_cid_ = cid;

  staged_fake_socket_ = std::make_unique<FakeVmSocket>();
  staged_fake_socket_->on_readable_result_ = false;

  leaked_server_socket_->on_readable_.Run();
  ASSERT_FALSE(staged_fake_socket_);
  // Since the connection was rejected, only one socket should remain (the
  // server socket).
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestAcceptSuccess) {
  AssertListeningSucceeds(*server_);
  AcceptNewClient(*server_, 10);
}

TEST_F(ServerTest, TestMaxConnectionsRejects) {
  AssertListeningSucceeds(*server_);

  // Max connections is 8.
  for (int i = 0; i < 8; i++) {
    AcceptNewClient(*server_, 10);
  }

  // Max connection should be reached. Further connections should be rejected.
  staged_fake_socket_ = std::make_unique<FakeVmSocket>();

  leaked_server_socket_->on_readable_.Run();

  // The staged socket should not have been used.
  ASSERT_TRUE(staged_fake_socket_);
}

TEST_F(ServerTest, TestRemoveUnregisteredVm) {
  AssertListeningSucceeds(*server_);
  server_->RemoveVm(10);
}

TEST_F(ServerTest, TestRemoveVmRemovesSockets) {
  AssertListeningSucceeds(*server_);

  // Accept connections from two different VMs
  AcceptNewClient(*server_, 10);
  AcceptNewClient(*server_, 11);

  // There should be 3 sockets now.
  ASSERT_EQ(FakeVmSocket::instance_count_, 3u);

  // Removing one VM should only remove one socket.
  server_->RemoveVm(10);
  ASSERT_EQ(FakeVmSocket::instance_count_, 2u);

  server_->RemoveVm(11);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestRemoveVmRunsDisconnectedCallback) {
  AssertListeningSucceeds(*server_);

  AcceptNewClient(*server_, 10);
  server_->RemoveVm(10);
  ASSERT_EQ(disconnected_count_, 1);
}

TEST_F(ServerTest, TestReadPacketFailureRemovesClient) {
  AssertListeningSucceeds(*server_);

  AcceptNewClient(*server_, 10);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  client_socket->read_result_ = false;

  client_socket->on_readable_.Run();

  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestHandleHandshakeInvalidPacketWriteFailureRemovesClient) {
  AssertListeningSucceeds(*server_);

  AcceptNewClient(*server_, 10);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  // Set the packet type, but don't set the handshake field.
  VmMemoryManagementPacket handshake_packet;
  handshake_packet.set_type(PacketType::PACKET_TYPE_HANDSHAKE);
  client_socket->packet_to_read_ = handshake_packet;

  client_socket->write_result_ = false;

  client_socket->on_readable_.Run();

  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestHandleHandshakeValidPacketWriteFailureRemovesClient) {
  AssertListeningSucceeds(*server_);

  AcceptNewClient(*server_, 10);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket handshake_packet;
  handshake_packet.set_type(PacketType::PACKET_TYPE_HANDSHAKE);
  handshake_packet.mutable_handshake()->set_type(
      ConnectionType::CONNECTION_TYPE_KILLS);
  client_socket->packet_to_read_ = handshake_packet;

  client_socket->write_result_ = false;

  client_socket->on_readable_.Run();

  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ServerTest, TestValidHandshakeSuccess) {
  AssertListeningSucceeds(*server_);
  ConnectNewClient(*server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);
}

class ReclaimServerTest : public ServerTest {
 public:
  void SetUp() override {
    reclaim_server_ = std::make_unique<ReclaimServer>(
        port_, base::BindRepeating(&ReclaimServerTest::FakeVmSocketFactory,
                                   base::Unretained(this)));

    reclaim_server_->SetClientDisconnectedNotification(base::BindRepeating(
        &ReclaimServerTest::OnClientDisconnected, base::Unretained(this)));
    reclaim_server_->SetClientConnectionNotification(base::BindRepeating(
        &ReclaimServerTest::OnClientConnected, base::Unretained(this)));
    reclaim_server_->SetNewGenerationNotification(base::BindRepeating(
        &ReclaimServerTest::OnNewMglruGeneration, base::Unretained(this)));
  }

 protected:
  void OnNewMglruGeneration(int cid, MglruStats stats) {
    mglru_cid_ = cid;
    stats_ = stats;
  }

  std::unique_ptr<ReclaimServer> reclaim_server_{};

  int mglru_cid_ = 0;
  MglruStats stats_{};
};

TEST_F(ReclaimServerTest, TestGetMglruStatsNoConnectionFails) {
  AssertListeningSucceeds(*reclaim_server_);

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));
}

TEST_F(ReclaimServerTest, TestGetMglruStatsSendPacketFailureFails) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  client_socket->write_result_ = false;

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));

  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ReclaimServerTest, TestGetMglruStatsReadPacketFailureFails) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  client_socket->read_result_ = false;

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));
  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(ReclaimServerTest, TestGetMglruStatsSendsCorrectRequest) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));

  ASSERT_EQ(client_socket->written_packet_.type(),
            PacketType::PACKET_TYPE_MGLRU_REQUEST);
}

TEST_F(ReclaimServerTest, TestGetMglruStatsInvalidPacketFails) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  // The socket will by default return an invalid packet.

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));
  // An invalid response should not cause the client to be removed.
  ASSERT_EQ(disconnected_count_, 0);
}

TEST_F(ReclaimServerTest, TestGetMglruStatsInvalidPacketFieldFails) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket invalid_packet;
  invalid_packet.set_type(PacketType::PACKET_TYPE_MGLRU_RESPONSE);

  client_socket->packet_to_read_ = invalid_packet;

  // No MGLRU response field.

  ASSERT_FALSE(reclaim_server_->GetMglruStats(10));
  // An invalid response should not cause the client to be removed.
  ASSERT_EQ(disconnected_count_, 0);
}

TEST_F(ReclaimServerTest, TestGetMglruStatsSuccess) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket response_packet;
  response_packet.set_type(PacketType::PACKET_TYPE_MGLRU_RESPONSE);
  mglru::AddMemcg(response_packet.mutable_mglru_response()->mutable_stats(), 1);
  client_socket->packet_to_read_ = response_packet;

  auto stats = reclaim_server_->GetMglruStats(10);

  ASSERT_TRUE(stats);
  ASSERT_EQ(stats->cgs_size(), 1);

  ASSERT_EQ(disconnected_count_, 0);
  ASSERT_EQ(FakeVmSocket::instance_count_, 2u);
}

TEST_F(ReclaimServerTest, TestHandleMglruPacketInvalidPacketDoesNothing) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket response_packet;
  response_packet.set_type(PacketType::PACKET_TYPE_MGLRU_RESPONSE);

  // Set the type to MGLRU response but don't set the MglruStats field.

  client_socket->packet_to_read_ = response_packet;

  client_socket->on_readable_.Run();

  // The MGLRU notification should not have been called.
  ASSERT_EQ(mglru_cid_, 0);

  // The invalid packet should not remove the client.
  ASSERT_EQ(disconnected_count_, 0);
}

TEST_F(ReclaimServerTest, TestHandleMglruPacketSuccess) {
  AssertListeningSucceeds(*reclaim_server_);
  ConnectNewClient(*reclaim_server_, 10, ConnectionType::CONNECTION_TYPE_STATS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket response_packet;
  response_packet.set_type(PacketType::PACKET_TYPE_MGLRU_RESPONSE);
  mglru::AddMemcg(response_packet.mutable_mglru_response()->mutable_stats(), 1);
  client_socket->packet_to_read_ = response_packet;

  client_socket->on_readable_.Run();

  // The MGLRU notification should not have been called.
  ASSERT_EQ(mglru_cid_, 10);
  ASSERT_EQ(stats_.cgs_size(), 1);
}

class KillsServerTest : public ServerTest {
 public:
  void SetUp() override {
    kills_server_ = std::make_unique<KillsServer>(
        port_, base::BindRepeating(&KillsServerTest::FakeVmSocketFactory,
                                   base::Unretained(this)));

    kills_server_->SetClientDisconnectedNotification(base::BindRepeating(
        &KillsServerTest::OnClientDisconnected, base::Unretained(this)));
    kills_server_->SetClientConnectionNotification(base::BindRepeating(
        &KillsServerTest::OnClientConnected, base::Unretained(this)));
    kills_server_->SetKillRequestHandler(base::BindRepeating(
        &KillsServerTest::HandleKillRequest, base::Unretained(this)));
    kills_server_->SetNoKillCandidateNotification(base::BindRepeating(
        &KillsServerTest::OnNoKillCandidates, base::Unretained(this)));
    kills_server_->SetDecisionLatencyNotification(base::BindRepeating(
        &KillsServerTest::OnDecisionLatency, base::Unretained(this)));
  }

 protected:
  size_t HandleKillRequest(Client client,
                           size_t size,
                           ResizePriority priority) {
    handle_kill_request_count_++;
    kill_request_client_ = client;
    kill_request_size_ = size;
    kill_request_priority_ = priority;
    return kill_request_result_;
  }

  void OnNoKillCandidates(Client client) {
    no_kill_candidates_client_ = client;
  }

  void OnDecisionLatency(Client client, const DecisionLatency& latency) {
    decision_latency_count_++;
    decision_latency_client_ = client;
    decision_latency_ = latency;
  }

  std::unique_ptr<KillsServer> kills_server_{};

  int handle_kill_request_count_ = 0;
  Client kill_request_client_{};
  size_t kill_request_size_{};
  ResizePriority kill_request_priority_{};
  size_t kill_request_result_ = 0;

  Client no_kill_candidates_client_{};

  Client decision_latency_client_{};
  DecisionLatency decision_latency_{};
  int decision_latency_count_ = 0;
};

TEST_F(KillsServerTest, TestMissingKillRequestFieldFails) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  // Create a packet with the correct type but that is missing the kill request
  // payload.
  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_KILL_REQUEST);
  client_socket->packet_to_read_ = request_packet;

  client_socket->on_readable_.Run();

  ASSERT_EQ(handle_kill_request_count_, 0);

  // The invalid packet should not remove the client.
  ASSERT_EQ(disconnected_count_, 0);
}

TEST_F(KillsServerTest, TestHandleKillRequestCallsKillRequestHandler) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_KILL_REQUEST);
  request_packet.mutable_kill_decision_request()->set_size_kb(10);
  client_socket->packet_to_read_ = request_packet;

  client_socket->on_readable_.Run();

  ASSERT_EQ(handle_kill_request_count_, 1);
}

TEST_F(KillsServerTest, TestHandleKillRequestWriteFailureRemovesClient) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_KILL_REQUEST);
  request_packet.mutable_kill_decision_request()->set_size_kb(10);
  client_socket->packet_to_read_ = request_packet;

  client_socket->write_result_ = false;

  client_socket->on_readable_.Run();

  // The invalid packet should not remove the client.
  ASSERT_EQ(disconnected_count_, 1);
  ASSERT_EQ(FakeVmSocket::instance_count_, 1u);
}

TEST_F(KillsServerTest, TestHandleKillRequestSendsCorrectResponse) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_KILL_REQUEST);
  request_packet.mutable_kill_decision_request()->set_sequence_num(6);
  request_packet.mutable_kill_decision_request()->set_size_kb(10);
  client_socket->packet_to_read_ = request_packet;

  // The kill request is for 10 kb, but only return 5kb. Make sure the server is
  // returning the value from the kill request handler and not just echoing the
  // value from the packet.
  kill_request_result_ = KiB(5);

  client_socket->on_readable_.Run();

  ASSERT_EQ(client_socket->written_packet_.type(),
            PacketType::PACKET_TYPE_KILL_DECISION);
  ASSERT_TRUE(client_socket->written_packet_.has_kill_decision_response());
  ASSERT_EQ(
      client_socket->written_packet_.kill_decision_response().sequence_num(),
      6);
  ASSERT_EQ(
      client_socket->written_packet_.kill_decision_response().size_freed_kb(),
      5);
}

TEST_F(KillsServerTest, TestNoKillCandidatesCallsNotification) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_NO_KILL_CANDIDATES);
  client_socket->packet_to_read_ = request_packet;

  client_socket->on_readable_.Run();

  ASSERT_EQ(no_kill_candidates_client_.cid, 10);
}

TEST_F(KillsServerTest, TestMissingDecisionLatencyDoesNothing) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_DECISION_LATENCY);
  client_socket->packet_to_read_ = request_packet;

  client_socket->on_readable_.Run();

  ASSERT_EQ(decision_latency_count_, 0);
}

TEST_F(KillsServerTest, TestDecisionLatencyIsReceivedProperly) {
  AssertListeningSucceeds(*kills_server_);
  ConnectNewClient(*kills_server_, 10, ConnectionType::CONNECTION_TYPE_KILLS);

  FakeVmSocket* client_socket = leaked_client_sockets_.back();

  VmMemoryManagementPacket request_packet;
  request_packet.set_type(PacketType::PACKET_TYPE_DECISION_LATENCY);
  request_packet.mutable_decision_latency()->set_sequence_num(67);
  request_packet.mutable_decision_latency()->set_latency_ms(44);
  client_socket->packet_to_read_ = request_packet;

  client_socket->on_readable_.Run();

  ASSERT_EQ(decision_latency_count_, 1);
  ASSERT_EQ(decision_latency_client_.cid, 10);
  ASSERT_EQ(decision_latency_.sequence_num(), 67);
  ASSERT_EQ(decision_latency_.latency_ms(), 44);
}

}  // namespace
}  // namespace vm_tools::concierge::mm
