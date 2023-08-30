// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/mm/reclaim_broker.h"

#include <sys/socket.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <tuple>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/test/task_environment.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include <vm_memory_management/vm_memory_management.pb.h>

#include "vm_tools/concierge/mm/fake_reclaim_server.h"
#include "vm_tools/concierge/mm/mglru_test_util.h"

namespace vm_tools::concierge::mm {
namespace {

using mglru::AddGeneration;
using mglru::AddMemcg;
using mglru::AddNode;
using mglru::StatsToString;

using vm_tools::vm_memory_management::ConnectionType;
using vm_tools::vm_memory_management::MglruMemcg;
using vm_tools::vm_memory_management::MglruNode;
using vm_tools::vm_memory_management::ResizePriority;

class ReclaimBrokerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = base::TimeTicks::Now();

    ASSERT_TRUE(base::CreateTemporaryFile(&local_mglru_));

    std::unique_ptr<FakeReclaimServer> reclaim_server =
        std::make_unique<FakeReclaimServer>();

    // Leak this pointer so tests can set expectations on the server.
    reclaim_server_ = reclaim_server.get();

    reclaim_broker_ = ReclaimBroker::Create(
        {local_mglru_, std::move(reclaim_server),
         base::BindRepeating(&ReclaimBrokerTest::LowestUnblockedPriority,
                             base::Unretained(this)),
         base::BindRepeating(&ReclaimBrokerTest::ReclaimHandler,
                             base::Unretained(this)),
         base::BindRepeating(&ReclaimBrokerTest::Now, base::Unretained(this)),
         0});

    ASSERT_TRUE(reclaim_broker_);

    client_connection_handler_ = reclaim_server_->ClientConnectionCallback();

    new_generation_notification_ = reclaim_server_->NewGenerationCallback();
  }

  void TearDown() override { brillo::DeleteFile(local_mglru_); }

  void SetupLocalMglruStats(const MglruStats& stats) {
    ASSERT_TRUE(
        base::WriteFile(local_mglru_, StatsToString(stats, getpagesize())));
  }

  ResizePriority LowestUnblockedPriority() { return lowest_block_priority_; }

  void ReclaimHandler(const BalloonBroker::ReclaimOperation& reclaim_operation,
                      ResizePriority) {
    reclaim_events_.emplace_back(reclaim_operation);
  }

  base::TimeTicks Now() { return now_; }

  base::FilePath local_mglru_{};
  static constexpr Client guest_1 = {
      .cid = 10,
      .connection_id = 2,
      .type = ConnectionType::CONNECTION_TYPE_STATS,
  };
  static constexpr Client guest_2 = {
      .cid = 11,
      .connection_id = 3,
      .type = ConnectionType::CONNECTION_TYPE_STATS,
  };

  base::test::TaskEnvironment task_environment_;

  FakeReclaimServer* reclaim_server_;
  std::unique_ptr<ReclaimBroker> reclaim_broker_;

  ReclaimServer::ClientConnectionNotification client_connection_handler_{};
  ReclaimServer::NewGenerationNotification new_generation_notification_{};

  base::TimeTicks now_{};

  ResizePriority lowest_block_priority_ =
      ResizePriority::RESIZE_PRIORITY_LOWEST;
  std::vector<BalloonBroker::ReclaimOperation> reclaim_events_{};
  size_t bytes_reclaimed_{};
};

TEST_F(ReclaimBrokerTest, TestLocalGenNoClients) {
  MglruStats local_stats;
  MglruMemcg* cg = AddMemcg(&local_stats, 1);
  MglruNode* node = AddNode(cg, 2);
  AddGeneration(node, 3, 4, 5, 6);

  // Since there is only the local client, nothing should be reclaimed
  new_generation_notification_.Run(VMADDR_CID_LOCAL, local_stats);
  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestGuestGenNoClients) {
  new_generation_notification_.Run(5, {});

  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestAdjacentGenProportion) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 10, 10, 10);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 20, 20);
  AddGeneration(node, 2, 5, 20, 20);

  client_connection_handler_.Run(guest_1);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  // The host's oldest generation is 10, so none of the guest's 2nd
  // gen should be reclaimed, but a proportion of the guest's 1st
  // gen should be reclaimed (20 * (20 - 10)) / (20 - 5) = 13

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(guest_1.cid));
  ASSERT_EQ(reclaim_event[guest_1.cid], KiB(13));
}

