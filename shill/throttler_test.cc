// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/throttler.h"

#include <memory>
#include <utility>

#include <base/functional/bind.h>
#include <base/test/task_environment.h>
#include <gmock/gmock.h>

#include "shill/mock_tc_process.h"

using testing::_;
using testing::Property;
using testing::WithArg;

namespace shill {

constexpr uint32_t kUploadThrottleRate = 100;
constexpr uint32_t kDownloadThrottleRate = 300;
const std::vector<std::string> kInterfaces = {"wlan0", "eth0"};
const char kNewAddedInterface[] = "ppp0";
const std::vector<std::string> kDisabledCommands = {
    "qdisc del dev wlan0 root\n",
    "qdisc del dev wlan0 ingress\n",
    "qdisc del dev eth0 root\n",
    "qdisc del dev eth0 ingress\n",
};
const std::vector<std::string> kEth0ThrottledCommands = {
    "qdisc del dev eth0 root\n",
    "qdisc del dev eth0 ingress\n",
    "qdisc add dev eth0 root handle 1: htb default 11\n",
    "class add dev eth0 parent 1: classid 1:1 htb rate 100kbit\n",
    "class add dev eth0 parent 1:1 classid 1:11 htb rate 100kbit prio 0 "
    "quantum 300\n",
    "qdisc add dev eth0 handle ffff: ingress\n",
    "filter add dev eth0 parent ffff: protocol all prio 50 u32 match ip src "
    "0.0.0.0/0 police rate 300kbit burst 600k mtu 66000 drop flowid :1\n",
};
const std::vector<std::string> kWlan0ThrottledCommands = {
    "qdisc del dev wlan0 root\n",
    "qdisc del dev wlan0 ingress\n",
    "qdisc add dev wlan0 root handle 1: htb default 11\n",
    "class add dev wlan0 parent 1: classid 1:1 htb rate 100kbit\n",
    "class add dev wlan0 parent 1:1 classid 1:11 htb rate 100kbit prio 0 "
    "quantum 300\n",
    "qdisc add dev wlan0 handle ffff: ingress\n",
    "filter add dev wlan0 parent ffff: protocol all prio 50 u32 match ip src "
    "0.0.0.0/0 police rate 300kbit burst 600k mtu 66000 drop flowid :1\n",
};
const std::vector<std::string> kPpp0ThrottledCommands = {
    "qdisc del dev ppp0 root\n",
    "qdisc del dev ppp0 ingress\n",
    "qdisc add dev ppp0 root handle 1: htb default 11\n",
    "class add dev ppp0 parent 1: classid 1:1 htb rate 100kbit\n",
    "class add dev ppp0 parent 1:1 classid 1:11 htb rate 100kbit prio 0 "
    "quantum 300\n",
    "qdisc add dev ppp0 handle ffff: ingress\n",
    "filter add dev ppp0 parent ffff: protocol all prio 50 u32 match ip src "
    "0.0.0.0/0 police rate 300kbit burst 600k mtu 66000 drop flowid :1\n",
};

class Client {
 public:
  ResultCallback GetCallback() {
    return base::BindOnce(&Client::OnThrottlerDone, base::Unretained(this));
  }

  MOCK_METHOD(void, OnThrottlerDone, (const Error&), ());
};

class ThrottlerTest : public testing::Test {
 protected:
  void SetUp() override {
    auto tc_process_factory = std::make_unique<MockTCProcessFactory>();
    tc_process_factory_ = tc_process_factory.get();

    throttler_ = std::make_unique<Throttler>(std::move(tc_process_factory));
  }

  // Expects to create a TCProcess with the commands. The exit callback will be
  // executed when task_environment_.RunUntilIdle().
  void ExpectCreateTCProcess(const std::vector<std::string>& commands) {
    EXPECT_CALL(*tc_process_factory_, Create(commands, _, _))
        .WillOnce(WithArg<1>([this](TCProcess::ExitCallback exit_callback) {
          task_environment_.GetMainThreadTaskRunner()->PostTask(
              FROM_HERE, base::BindOnce(std::move(exit_callback), 0));
          return std::make_unique<MockTCProcess>();
        }));
  }

  base::test::TaskEnvironment task_environment_;

