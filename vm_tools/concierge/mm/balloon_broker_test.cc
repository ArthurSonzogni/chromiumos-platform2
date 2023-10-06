// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unordered_map>
#include <utility>

#include <sys/socket.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <gtest/gtest.h>

#include <base/test/task_environment.h>

#include "vm_tools/concierge/mm/balloon_broker.h"
#include "vm_tools/concierge/mm/fake_balloon_blocker.h"
#include "vm_tools/concierge/mm/fake_kills_server.h"

using vm_tools::vm_memory_management::ConnectionType;

namespace vm_tools::concierge::mm {

bool operator==(const ResizeRequest& lhs, const ResizeRequest& rhs) {
  return lhs.GetPriority() == rhs.GetPriority() &&
         lhs.GetDeltaBytes() == rhs.GetDeltaBytes();
}

std::unique_ptr<BalloonBlocker> CreateFakeBalloonBlocker(
    int vm_cid, const std::string&, scoped_refptr<base::SequencedTaskRunner>) {
  return std::make_unique<FakeBalloonBlocker>(vm_cid);
}

namespace {

class BalloonBrokerTest : public ::testing::Test {
 public:
  void SetUp() override {
    std::unique_ptr<FakeKillsServer> fake_kills_server =
        std::make_unique<FakeKillsServer>();

    // Leak this pointer so tests can set expectations on the fake server.
    fake_kills_server_ = fake_kills_server.get();

    balloon_broker_ = std::make_unique<BalloonBroker>(
        std::move(fake_kills_server),
        base::SequencedTaskRunner::GetCurrentDefault(),
        &CreateFakeBalloonBlocker);

    client_connection_handler_ = fake_kills_server_->ClientConnectionCallback();

    client_disconnected_handler_ =
        fake_kills_server_->ClientDisconnectedCallback();

    kill_request_handler_ = fake_kills_server_->KillRequestHandler();

    no_kill_candidate_handler_ = fake_kills_server_->NoKillCandidateCallback();
  }

  void TearDown() override {
    FakeBalloonBlocker::fake_balloon_blockers_.clear();
  }

 protected:
  static constexpr char kTestSocket[] = "/run/test-socket";
  static constexpr char kTestSocket2[] = "/run/test-socket2";

  void ExpectNoResize(const int cid) {
    EXPECT_EQ(FakeBalloonBlocker::fake_balloon_blockers_[cid]
                  ->resize_requests_.size(),
              0);
  }

  void ExpectNoResizes() {
    for (auto balloon_blocker : FakeBalloonBlocker::fake_balloon_blockers_) {
      EXPECT_EQ(balloon_blocker.second->resize_requests_.size(), 0);
    }
  }

  void ExpectResizeRequest(int cid, ResizeRequest request) {
    EXPECT_EQ(FakeBalloonBlocker::fake_balloon_blockers_[cid]
                  ->resize_requests_.back(),
              request)
        << "Resize Requests not equal. Expected: " << request.GetDeltaBytes()
        << " Actual: "
        << FakeBalloonBlocker::fake_balloon_blockers_[cid]
               ->resize_requests_.back()
               .GetDeltaBytes();
  }

  std::unique_ptr<BalloonBroker> balloon_broker_{};
  FakeKillsServer* fake_kills_server_{};

  Server::ClientConnectionNotification client_connection_handler_{};
  Server::ClientDisconnectedNotification client_disconnected_handler_{};
  KillsServer::KillRequestHandler kill_request_handler_{};
  KillsServer::NoKillCandidateNotification no_kill_candidate_handler_{};
  base::test::TaskEnvironment task_environment_;
};

TEST_F(BalloonBrokerTest, TestRegisterVm) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  ASSERT_EQ(FakeBalloonBlocker::fake_balloon_blockers_.size(), 1);
  balloon_broker_->RegisterVm(6, kTestSocket2);
  ASSERT_EQ(FakeBalloonBlocker::fake_balloon_blockers_.size(), 2);
}

TEST_F(BalloonBrokerTest, TestNoConnections) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  Client host_client{.cid = VMADDR_CID_LOCAL,
                     .connection_id = 1,
                     .type = ConnectionType::CONNECTION_TYPE_KILLS};

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            0);
  // There are two VMs, but neither has clients connected. No balloons should be
  // changed
  ExpectNoResizes();
}