TEST_F(ReclaimBrokerTest, TestHostGenOneClient) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);

  client_connection_handler_.Run(guest_1);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  // The host's oldest generation is age 13, which means that all generations
  // older than 13 should be reclaimed in the guest. Since the guest's only
  // generation is age 20, a proportion of that generation should be reclaimed:
  // ((20 - 13) / 20) * (10) = 3
  // Note that only file cache is counted for ARCVM and not anon cache

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(guest_1.cid));
  ASSERT_EQ(reclaim_event[guest_1.cid], KiB(3));
}

TEST_F(ReclaimBrokerTest, TestMultipleCgsAndNodes) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 17, 0, 10);
  cg = AddMemcg(&host_stats, 2);
  node = AddNode(cg, 2);
  AddGeneration(node, 2, 20, 0, 24);
  node = AddNode(cg, 3);
  AddGeneration(node, 3, 25, 0, 32);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 0, 50);
  cg = AddMemcg(&guest_stats, 2);
  node = AddNode(cg, 2);
  AddGeneration(node, 2, 17, 0, 30);

  client_connection_handler_.Run(guest_1);

  // Local stats should be read from the MGLRU file directly
  SetupLocalMglruStats(host_stats);

  new_generation_notification_.Run(guest_1.cid, guest_stats);

  // The youngest oldest gen is the guest's of age 17. The host has two
  // generations that are older than 17 so the portion of each of those
  // generations that is older than 17 should be reclaimed. Age 20: (20 - 17) /
  // 20 * 24 = 3 Age 25: (25 - 17) / 25 * 32 = 10

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(VMADDR_CID_LOCAL));
  ASSERT_EQ(reclaim_event[VMADDR_CID_LOCAL], KiB(13));
}

TEST_F(ReclaimBrokerTest, TestMultipleGenerations) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 12, 10, 10);
  AddGeneration(node, 3, 11, 10, 10);
  AddGeneration(node, 4, 10, 10, 10);
  AddGeneration(node, 5, 9, 10, 10);
  AddGeneration(node, 6, 8, 10, 10);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);
  AddGeneration(node, 2, 19, 10, 10);
  AddGeneration(node, 3, 18, 10, 10);
  AddGeneration(node, 4, 17, 10, 10);
  AddGeneration(node, 5, 16, 10, 10);
  AddGeneration(node, 6, 15, 10, 10);
  AddGeneration(node, 7, 10, 10, 10);
  AddGeneration(node, 8, 8, 10, 10);
  AddGeneration(node, 9, 6, 10, 10);

  client_connection_handler_.Run(guest_1);

  // Local stats should be read from the MGLRU file directly
  SetupLocalMglruStats(host_stats);

  new_generation_notification_.Run(guest_1.cid, guest_stats);

  // Since there is only one guest and it is the one sending the new generation
  // event no other stats should be requested
  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 0);

  // The host's oldest generation is 13, and the guest has 50kb + 4kb file
  // older than 13

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(guest_1.cid));
  ASSERT_EQ(reclaim_event[guest_1.cid], KiB(54));
}

TEST_F(ReclaimBrokerTest, TestGuestGenOneGuest) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 500, 100);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 5, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  client_connection_handler_.Run(guest_1);

  // Local stats should be read from the MGLRU file directly
  SetupLocalMglruStats(host_stats);

  new_generation_notification_.Run(guest_1.cid, guest_stats);

  // Since there is only one guest and it is the one sending the new generation
  // event no other stats should be requested
  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 0);

  // Only file should be counted for both the host and guest, and since the
  // oldest gen of the guest is age 5: (13 - 5) / 13 * (100) = 61
  // should be reclaimed from the host
  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(VMADDR_CID_LOCAL));
  ASSERT_EQ(reclaim_event[VMADDR_CID_LOCAL], KiB(61));
}