  MockTCProcessFactory* tc_process_factory_;
  std::unique_ptr<Throttler> throttler_;
};

TEST_F(ThrottlerTest, ThrottleInterfaces) {
  ExpectCreateTCProcess(kEth0ThrottledCommands);
  ExpectCreateTCProcess(kWlan0ThrottledCommands);

  Client client;
  EXPECT_CALL(client, OnThrottlerDone(Property(&Error::type, Error::kSuccess)));

  throttler_->ThrottleInterfaces(client.GetCallback(), kUploadThrottleRate,
                                 kDownloadThrottleRate, kInterfaces);
  task_environment_.RunUntilIdle();

  // After successfully throttling interfaces, ApplyThrottleToNewInterface()
  // could throttle new interface with the same throttled rate.
  ExpectCreateTCProcess(kPpp0ThrottledCommands);
  EXPECT_TRUE(throttler_->ApplyThrottleToNewInterface(kNewAddedInterface));
  task_environment_.RunUntilIdle();
}

TEST_F(ThrottlerTest, ThrottleInterfacesWithoutBitrate) {
  Client client;
  EXPECT_CALL(client, OnThrottlerDone(
                          Property(&Error::type, Error::kInvalidArguments)));

  throttler_->ThrottleInterfaces(client.GetCallback(), 0, 0, kInterfaces);
}

TEST_F(ThrottlerTest, ApplyThrottleToNewInterface) {
  ExpectCreateTCProcess(kEth0ThrottledCommands);
  ExpectCreateTCProcess(kWlan0ThrottledCommands);
  ExpectCreateTCProcess(kPpp0ThrottledCommands);

  Client client;
  EXPECT_CALL(client, OnThrottlerDone(Property(&Error::type, Error::kSuccess)));

  throttler_->ThrottleInterfaces(client.GetCallback(), kUploadThrottleRate,
                                 kDownloadThrottleRate, kInterfaces);
  // Calling ApplyThrottleToNewInterface() before the previous
  // ThrottleInterfaces() has been finished (simulated by RunUntilIdle()) should
  // also work.
  EXPECT_TRUE(throttler_->ApplyThrottleToNewInterface(kNewAddedInterface));

  task_environment_.RunUntilIdle();
}

TEST_F(ThrottlerTest, ApplyThrottleToNewInterfaceWithoutThrottleRate) {
  EXPECT_FALSE(throttler_->ApplyThrottleToNewInterface(kNewAddedInterface));
}

TEST_F(ThrottlerTest, DisableThrottlingOnAllInterfaces) {
  ExpectCreateTCProcess(kDisabledCommands);

  Client client;
  EXPECT_CALL(client, OnThrottlerDone(Property(&Error::type, Error::kSuccess)));

  throttler_->DisableThrottlingOnAllInterfaces(client.GetCallback(),
                                               kInterfaces);
  task_environment_.RunUntilIdle();

  // ApplyThrottleToNewInterface() should fail after throttling is disabled.
  EXPECT_FALSE(throttler_->ApplyThrottleToNewInterface(kNewAddedInterface));
}

TEST_F(ThrottlerTest, DisableDuringThrottling) {
  // When DisableThrottlingOnAllInterfaces() is called before throttling the
  // first interface is finished, then:
  // - Throttler only creates a TC process to throttling the first interface,
  //   and another TC process to disable the throttling.
  // - The result of ThrottleInterfaces() will be kOperationAborted, and the
  //   result of DisableThrottlingOnAllInterfaces is kSuccess.
  ExpectCreateTCProcess(kEth0ThrottledCommands);
  ExpectCreateTCProcess(kDisabledCommands);

  Client client;
  EXPECT_CALL(client, OnThrottlerDone(
                          Property(&Error::type, Error::kOperationAborted)));
  EXPECT_CALL(client, OnThrottlerDone(Property(&Error::type, Error::kSuccess)));

  throttler_->ThrottleInterfaces(client.GetCallback(), kUploadThrottleRate,
                                 kDownloadThrottleRate, kInterfaces);
  throttler_->DisableThrottlingOnAllInterfaces(client.GetCallback(),
                                               kInterfaces);
  task_environment_.RunUntilIdle();
}

TEST_F(ThrottlerTest, DisableThrottlingOnEmptyInterfaces) {
  Client client;
  EXPECT_CALL(client, OnThrottlerDone(Property(&Error::type, Error::kSuccess)));

  throttler_->DisableThrottlingOnAllInterfaces(client.GetCallback(), {});
  task_environment_.RunUntilIdle();

  // ApplyThrottleToNewInterface() should fail after throttling is disabled.
  EXPECT_FALSE(throttler_->ApplyThrottleToNewInterface(kNewAddedInterface));
}

}  // namespace shill
