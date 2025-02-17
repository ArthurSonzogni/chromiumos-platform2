// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/cros_safety/safety_service_manager_impl.h"

#include <unistd.h>

#include <memory>
#include <utility>

#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/service_constants.h>
#include <mojo_service_manager/fake/simple_fake_service_manager.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "odml/cros_safety/metrics.h"
#include "odml/cros_safety/mock_cloud_safety_session.h"
#include "odml/cros_safety/mock_on_device_safety_session.h"
#include "odml/cros_safety/safety_service_manager.h"
#include "odml/mojom/big_buffer.mojom.h"
#include "odml/mojom/cros_safety.mojom-shared.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/mojom/cros_safety_service.mojom-shared.h"

namespace cros_safety {

namespace {

using testing::_;
using testing::NiceMock;

using ClassifySafetyCallback = SafetyServiceManager::ClassifySafetyCallback;
using base::test::RunOnceCallback;
using mojo_base::mojom::BigBuffer;
using mojo_base::mojom::BigBufferPtr;
using mojom::GetCloudSafetySessionResult;
using mojom::GetOnDeviceSafetySessionResult;
using mojom::SafetyClassifierVerdict;
using mojom::SafetyRuleset;

constexpr uint32_t kSafetyServiceUid = 123;

class FakeCrosSafetyService
    : public cros_safety::mojom::CrosSafetyService,
      public chromeos::mojo_service_manager::mojom::ServiceProvider {
 public:
  explicit FakeCrosSafetyService(
      mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
          service_manager) {
    service_manager->Register(
        /*service_name=*/chromeos::mojo_services::kCrosSafetyService,
        provider_receiver_.BindNewPipeAndPassRemote());
  }

  NiceMock<cros_safety::MockCloudSafetySession>& cloud_safety_session() {
    return cloud_safety_session_;
  }

  NiceMock<cros_safety::MockOnDeviceSafetySession>& on_device_safety_session() {
    return on_device_safety_session_;
  }

  void CreateOnDeviceSafetySession(
      mojo::PendingReceiver<cros_safety::mojom::OnDeviceSafetySession> session,
      CreateOnDeviceSafetySessionCallback callback) override {
    on_device_safety_session_.AddReceiver(std::move(session));
    std::move(callback).Run(
        cros_safety::mojom::GetOnDeviceSafetySessionResult::kOk);
  }

  void CreateCloudSafetySession(
      mojo::PendingReceiver<cros_safety::mojom::CloudSafetySession> session,
      CreateCloudSafetySessionCallback callback) override {
    cloud_safety_session_.AddReceiver(std::move(session));
    std::move(callback).Run(
        cros_safety::mojom::GetCloudSafetySessionResult::kOk);
  }

  void ClearReceivers() { receiver_set_.Clear(); }

 private:
  // overrides ServiceProvider.
  void Request(
      chromeos::mojo_service_manager::mojom::ProcessIdentityPtr identity,
      mojo::ScopedMessagePipeHandle receiver) override {
    receiver_set_.Add(
        this,
        mojo::PendingReceiver<cros_safety::mojom::CrosSafetyService>(
            std::move(receiver)),
        base::SequencedTaskRunner::GetCurrentDefault());
  }

  mojo::Receiver<chromeos::mojo_service_manager::mojom::ServiceProvider>
      provider_receiver_{this};
  mojo::ReceiverSet<mojom::CrosSafetyService> receiver_set_;
  NiceMock<cros_safety::MockCloudSafetySession> cloud_safety_session_;
  NiceMock<cros_safety::MockOnDeviceSafetySession> on_device_safety_session_;
};

class SafetyServiceManagerImplTest : public testing::Test {
 public:
  SafetyServiceManagerImplTest() {
    mojo::core::Init();
    mojo_service_manager_ = std::make_unique<
        chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>();
    remote_service_manager_.Bind(
        mojo_service_manager_->AddNewPipeAndPassRemote(kSafetyServiceUid));

    fake_safety_service_ =
        std::make_unique<FakeCrosSafetyService>(remote_service_manager_);

    safety_service_manager_ = std::make_unique<SafetyServiceManagerImpl>(
        remote_service_manager_, raw_ref(metrics_));
  }

