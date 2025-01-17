// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/routing_service.h"

#include <sys/socket.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

#include <base/files/scoped_file.h>
#include <base/strings/stringprintf.h>
#include <base/types/cxx23_to_underlying.h>
#include <gtest/gtest.h>

#include "patchpanel/mock_lifeline_fd_service.h"
#include "patchpanel/mock_system.h"
#include "patchpanel/system.h"

using testing::Return;
using testing::WithArgs;

namespace patchpanel {
namespace {

std::string hex(uint32_t val) {
  return base::StringPrintf("0x%08x", val);
}

struct sockopt_data {
  int sockfd;
  int level;
  int optname;
  char optval[256];
  socklen_t optlen;
};

void SetOptval(sockopt_data* sockopt, uint32_t optval) {
  sockopt->optlen = sizeof(optval);
  memcpy(sockopt->optval, &optval, sizeof(optval));
}

uint32_t GetOptval(const sockopt_data& sockopt) {
  uint32_t optval;
  memcpy(&optval, sockopt.optval, sizeof(optval));
  return optval;
}

Fwmark fwmark(uint32_t fwmark) {
  return {.fwmark = fwmark};
}

base::ScopedFD MakeTestSocket() {
  return base::ScopedFD(socket(AF_INET, SOCK_DGRAM, 0));
}

class TestableRoutingService : public RoutingService {
 public:
  TestableRoutingService(System* system, LifelineFDService* lifeline_fd_svc)
      : RoutingService(system, lifeline_fd_svc) {}
  ~TestableRoutingService() = default;

  int GetSockopt(int sockfd,
                 int level,
                 int optname,
                 void* optval,
                 socklen_t* optlen) override {
    sockopt.sockfd = sockfd;
    sockopt.level = level;
    sockopt.optname = optname;
    memcpy(optval, sockopt.optval,
           std::min(*optlen, (socklen_t)sizeof(sockopt.optval)));
    *optlen = sockopt.optlen;
    return getsockopt_ret;
  }

  int SetSockopt(int sockfd,
                 int level,
                 int optname,
                 const void* optval,
                 socklen_t optlen) override {
    sockopt.sockfd = sockfd;
    sockopt.level = level;
    sockopt.optname = optname;
    sockopt.optlen = optlen;
    memcpy(sockopt.optval, optval,
           std::min(optlen, (socklen_t)sizeof(sockopt.optval)));
    return setsockopt_ret;
  }

  // Variables used to mock and track interactions with getsockopt and
  // setsockopt.
  int getsockopt_ret;
  int setsockopt_ret;
  sockopt_data sockopt;
};

class RoutingServiceTest : public testing::Test {
 public:
  RoutingServiceTest() = default;

 protected:
  void SetUp() override {
    ON_CALL(lifeline_fd_svc_, AddLifelineFD)
        .WillByDefault(

            WithArgs<1>([](base::OnceClosure cb) {
              return base::ScopedClosureRunner(base::DoNothing());
            }));
  }