TEST_F(ReclaimBrokerTest, TestHostGenMultipleGuests) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 3, 0, 10);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 5, 0, 10);
  AddGeneration(node, 2, 1, 0, 10);

  MglruStats guest2_stats;
  cg = AddMemcg(&guest2_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 10, 0, 10);
  AddGeneration(node, 2, 1, 0, 10);

  client_connection_handler_.Run(guest_1);
  client_connection_handler_.Run(guest_2);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;
  reclaim_server_->mglru_stats_[guest_2.cid] = guest2_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  // Since a new generation event was received from the host, the reclaim broker
  // should have requested stats from each of the guests.
  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 2);

  // Check that stats from both VM CIDs were requested.
  ASSERT_EQ(reclaim_server_->stats_requests_.front(), guest_1.cid);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_2.cid);

  // The youngest oldest generation is the host's generation of age 3, so
  // everything older than 3 should be reclaimed from the guests.
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 2);
  ASSERT_TRUE(reclaim_event.contains(guest_1.cid));
  ASSERT_TRUE(reclaim_event.contains(guest_2.cid));

  // Guest 1 has 2 generations. The second generation is entirely younger than
  // 3, so nothing from it should be reclaimed. The first generation has a
  // proportion older than 3:
  // gen_size * duration_older / gen_duration: 10 * 2 / 4 = 5
  ASSERT_EQ(reclaim_event[guest_1.cid], KiB(5));
  // Guest 2 has 2 generations. The second generation is entirely younger than
  // 3, so nothing from it should be reclaimed. The first generation has a
  // proportion older than 3:
  // gen_size * duration_older / gen_duration: 10 * 7 / 9  = 7
  ASSERT_EQ(reclaim_event[guest_2.cid], KiB(7));
}

TEST_F(ReclaimBrokerTest, TestGuestGenMultipleGuests) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 12, 800, 900);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 12, 10, 10);

  MglruStats guest2_stats;
  cg = AddMemcg(&guest2_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 10, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  client_connection_handler_.Run(guest_1);
  client_connection_handler_.Run(guest_2);

  // Local stats should be read from the MGLRU file directly
  SetupLocalMglruStats(host_stats);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;
  reclaim_server_->mglru_stats_[guest_2.cid] = guest2_stats;

  new_generation_notification_.Run(guest_2.cid, guest2_stats);

  // Since the event was triggered by a guest generation, only the other guest's
  // stats should be requested
  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.front(), guest_1.cid);

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 2);
  ASSERT_TRUE(reclaim_event.contains(VMADDR_CID_LOCAL));
  ASSERT_TRUE(reclaim_event.contains(guest_1.cid));
  ASSERT_EQ(reclaim_event[VMADDR_CID_LOCAL], KiB(150));
  ASSERT_EQ(reclaim_event[guest_1.cid], KiB(1));
}

TEST_F(ReclaimBrokerTest, TestRemoveVm) {
  client_connection_handler_.Run(guest_1);
  client_connection_handler_.Run(guest_2);

  // Remove guest 1
  reclaim_broker_->RemoveVm(guest_1.cid);

  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 12, 10, 10);
  AddGeneration(node, 1, 3, 10, 10);

  MglruStats guest2_stats;
  cg = AddMemcg(&guest2_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  reclaim_server_->mglru_stats_[guest_2.cid] = guest2_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  // Since guest_1 is disconnected, only stats from guest 2 should have been
  // requested.
  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.front(), guest_2.cid);

  ASSERT_EQ(reclaim_events_.size(), 1);
  BalloonBroker::ReclaimOperation& reclaim_event = reclaim_events_.back();
  ASSERT_EQ(reclaim_event.size(), 1);
  ASSERT_TRUE(reclaim_event.contains(guest_2.cid));
  ASSERT_EQ(reclaim_event[guest_2.cid], KiB(4));
}