TEST_F(BalloonBrokerTest, TestOneConnectionHostKillRequest) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  Client host_client{VMADDR_CID_LOCAL, 0,
                     ConnectionType::CONNECTION_TYPE_KILLS};

  Client connected_client{6, 1, ConnectionType::CONNECTION_TYPE_KILLS};

  client_connection_handler_.Run(host_client);
  client_connection_handler_.Run(connected_client);

  FakeBalloonBlocker::fake_balloon_blockers_[6]
      ->try_resize_results_.emplace_back(512);

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            512);
  // Only the connected VM should have its balloon changed
  ExpectResizeRequest(6, {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 512});
}

TEST_F(BalloonBrokerTest, TestMultipleConnectionsHostKillRequest) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  Client host_client{VMADDR_CID_LOCAL, 0,
                     ConnectionType::CONNECTION_TYPE_KILLS};
  Client client1{5, 1, ConnectionType::CONNECTION_TYPE_KILLS};
  Client client2{6, 2, ConnectionType::CONNECTION_TYPE_KILLS};

  client_connection_handler_.Run(host_client);
  client_connection_handler_.Run(client1);
  client_connection_handler_.Run(client2);

  // Both balloons should successfully resize by 256 bytes
  FakeBalloonBlocker::fake_balloon_blockers_[5]
      ->try_resize_results_.emplace_back(256);
  FakeBalloonBlocker::fake_balloon_blockers_[6]
      ->try_resize_results_.emplace_back(256);

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            512);

  // Both balloons should be inflated by an equal amount
  ExpectResizeRequest(5, {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 256});
  ExpectResizeRequest(6, {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 256});
}

TEST_F(BalloonBrokerTest, TestGuestDisconnectHostKillRequest) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  Client host_client{VMADDR_CID_LOCAL, 0,
                     ConnectionType::CONNECTION_TYPE_KILLS};
  Client client{6, 1, ConnectionType::CONNECTION_TYPE_KILLS};

  client_connection_handler_.Run(host_client);
  client_connection_handler_.Run({5, 2, ConnectionType::CONNECTION_TYPE_KILLS});
  client_connection_handler_.Run(client);

  balloon_broker_->RemoveVm(5);
  EXPECT_FALSE(FakeBalloonBlocker::fake_balloon_blockers_.contains(5));

  // The balloon should successfully resize
  FakeBalloonBlocker::fake_balloon_blockers_[6]
      ->try_resize_results_.emplace_back(512);

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            512);

  // Only 6 is connected now, so it should be the only one inflated
  ExpectResizeRequest(6, {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 512});
}

TEST_F(BalloonBrokerTest, TestGuestKillRequest) {
  client_connection_handler_.Run(
      {VMADDR_CID_LOCAL, 0, ConnectionType::CONNECTION_TYPE_KILLS});

  balloon_broker_->RegisterVm(5, kTestSocket);
  Client client{5, 1, ConnectionType::CONNECTION_TYPE_KILLS};
  client_connection_handler_.Run(client);

  FakeBalloonBlocker::fake_balloon_blockers_[5]
      ->try_resize_results_.emplace_back(-512);

  ASSERT_EQ(kill_request_handler_.Run(
                client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            512);
  // The balloon should have been deflated since this is a client initiated kill
  // request
  ExpectResizeRequest(5, {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, -512});
}

TEST_F(BalloonBrokerTest, TestReclaimWithNoConnectedVms) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  BalloonBroker::ReclaimOperation reclaim_operation{{VMADDR_CID_LOCAL, 512}};

  balloon_broker_->Reclaim(reclaim_operation,
                           ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM);

  ExpectNoResizes();
}