  MockSystem system_;
  MockLifelineFDService lifeline_fd_svc_;
};

}  // namespace

TEST_F(RoutingServiceTest, FwmarkSize) {
  EXPECT_EQ(sizeof(uint32_t), sizeof(Fwmark));
}

TEST_F(RoutingServiceTest, FwmarkOperators) {
  EXPECT_EQ(fwmark(0x00000000), fwmark(0x00000000) | fwmark(0x00000000));
  EXPECT_EQ(fwmark(0x00000000), fwmark(0x00000000) & fwmark(0x00000000));
  EXPECT_EQ(fwmark(0x00110034), fwmark(0x00110034) | fwmark(0x00000000));
  EXPECT_EQ(fwmark(0x00000000), fwmark(0x00110034) & fwmark(0x00000000));
  EXPECT_EQ(fwmark(0x1234abcd), fwmark(0x12340000) | fwmark(0x0000abcd));
  EXPECT_EQ(fwmark(0x00000000), fwmark(0x12340000) & fwmark(0x0000abcd));
  EXPECT_EQ(fwmark(0x00120000), fwmark(0x00120000) & fwmark(0x00120000));
  EXPECT_EQ(fwmark(0x12fffbcd), fwmark(0x1234abcd) | fwmark(0x00fff000));
  EXPECT_EQ(fwmark(0x0034a000), fwmark(0x1234abcd) & fwmark(0x00fff000));
  EXPECT_EQ(fwmark(0x0000ffff), ~fwmark(0xffff0000));
  EXPECT_EQ(fwmark(0x12345678), ~~fwmark(0x12345678));
  EXPECT_EQ(fwmark(0x55443322), ~fwmark(0xaabbccdd));
}

TEST_F(RoutingServiceTest, FwmarkAndMaskConstants) {
  EXPECT_EQ("0x00003f00", kFwmarkAllSourcesMask.ToString());
  EXPECT_EQ("0xffff0000", kFwmarkRoutingMask.ToString());
  EXPECT_EQ("0x00000001", kFwmarkLegacySNAT.ToString());
  EXPECT_EQ("0x0000c000", kFwmarkVpnMask.ToString());
  EXPECT_EQ("0x00008000", kFwmarkRouteOnVpn.ToString());
  EXPECT_EQ("0x00004000", kFwmarkBypassVpn.ToString());
  EXPECT_EQ("0x00002000", kFwmarkForwardedSourcesMask.ToString());
  EXPECT_EQ("0x000000e0", kFwmarkQoSCategoryMask.ToString());

  EXPECT_EQ(0x00003f00, kFwmarkAllSourcesMask.Value());
  EXPECT_EQ(0xffff0000, kFwmarkRoutingMask.Value());
  EXPECT_EQ(0x00000001, kFwmarkLegacySNAT.Value());
  EXPECT_EQ(0x0000c000, kFwmarkVpnMask.Value());
  EXPECT_EQ(0x00008000, kFwmarkRouteOnVpn.Value());
  EXPECT_EQ(0x00004000, kFwmarkBypassVpn.Value());
  EXPECT_EQ(0x00002000, kFwmarkForwardedSourcesMask.Value());
  EXPECT_EQ(0x000000e0, kFwmarkQoSCategoryMask.Value());
}

TEST_F(RoutingServiceTest, FwmarkSources) {
  EXPECT_EQ("0x00000000",
            Fwmark::FromSource(TrafficSource::kUnknown).ToString());
  EXPECT_EQ("0x00000100",
            Fwmark::FromSource(TrafficSource::kChrome).ToString());
  EXPECT_EQ("0x00000200", Fwmark::FromSource(TrafficSource::kUser).ToString());
  EXPECT_EQ("0x00000300",
            Fwmark::FromSource(TrafficSource::kUpdateEngine).ToString());
  EXPECT_EQ("0x00000400",
            Fwmark::FromSource(TrafficSource::kSystem).ToString());
  EXPECT_EQ("0x00000500",
            Fwmark::FromSource(TrafficSource::kHostVpn).ToString());
  EXPECT_EQ("0x00002000", Fwmark::FromSource(TrafficSource::kArc).ToString());
  EXPECT_EQ("0x00002100",
            Fwmark::FromSource(TrafficSource::kCrostiniVM).ToString());
  EXPECT_EQ("0x00002200",
            Fwmark::FromSource(TrafficSource::kParallelsVM).ToString());
  EXPECT_EQ("0x00002300",
            Fwmark::FromSource(TrafficSource::kTetherDownstream).ToString());
  EXPECT_EQ("0x00002400",
            Fwmark::FromSource(TrafficSource::kArcVpn).ToString());

  for (auto ts : kLocalSources) {
    EXPECT_EQ(
        "0x00000000",
        (Fwmark::FromSource(ts) & kFwmarkForwardedSourcesMask).ToString());
  }
  for (auto ts : kForwardedSources) {
    EXPECT_EQ(
        kFwmarkForwardedSourcesMask.ToString(),
        (Fwmark::FromSource(ts) & kFwmarkForwardedSourcesMask).ToString());
  }

  for (auto ts : kLocalSources) {
    EXPECT_EQ("0x00000000",
              (Fwmark::FromSource(ts) & ~kFwmarkAllSourcesMask).ToString());
  }
  for (auto ts : kForwardedSources) {
    EXPECT_EQ("0x00000000",
              (Fwmark::FromSource(ts) & ~kFwmarkAllSourcesMask).ToString());
  }
}

TEST_F(RoutingServiceTest, FwmarkQoSCategories) {
  constexpr QoSCategory kAllCategories[] = {
      QoSCategory::kDefault, QoSCategory::kRealTimeInteractive,
      QoSCategory::kMultimediaConferencing, QoSCategory::kNetworkControl,
      QoSCategory::kWebRTC};
  // The offset of the qos fields defined in Fwmark.
  constexpr auto kOffset = 5;

  for (const auto category : kAllCategories) {
    uint32_t category_int = base::to_underlying(category);
    EXPECT_EQ(category_int, Fwmark::FromQoSCategory(category).qos_category);
    EXPECT_EQ(category_int << kOffset,
              Fwmark::FromQoSCategory(category).Value());
    EXPECT_EQ(hex(category_int << kOffset),
              Fwmark::FromQoSCategory(category).ToString());
  }
}

TEST_F(RoutingServiceTest, TagSocket) {
  ON_CALL(system_, IfNametoindex("eth0")).WillByDefault(Return(1));
  ON_CALL(system_, IfNametoindex("eth1")).WillByDefault(Return(2));
  ON_CALL(system_, IfNametoindex("eth2")).WillByDefault(Return(3));

  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;
  svc->AssignInterfaceToNetwork(1, "eth0", MakeTestSocket());
  svc->AssignInterfaceToNetwork(34567, "eth1", MakeTestSocket());
  svc->AssignInterfaceToNetwork(12, "eth2", MakeTestSocket());

  using Policy = VPNRoutingPolicy;
  struct {
    std::optional<int> network_id;
    Policy policy;
    std::optional<TrafficAnnotationId> annotation_id;
    uint32_t initial_fwmark;
    uint32_t expected_fwmark;
  } testcases[] = {
      {std::nullopt, Policy::kRouteOnVPN, std::nullopt, 0x0, 0x00008000},
      {std::nullopt, Policy::kBypassVPN, std::nullopt, 0x0, 0x00004000},
      {std::nullopt, Policy::kRouteOnVPN, std::nullopt, 0x1, 0x00008001},
      {1, Policy::kBypassVPN, std::nullopt, 0xabcd00ef, 0x03e940ef},
      {std::nullopt, Policy::kRouteOnVPN, std::nullopt, 0x11223344, 0x0000b344},
      {34567, Policy::kBypassVPN, std::nullopt, 0x11223344, 0x03ea7344},
      {std::nullopt, Policy::kRouteOnVPN, std::nullopt, 0x00008000, 0x00008000},
      {std::nullopt, Policy::kBypassVPN, std::nullopt, 0x00004000, 0x00004000},
      {std::nullopt, Policy::kBypassVPN, std::nullopt, 0x00008000, 0x00004000},
      {std::nullopt, Policy::kRouteOnVPN, std::nullopt, 0x00004000, 0x00008000},
      {1, Policy::kDefault, std::nullopt, 0x00008000, 0x03e90000},
      {12, Policy::kDefault, std::nullopt, 0x00004000, 0x03eb0000},
  };

  for (const auto& tt : testcases) {
    SetOptval(&svc->sockopt, tt.initial_fwmark);
    EXPECT_TRUE(svc->TagSocket(4, tt.network_id, tt.policy, tt.annotation_id));
    EXPECT_EQ(4, svc->sockopt.sockfd);
    EXPECT_EQ(SOL_SOCKET, svc->sockopt.level);
    EXPECT_EQ(SO_MARK, svc->sockopt.optname);
    EXPECT_EQ(hex(tt.expected_fwmark), hex(GetOptval(svc->sockopt)));
  }

  // ROUTE_ON_VPN should not be set with network_id at the same time.
  EXPECT_FALSE(
      svc->TagSocket(4, /*network_id=*/123, Policy::kRouteOnVPN, std::nullopt));

  // getsockopt() returns failure.
  svc->getsockopt_ret = -1;
  svc->setsockopt_ret = 0;
  EXPECT_FALSE(
      svc->TagSocket(4, std::nullopt, Policy::kRouteOnVPN, std::nullopt));

  // setsockopt() returns failure.
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = -1;
  EXPECT_FALSE(
      svc->TagSocket(4, std::nullopt, Policy::kRouteOnVPN, std::nullopt));
}

TEST_F(RoutingServiceTest, SetFwmark) {
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;

  struct {
    uint32_t initial_fwmark;
    uint32_t fwmark_value;
    uint32_t fwmark_mask;
    uint32_t expected_fwmark;
  } testcases[] = {
      {0x0, 0x0, 0x0, 0x0},
      {0x1, 0x0, 0x0, 0x1},
      {0x1, 0x0, 0x1, 0x0},
      {0xaabbccdd, 0x11223344, 0xf0f0f0f0, 0x1a2b3c4d},
      {0xaabbccdd, 0x11223344, 0xffff0000, 0x1122ccdd},
      {0xaabbccdd, 0x11223344, 0x0000ffff, 0xaabb3344},
  };

  for (const auto& tt : testcases) {
    SetOptval(&svc->sockopt, tt.initial_fwmark);
    EXPECT_TRUE(
        svc->SetFwmark(4, fwmark(tt.fwmark_value), fwmark(tt.fwmark_mask)));
    EXPECT_EQ(4, svc->sockopt.sockfd);
    EXPECT_EQ(SOL_SOCKET, svc->sockopt.level);
    EXPECT_EQ(SO_MARK, svc->sockopt.optname);
    EXPECT_EQ(hex(tt.expected_fwmark), hex(GetOptval(svc->sockopt)));
  }
}

TEST_F(RoutingServiceTest, SetFwmark_Failures) {
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  svc->getsockopt_ret = -1;
  svc->setsockopt_ret = 0;
  EXPECT_FALSE(svc->SetFwmark(4, fwmark(0x1), fwmark(0x01)));

  svc = std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = -1;
  EXPECT_FALSE(svc->SetFwmark(5, fwmark(0x1), fwmark(0x01)));

  svc = std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  svc->getsockopt_ret = 0;
  svc->setsockopt_ret = 0;
  EXPECT_TRUE(svc->SetFwmark(6, fwmark(0x1), fwmark(0x01)));
}

TEST_F(RoutingServiceTest, LocalSourceSpecsPrettyPrinting) {
  struct {
    LocalSourceSpecs source;
    std::string expected_output;
  } testcases[] = {
      {{}, "{source: UNKNOWN, uid: , classid: 0, is_on_vpn: false}"},
      {{TrafficSource::kChrome, kUidChronos, 0, true},
       "{source: CHROME, uid: chronos, classid: 0, is_on_vpn: true}"},
      {{TrafficSource::kUser, kUidDebugd, 0, true},
       "{source: USER, uid: debugd, classid: 0, is_on_vpn: true}"},
      {{TrafficSource::kSystem, kUidTlsdate, 0, true},
       "{source: SYSTEM, uid: tlsdate, classid: 0, is_on_vpn: true}"},
      {{TrafficSource::kUser, kUidPluginvm, 0, true},
       "{source: USER, uid: pluginvm, classid: 0, is_on_vpn: true}"},
      {{TrafficSource::kUpdateEngine, "", 1234, false},
       "{source: UPDATE_ENGINE, uid: , classid: 1234, is_on_vpn: false}"},
  };

  for (const auto& tt : testcases) {
    std::ostringstream stream;
    stream << tt.source;
    EXPECT_EQ(tt.expected_output, stream.str());
  }
}

TEST_F(RoutingServiceTest, AllocateNetworkIDs) {
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  std::set<int> net_ids;
  for (int i = 0; i < 100; i++) {
    int id = svc->AllocateNetworkID();
    ASSERT_FALSE(net_ids.contains(id));
    net_ids.insert(id);
  }
}

TEST_F(RoutingServiceTest, AssignInterfaceToNetwork) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(12));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  int network1 = svc->AllocateNetworkID();

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));
}