TEST_F(ReclaimBrokerTest, TestReclaimThreshold) {
  std::unique_ptr<FakeReclaimServer> reclaim_server =
      std::make_unique<FakeReclaimServer>();
  reclaim_server_ = reclaim_server.get();

  reclaim_broker_ = ReclaimBroker::Create(
      {local_mglru_, std::move(reclaim_server),
       base::BindRepeating(&ReclaimBrokerTest_TestReclaimThreshold_Test::
                               LowestUnblockedPriority,
                           base::Unretained(this)),
       base::BindRepeating(
           &ReclaimBrokerTest_TestReclaimThreshold_Test::ReclaimHandler,
           base::Unretained(this)),
       base::BindRepeating(&ReclaimBrokerTest_TestReclaimThreshold_Test::Now,
                           base::Unretained(this)),
       MiB(1)});

  ASSERT_TRUE(reclaim_broker_);

  client_connection_handler_ = reclaim_server_->ClientConnectionCallback();
  new_generation_notification_ = reclaim_server_->NewGenerationCallback();

  client_connection_handler_.Run(guest_1);

  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 12, 10, 10);
  AddGeneration(node, 1, 3, 10, 10);

  MglruStats guest1_stats;
  cg = AddMemcg(&guest1_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest1_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.front(), guest_1.cid);

  // The expected reclaim is below the reclaim threshold, nothing should be
  // performed

  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestAllBalloonsBlockedDoesNotReclaim) {
  client_connection_handler_.Run(guest_1);

  // Set the lowest blocked priority to cached app. At this priority, nothing
  // should be able to be reclaimed and the ReclaimBroker should do nothing.
  lowest_block_priority_ = ResizePriority::RESIZE_PRIORITY_CACHED_APP;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, {});

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 0);
  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestReclaimIntervalRespected) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);

  client_connection_handler_.Run(guest_1);

  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  ASSERT_EQ(reclaim_events_.size(), 1);

  // Fast forward by some amount less than the reclaim interval.
  now_ += base::Seconds(10);

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  // The number of reclaim events should not have increased.
  ASSERT_EQ(reclaim_events_.size(), 1);

  // Fast forward past the reclaim interval
  now_ += base::Seconds(100);

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  // Now the number of reclaim events should have increased.
  ASSERT_EQ(reclaim_events_.size(), 2);
}

TEST_F(ReclaimBrokerTest, TestTooManyCgsAreRejected) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  // These stats would result in reclaim if they were properly formed.
  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);

  // Add a bunch of dummy cgs that should cause the stats to be rejected.
  for (size_t i = 2; i < 20; i++) {
    cg = AddMemcg(&guest_stats, i);
  }

  client_connection_handler_.Run(guest_1);
  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  // Nothing should be reclaimed since the guest stats were rejected.
  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestTooManyNodesAreRejected) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  // These stats would result in reclaim if they were properly formed.
  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);

  // Add a bunch of dummy nodes that should cause the stats to be rejected.
  for (size_t i = 2; i < 20; i++) {
    node = AddNode(cg, i);
  }

  client_connection_handler_.Run(guest_1);
  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  // Nothing should be reclaimed since the guest stats were rejected.
  ASSERT_EQ(reclaim_events_.size(), 0);
}

TEST_F(ReclaimBrokerTest, TestTooManyGenerationsAreRejected) {
  MglruStats host_stats;
  MglruMemcg* cg = AddMemcg(&host_stats, 1);
  MglruNode* node = AddNode(cg, 1);
  AddGeneration(node, 1, 13, 10, 10);
  AddGeneration(node, 2, 1, 10, 10);

  // These stats would result in reclaim if they were properly formed.
  MglruStats guest_stats;
  cg = AddMemcg(&guest_stats, 1);
  node = AddNode(cg, 1);
  AddGeneration(node, 1, 20, 10, 10);

  // Add a bunch of dummy nodes that should cause the stats to be rejected.
  for (size_t i = 2; i < 20; i++) {
    AddGeneration(node, i, 0, 0, 0);
  }

  client_connection_handler_.Run(guest_1);
  reclaim_server_->mglru_stats_[guest_1.cid] = guest_stats;

  new_generation_notification_.Run(VMADDR_CID_LOCAL, host_stats);

  ASSERT_EQ(reclaim_server_->stats_requests_.size(), 1);
  ASSERT_EQ(reclaim_server_->stats_requests_.back(), guest_1.cid);

  // Nothing should be reclaimed since the guest stats were rejected.
  ASSERT_EQ(reclaim_events_.size(), 0);
}

}  // namespace
}  // namespace vm_tools::concierge::mm