TEST_F(BalloonBrokerTest, TestReclaimFromHost) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  client_connection_handler_.Run({5, 0, ConnectionType::CONNECTION_TYPE_KILLS});
  client_connection_handler_.Run({6, 1, ConnectionType::CONNECTION_TYPE_KILLS});

  FakeBalloonBlocker::fake_balloon_blockers_[5]
      ->try_resize_results_.emplace_back(-256);
  FakeBalloonBlocker::fake_balloon_blockers_[6]
      ->try_resize_results_.emplace_back(-256);

  BalloonBroker::ReclaimOperation reclaim_operation{{VMADDR_CID_LOCAL, 512}};

  balloon_broker_->Reclaim(reclaim_operation,
                           ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM);

  ExpectResizeRequest(5, {ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM, -256});
  ExpectResizeRequest(6, {ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM, -256});
}

TEST_F(BalloonBrokerTest, TestReclaimFromGuest) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  client_connection_handler_.Run({5, 0, ConnectionType::CONNECTION_TYPE_KILLS});
  client_connection_handler_.Run({6, 1, ConnectionType::CONNECTION_TYPE_KILLS});

  FakeBalloonBlocker::fake_balloon_blockers_[5]
      ->try_resize_results_.emplace_back(512);

  BalloonBroker::ReclaimOperation reclaim_operation{{5, 512}};

  balloon_broker_->Reclaim(reclaim_operation,
                           ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM);

  ExpectResizeRequest(5, {ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM, 512});
}

TEST_F(BalloonBrokerTest, TestReclaimFromHostAndGuest) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  client_connection_handler_.Run({5, 0, ConnectionType::CONNECTION_TYPE_KILLS});
  client_connection_handler_.Run({6, 1, ConnectionType::CONNECTION_TYPE_KILLS});

  FakeBalloonBlocker::fake_balloon_blockers_[5]
      ->try_resize_results_.emplace_back(-150);
  FakeBalloonBlocker::fake_balloon_blockers_[6]
      ->try_resize_results_.emplace_back(-250);

  BalloonBroker::ReclaimOperation reclaim_operation{{5, 100},
                                                    {VMADDR_CID_LOCAL, 500}};

  balloon_broker_->Reclaim(reclaim_operation,
                           ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM);

  ExpectResizeRequest(5, {ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM, -150});
  ExpectResizeRequest(6, {ResizePriority::RESIZE_PRIORITY_MGLRU_RECLAIM, -250});
}

TEST_F(BalloonBrokerTest, TestClientDisconnected) {
  int vm_cid = 5;
  balloon_broker_->RegisterVm(vm_cid, kTestSocket);

  Client host_client{VMADDR_CID_LOCAL, 0,
                     ConnectionType::CONNECTION_TYPE_KILLS};

  Client client1{vm_cid, 1, ConnectionType::CONNECTION_TYPE_KILLS};
  Client client2{vm_cid, 2, ConnectionType::CONNECTION_TYPE_KILLS};

  client_connection_handler_.Run(host_client);
  client_connection_handler_.Run(client1);
  client_connection_handler_.Run(client2);

  FakeBalloonBlocker::fake_balloon_blockers_[vm_cid]
      ->try_resize_results_.emplace_back(512);

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            512);

  ExpectResizeRequest(vm_cid,
                      {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 512});

  // Even after one of the clients has disconnected, the balloon should still be
  // resized since one client remains
  client_disconnected_handler_.Run(client1);

  FakeBalloonBlocker::fake_balloon_blockers_[vm_cid]
      ->try_resize_results_.emplace_back(256);

  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 256, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            256);

  ExpectResizeRequest(vm_cid,
                      {ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB, 256});

  // Now that both clients have disconnected, no more resizes should be
  // performed
  client_disconnected_handler_.Run(client2);
  ASSERT_EQ(FakeBalloonBlocker::fake_balloon_blockers_.size(), 0);
  ASSERT_EQ(kill_request_handler_.Run(
                host_client, 512, ResizePriority::RESIZE_PRIORITY_FOCUSED_TAB),
            0);
}