TEST_F(RoutingServiceTest, AssignInterfaceToMultipleNetworks) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(12));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  int network1 = svc->AllocateNetworkID();
  int network2 = svc->AllocateNetworkID();

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_FALSE(
      svc->AssignInterfaceToNetwork(network2, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));
  ASSERT_EQ(nullptr, svc->GetInterface(network2));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network2));
}

TEST_F(RoutingServiceTest, AssignMultipleInterfacesToNetwork) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(12));
  ON_CALL(system_, IfNametoindex("eth0")).WillByDefault(Return(13));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  int network1 = svc->AllocateNetworkID();

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_FALSE(
      svc->AssignInterfaceToNetwork(network1, "eth0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));
  ASSERT_EQ(std::nullopt, svc->GetNetworkID("eth0"));
}

TEST_F(RoutingServiceTest, ReassignDifferentInterfacesToNetwork) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(12));
  ON_CALL(system_, IfNametoindex("eth0")).WillByDefault(Return(13));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  int network1 = svc->AllocateNetworkID();

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));

  svc->ForgetNetworkID(network1);
  ASSERT_EQ(nullptr, svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetNetworkID("wlan0"));

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "eth0", MakeTestSocket()));
  ASSERT_EQ("eth0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1013}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(std::nullopt, svc->GetNetworkID("wlan0"));
  ASSERT_EQ(1, svc->GetNetworkID("eth0"));
}