 protected:
  void ExpectSendGetCloudSafetySessionOk(int times) {
    EXPECT_CALL(
        metrics_,
        SendEnumToUMA(metrics::kGetCloudSafetySession,
                      static_cast<int>(GetCloudSafetySessionResult::kOk), _))
        .Times(times);
  }
  void ExpectSendGetOnDeviceSafetySessionOk(int times) {
    EXPECT_CALL(
        metrics_,
        SendEnumToUMA(metrics::kGetOnDeviceSafetySession,
                      static_cast<int>(GetOnDeviceSafetySessionResult::kOk), _))
        .Times(times);
  }
  void ExpectSendGroupLatency(int times) {
    EXPECT_CALL(metrics_, SendTimeToUMA(metrics::kClassifySafetyLatencyPrefix +
                                            metrics::kMapRulesetToString.at(
                                                SafetyRuleset::kGeneric),
                                        _, _, _, _))
        .Times(times);
  }

  void ExpectSendGroupVerdict(SafetyClassifierVerdict verdict) {
    EXPECT_CALL(metrics_, SendEnumToUMA(metrics::kClassifySafetyResultPrefix +
                                            metrics::kMapRulesetToString.at(
                                                SafetyRuleset::kGeneric),
                                        static_cast<int>(verdict), _));
  }

  base::test::TaskEnvironment task_environment_;
  std::unique_ptr<chromeos::mojo_service_manager::SimpleFakeMojoServiceManager>
      mojo_service_manager_;
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      remote_service_manager_;

  std::unique_ptr<FakeCrosSafetyService> fake_safety_service_;
  std::unique_ptr<cros_safety::SafetyServiceManagerImpl>
      safety_service_manager_;

 private:
  NiceMock<MetricsLibraryMock> metrics_;
};

TEST_F(SafetyServiceManagerImplTest, ClassifyTextSafetyPass) {
  ExpectSendGetOnDeviceSafetySessionOk(1);
  ExpectSendGroupLatency(1);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kPass);
  EXPECT_CALL(fake_safety_service_->on_device_safety_session(),
              ClassifyTextSafety)
      .WillOnce(RunOnceCallback<2>(SafetyClassifierVerdict::kPass));
  base::RunLoop run_loop;
  safety_service_manager_->ClassifyTextSafety(
      SafetyRuleset::kGeneric, "test",
      base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
        EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
        run_loop.Quit();
      }));
  run_loop.Run();
}

TEST_F(SafetyServiceManagerImplTest, ClassifyImageSafetyPass) {
  ExpectSendGetCloudSafetySessionOk(1);
  ExpectSendGroupLatency(1);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kPass);
  EXPECT_CALL(fake_safety_service_->cloud_safety_session(), ClassifyImageSafety)
      .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kPass));
  base::RunLoop run_loop;
  safety_service_manager_->ClassifyImageSafety(
      SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
      base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
        EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
        run_loop.Quit();
      }));

  run_loop.Run();
}

TEST_F(SafetyServiceManagerImplTest, SafetyServiceDisconnect) {
  ExpectSendGetCloudSafetySessionOk(3);
  ExpectSendGroupLatency(2);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kPass);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kFailedImage);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kServiceNotAvailable);
  EXPECT_CALL(fake_safety_service_->cloud_safety_session(), ClassifyImageSafety)
      .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kPass))
      .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kFailedImage))
      .WillOnce(
          RunOnceCallback<3>(SafetyClassifierVerdict::kServiceNotAvailable));

  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  fake_safety_service_->ClearReceivers();
  task_environment_.RunUntilIdle();
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedImage);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  fake_safety_service_->ClearReceivers();
  task_environment_.RunUntilIdle();
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kServiceNotAvailable);
          run_loop.Quit();
        }));
    run_loop.Run();
  }
}

TEST_F(SafetyServiceManagerImplTest, CloudSafetySessionDisconnected) {
  ExpectSendGetCloudSafetySessionOk(2);
  ExpectSendGroupLatency(2);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kPass);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kFailedImage);
  EXPECT_CALL(fake_safety_service_->cloud_safety_session(), ClassifyImageSafety)
      .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kPass))
      .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kFailedImage));
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  // Disconnect from the server side and wait for the disconnect handler be
  // triggered.
  fake_safety_service_->cloud_safety_session().ClearReceivers();
  task_environment_.RunUntilIdle();

  // The manager should be able to reconnect session and work as intended.
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedImage);
          run_loop.Quit();
        }));
    run_loop.Run();
  }
}

