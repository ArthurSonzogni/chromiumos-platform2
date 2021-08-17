// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include <set>
#include <utility>

#include <base/run_loop.h>
#include <base/test/task_environment.h>
#include <libmems/common_types.h>
#include <libmems/test_fakes.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/daemon/sensor_device_impl.h"
#include "iioservice/daemon/sensor_metrics_mock.h"
#include "iioservice/daemon/test_fakes.h"
#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

namespace {

constexpr char kDeviceAttrName[] = "FakeDeviceAttr";
constexpr char kDeviceAttrValue[] = "FakeDeviceAttrValue\0\n\0";
constexpr char kParsedDeviceAttrValue[] = "FakeDeviceAttrValue";

constexpr char kChnAttrName[] = "FakeChnAttr";
constexpr char kChnAttrValue[] = "FakeChnValue\n\0\n";
constexpr char kParsedChnAttrValue[] = "FakeChnValue";

constexpr char kDummyChnAttrName1[] = "DummyChnAttr1";
constexpr char kDummyChnAttrName2[] = "DummyChnAttr2";

class SensorDeviceImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SensorMetricsMock::InitializeForTesting();

    ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
        task_environment_.GetMainThreadTaskRunner(),
        mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);

    context_ = std::make_unique<libmems::fakes::FakeIioContext>();
    EXPECT_FALSE(context_->IsValid());

    // Initialize with an invalid context.
    sensor_device_ = SensorDeviceImpl::Create(
        task_environment_.GetMainThreadTaskRunner(), context_.get());
    EXPECT_TRUE(sensor_device_);

    // Tried to add an invalid device with an invalid context.
    sensor_device_->AddReceiver(
        fakes::kAccelDeviceId, remote_.BindNewPipeAndPassReceiver(),
        std::set<cros::mojom::DeviceType>{cros::mojom::DeviceType::ACCEL});
    remote_.set_disconnect_handler(
        base::BindOnce(&SensorDeviceImplTest::OnSensorDeviceDisconnect,
                       base::Unretained(this)));
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(remote_.is_bound());

    auto device = std::make_unique<libmems::fakes::FakeIioDevice>(
        nullptr, fakes::kAccelDeviceName, fakes::kAccelDeviceId);
    EXPECT_TRUE(
        device->WriteStringAttribute(libmems::kSamplingFrequencyAvailable,
                                     fakes::kFakeSamplingFrequencyAvailable));
    EXPECT_TRUE(device->WriteDoubleAttribute(libmems::kHWFifoTimeoutAttr, 0.0));
    EXPECT_TRUE(
        device->WriteStringAttribute(kDeviceAttrName, kDeviceAttrValue));

    for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i) {
      auto chn = std::make_unique<libmems::fakes::FakeIioChannel>(
          libmems::fakes::kFakeAccelChns[i], true);
      if (i % 2 == 0)
        chn->WriteStringAttribute(kChnAttrName, kChnAttrValue);

      device->AddChannel(std::move(chn));
    }

    device_ = device.get();
    context_->AddDevice(std::move(device));

    sensor_device_->AddReceiver(
        fakes::kAccelDeviceId, remote_.BindNewPipeAndPassReceiver(),
        std::set<cros::mojom::DeviceType>{cros::mojom::DeviceType::ACCEL});
  }

  void TearDown() override {
    sensor_device_.reset();
    SensorMetrics::Shutdown();
  }

  void OnSensorDeviceDisconnect() { remote_.reset(); }

  std::unique_ptr<libmems::fakes::FakeIioContext> context_;
  libmems::fakes::FakeIioDevice* device_;

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME,
      base::test::TaskEnvironment::MainThreadType::IO};

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device_ = {
      nullptr, SensorDeviceImpl::SensorDeviceImplDeleter};

  mojo::Remote<cros::mojom::SensorDevice> remote_;
};

TEST_F(SensorDeviceImplTest, CheckWeakPtrs) {
  remote_.reset();
  base::RunLoop().RunUntilIdle();
}

TEST_F(SensorDeviceImplTest, SetTimeout) {
  remote_->SetTimeout(0);
}

TEST_F(SensorDeviceImplTest, GetAttributes) {
  base::RunLoop loop;
  remote_->GetAttributes(
      std::vector<std::string>{kDummyChnAttrName1, kDeviceAttrName,
                               cros::mojom::kDeviceName, kDummyChnAttrName2},
      base::BindOnce(
          [](base::Closure closure,
             const std::vector<base::Optional<std::string>>& values) {
            EXPECT_EQ(values.size(), 4u);
            EXPECT_FALSE(values.front().has_value());
            EXPECT_FALSE(values.back().has_value());
            EXPECT_TRUE(values[1].has_value());
            EXPECT_EQ(values[1].value().compare(kParsedDeviceAttrValue), 0);
            EXPECT_TRUE(values[2].has_value());
            EXPECT_EQ(values[2].value().compare(fakes::kAccelDeviceName), 0);
            closure.Run();
          },
          loop.QuitClosure()));
  loop.Run();
}