TEST_F(BalloonBrokerTest, TestLowestUnblockedPriority) {
  balloon_broker_->RegisterVm(5, kTestSocket);
  balloon_broker_->RegisterVm(6, kTestSocket2);

  // By default if nothing is blocked, the lowest block priority is LOWEST.
  ASSERT_EQ(balloon_broker_->LowestUnblockedPriority(),
            ResizePriority::RESIZE_PRIORITY_LOWEST);

  // VM 5 is fully blocked at cached app priority, but VM 6 is not, so the
  // lowest block should still be LOWEST.
  FakeBalloonBlocker::fake_balloon_blockers_[5]->BlockAt(
      ResizeDirection::kInflate, ResizePriority::RESIZE_PRIORITY_CACHED_APP);
  FakeBalloonBlocker::fake_balloon_blockers_[5]->BlockAt(
      ResizeDirection::kDeflate, ResizePriority::RESIZE_PRIORITY_CACHED_APP);
  ASSERT_EQ(balloon_broker_->LowestUnblockedPriority(),
            ResizePriority::RESIZE_PRIORITY_LOWEST);

  // Now that VM 6 is also fully blocked at cached app priority, the lowest
  // block priority should be CACHED_APP which means CACHED_TAB is the lowest
  // unblocked priority.
  FakeBalloonBlocker::fake_balloon_blockers_[6]->BlockAt(
      ResizeDirection::kInflate, ResizePriority::RESIZE_PRIORITY_CACHED_APP);
  FakeBalloonBlocker::fake_balloon_blockers_[6]->BlockAt(
      ResizeDirection::kDeflate, ResizePriority::RESIZE_PRIORITY_CACHED_APP);
  ASSERT_EQ(balloon_broker_->LowestUnblockedPriority(),
            ResizePriority::RESIZE_PRIORITY_CACHED_TAB);

  // Check that when every balloon is fully blocked, UNSPECIFIED is returned.
  FakeBalloonBlocker::fake_balloon_blockers_[5]->BlockAt(
      ResizeDirection::kInflate, ResizePriority::RESIZE_PRIORITY_HIGHEST);
  FakeBalloonBlocker::fake_balloon_blockers_[5]->BlockAt(
      ResizeDirection::kDeflate, ResizePriority::RESIZE_PRIORITY_HIGHEST);
  FakeBalloonBlocker::fake_balloon_blockers_[6]->BlockAt(
      ResizeDirection::kInflate, ResizePriority::RESIZE_PRIORITY_HIGHEST);
  FakeBalloonBlocker::fake_balloon_blockers_[6]->BlockAt(
      ResizeDirection::kDeflate, ResizePriority::RESIZE_PRIORITY_HIGHEST);
  ASSERT_EQ(balloon_broker_->LowestUnblockedPriority(),
            ResizePriority::RESIZE_PRIORITY_UNSPECIFIED);
}

TEST_F(BalloonBrokerTest, TestHandleNoKillCandidates) {
  int vm_cid = 5;
  balloon_broker_->RegisterVm(vm_cid, kTestSocket);

  Client host_client{VMADDR_CID_LOCAL, 0,
                     ConnectionType::CONNECTION_TYPE_KILLS};
  Client host_client2{VMADDR_CID_LOCAL, 1,
                      ConnectionType::CONNECTION_TYPE_KILLS};

  Client client1{vm_cid, 2, ConnectionType::CONNECTION_TYPE_KILLS};
  Client client2{vm_cid, 3, ConnectionType::CONNECTION_TYPE_KILLS};

  client_connection_handler_.Run(host_client);
  client_connection_handler_.Run(host_client2);
  client_connection_handler_.Run(client1);
  client_connection_handler_.Run(client2);

  // Only the first client has no kill candidates so no resizes should be
  // performed
  no_kill_candidate_handler_.Run(host_client2);
  ExpectNoResizes();

  FakeBalloonBlocker::fake_balloon_blockers_[vm_cid]
      ->try_resize_results_.emplace_back(MiB(128));
  no_kill_candidate_handler_.Run(host_client);
  // Both clients have no kill candidates, so the guest balloon should be
  // inflated by the no kill candidate reclaim amount (64MB)
  ExpectResizeRequest(
      vm_cid,
      {ResizePriority::RESIZE_PRIORITY_NO_KILL_CANDIDATES_HOST, MiB(128)});
}

}  // namespace
}  // namespace vm_tools::concierge::mm