TEST_F(SafetyServiceManagerImplTest, OnDeviceSafetySessionDisconnected) {
  ExpectSendGetOnDeviceSafetySessionOk(2);
  ExpectSendGroupLatency(2);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kPass);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kFailedText);
  EXPECT_CALL(fake_safety_service_->on_device_safety_session(),
              ClassifyTextSafety)
      .WillOnce(RunOnceCallback<2>(SafetyClassifierVerdict::kPass))
      .WillOnce(RunOnceCallback<2>(SafetyClassifierVerdict::kFailedText));
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyTextSafety(
        SafetyRuleset::kGeneric, "test",
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  // Disconnect from the server side and wait for the disconnect handler be
  // triggered.
  fake_safety_service_->on_device_safety_session().ClearReceivers();
  task_environment_.RunUntilIdle();

  // The manager should be able to reconnect session and work as intended.
  {
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyTextSafety(
        SafetyRuleset::kGeneric, "test",
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedText);
          run_loop.Quit();
        }));
    run_loop.Run();
  }
}

TEST_F(SafetyServiceManagerImplTest, ClassifyImageSafetyCallbackNotRun) {
  ExpectSendGetCloudSafetySessionOk(2);
  ExpectSendGroupLatency(1);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kServiceNotAvailable);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kFailedImage);
  // Drop the callback and don't run it.
  {
    SafetyServiceManager::ClassifySafetyCallback callback;
    base::RunLoop run_loop;
    EXPECT_CALL(fake_safety_service_->cloud_safety_session(),
                ClassifyImageSafety)
        .WillOnce([&](SafetyRuleset ruleset,
                      const std::optional<std::string>& text,
                      BigBufferPtr image,
                      SafetyServiceManager::ClassifySafetyCallback cb) {
          callback = std::move(cb);
          run_loop.Quit();
        });
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          // This is the default return value passed into
          // WrapCallbackWithDefaultInvokeIfNotRun.
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kServiceNotAvailable);
        }));
    run_loop.Run();
    fake_safety_service_->cloud_safety_session().ClearReceivers();
    task_environment_.RunUntilIdle();
  }

  // Following calls should work as intended.
  {
    EXPECT_CALL(fake_safety_service_->cloud_safety_session(),
                ClassifyImageSafety)
        .WillOnce(RunOnceCallback<3>(SafetyClassifierVerdict::kFailedImage));
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyImageSafety(
        SafetyRuleset::kGeneric, "test", BigBuffer::NewInvalidBuffer(false),
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedImage);
          run_loop.Quit();
        }));

    run_loop.Run();
  }
}

TEST_F(SafetyServiceManagerImplTest, ClassifyTextSafetyCallbackNotRun) {
  ExpectSendGetOnDeviceSafetySessionOk(2);
  ExpectSendGroupLatency(1);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kServiceNotAvailable);
  ExpectSendGroupVerdict(SafetyClassifierVerdict::kFailedText);
  // Drop the callback and don't run it.
  {
    SafetyServiceManager::ClassifySafetyCallback callback;
    base::RunLoop run_loop;
    EXPECT_CALL(fake_safety_service_->on_device_safety_session(),
                ClassifyTextSafety)
        .WillOnce([&](SafetyRuleset ruleset, const std::string& text,
                      SafetyServiceManager::ClassifySafetyCallback cb) {
          callback = std::move(cb);
          run_loop.Quit();
        });
    safety_service_manager_->ClassifyTextSafety(
        SafetyRuleset::kGeneric, "test",
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          // This is the default return value passed into
          // WrapCallbackWithDefaultInvokeIfNotRun.
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kServiceNotAvailable);
        }));
    run_loop.Run();
    fake_safety_service_->on_device_safety_session().ClearReceivers();
    task_environment_.RunUntilIdle();
  }

  // Following calls should work as intended.
  {
    EXPECT_CALL(fake_safety_service_->on_device_safety_session(),
                ClassifyTextSafety)
        .WillOnce(RunOnceCallback<2>(SafetyClassifierVerdict::kFailedText));
    base::RunLoop run_loop;
    safety_service_manager_->ClassifyTextSafety(
        SafetyRuleset::kGeneric, "test",
        base::BindLambdaForTesting([&](SafetyClassifierVerdict verdict) {
          EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedText);
          run_loop.Quit();
        }));
    run_loop.Run();
  }
}

}  // namespace
}  // namespace cros_safety