TEST_F(RoutingServiceTest, ReassignInterfaceToDifferentNetworks) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(12));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);

  int network1 = svc->AllocateNetworkID();
  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));

  svc->ForgetNetworkID(network1);
  ASSERT_EQ(nullptr, svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(std::nullopt, svc->GetNetworkID("wlan0"));

  int network2 = svc->AllocateNetworkID();
  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network2, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network2));
  ASSERT_EQ(Fwmark{.rt_table_id = 1012}, svc->GetRoutingFwmark(network2));
  ASSERT_EQ(network2, svc->GetNetworkID("wlan0"));
  ASSERT_EQ(nullptr, svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network1));
}

TEST_F(RoutingServiceTest, AssignUnknownInterfaceToNetwork) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(-1));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);
  int network1 = svc->AllocateNetworkID();

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));
}

TEST_F(RoutingServiceTest, NetworkAssignmentAutomaticCleanup) {
  ON_CALL(system_, IfNametoindex("wlan0")).WillByDefault(Return(-1));
  auto svc =
      std::make_unique<TestableRoutingService>(&system_, &lifeline_fd_svc_);

  int network1 = svc->AllocateNetworkID();
  base::OnceClosure on_lifeline_fd_event;
  EXPECT_CALL(lifeline_fd_svc_, AddLifelineFD)
      .WillOnce(WithArgs<1>([&](base::OnceClosure cb) {
        on_lifeline_fd_event = std::move(cb);
        return base::ScopedClosureRunner(base::DoNothing());
      }));

  ASSERT_TRUE(
      svc->AssignInterfaceToNetwork(network1, "wlan0", MakeTestSocket()));
  ASSERT_EQ("wlan0", *svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(network1, svc->GetNetworkID("wlan0"));

  std::move(on_lifeline_fd_event).Run();
  ASSERT_EQ(nullptr, svc->GetInterface(network1));
  ASSERT_EQ(std::nullopt, svc->GetRoutingFwmark(network1));
  ASSERT_EQ(std::nullopt, svc->GetNetworkID("wlan0"));
  ASSERT_EQ(0, svc->GetNetworkIDs().size());
}

}  // namespace patchpanel