TEST_F(SensorDeviceImplTest, SetFrequency) {
  base::RunLoop loop;
  remote_->SetFrequency(libmems::fakes::kFakeSamplingFrequency,
                        base::BindOnce(
                            [](base::Closure closure, double result_freq) {
                              EXPECT_EQ(result_freq,
                                        libmems::fakes::kFakeSamplingFrequency);
                              closure.Run();
                            },
                            loop.QuitClosure()));
  loop.Run();
}

TEST_F(SensorDeviceImplTest, StartAndStopReadingSamples) {
  // No samples in this test, as the sample index may not match due to the
  // potential missed samples between reconnections.
  device_->SetPauseCallbackAtKthSamples(0, base::BindOnce([]() {}));

  double frequency = libmems::fakes::kFakeSamplingFrequency;
  remote_->SetTimeout(0);
  remote_->SetFrequency(frequency, base::BindOnce([](double result_freq) {
                          EXPECT_EQ(result_freq,
                                    libmems::fakes::kFakeSamplingFrequency);
                        }));

  remote_->SetChannelsEnabled(
      std::vector<int32_t>{0, 2, 3}, true,
      base::BindOnce([](const std::vector<int32_t>& failed_indices) {
        EXPECT_TRUE(failed_indices.empty());
      }));

  // No pause: setting pause_index_ to the size of fake data.
  auto fake_observer = fakes::FakeSamplesObserver::Create(
      device_, std::multiset<std::pair<int, cros::mojom::ObserverErrorType>>(),
      frequency, frequency, frequency, frequency,
      base::size(libmems::fakes::kFakeAccelSamples));

  mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> pending_remote;
  auto pending_receiver = pending_remote.InitWithNewPipeAndPassReceiver();
  // Check SensorDevice::StopReadingSamples works.
  remote_->StartReadingSamples(std::move(pending_remote));
  remote_->StopReadingSamples();
  pending_receiver.reset();

  remote_->StartReadingSamples(fake_observer->GetRemote());

  // Wait to make sure fake_observer is not disconnected.
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(fake_observer->is_bound());

  remote_->StopReadingSamples();

  // StopReadingSamples can be called multiple times.
  remote_->StopReadingSamples();
}

TEST_F(SensorDeviceImplTest, SetChannels) {
  remote_->GetAllChannelIds(
      base::BindOnce([](const std::vector<std::string>& chn_ids) {
        EXPECT_EQ(chn_ids.size(), base::size(libmems::fakes::kFakeAccelChns));
        for (int i = 0; i < chn_ids.size(); ++i)
          EXPECT_EQ(chn_ids[i], libmems::fakes::kFakeAccelChns[i]);
      }));

  std::vector<int32_t> indices = {0, 2};
  remote_->SetChannelsEnabled(
      indices, true,
      base::BindOnce([](const std::vector<int32_t>& failed_indices) {
        EXPECT_TRUE(failed_indices.empty());
      }));

  indices.clear();
  for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i)
    indices.push_back(i);

  base::RunLoop loop;
  remote_->GetChannelsEnabled(
      indices, base::BindOnce(
                   [](base::Closure closure, const std::vector<bool>& enabled) {
                     EXPECT_EQ(enabled.size(),
                               base::size(libmems::fakes::kFakeAccelChns));
                     for (int i = 0; i < enabled.size(); ++i)
                       EXPECT_EQ(enabled[i], i % 2 == 0);

                     closure.Run();
                   },
                   loop.QuitClosure()));
  loop.Run();
}

TEST_F(SensorDeviceImplTest, GetChannelsAttributes) {
  std::vector<int32_t> indices;
  for (int i = 0; i < base::size(libmems::fakes::kFakeAccelChns); ++i)
    indices.push_back(i);

  base::RunLoop loop;
  remote_->GetChannelsAttributes(
      indices, kChnAttrName,
      base::BindOnce(
          [](base::Closure closure,
             const std::vector<base::Optional<std::string>>& values) {
            EXPECT_EQ(values.size(),
                      base::size(libmems::fakes::kFakeAccelChns));
            for (int i = 0; i < values.size(); ++i) {
              if (i % 2 == 0) {
                EXPECT_TRUE(values[i].has_value());
                EXPECT_EQ(values[i].value().compare(kParsedChnAttrValue), 0);
              }
            }
            closure.Run();
          },
          loop.QuitClosure()));
  loop.Run();
}

}  // namespace

}  // namespace iioservice
