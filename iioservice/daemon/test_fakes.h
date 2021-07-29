// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_TEST_FAKES_H_
#define IIOSERVICE_DAEMON_TEST_FAKES_H_

#include <memory>
#include <set>
#include <utility>

#include <base/callback.h>
#include <base/sequence_checker.h>
#include <base/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include <libmems/iio_device.h>
#include <libmems/test_fakes.h>

#include "iioservice/daemon/samples_handler.h"
#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

namespace fakes {

constexpr char kAccelDeviceName[] = "FakeAccelDevice";
constexpr int kAccelDeviceId = 1;

constexpr char kFakeSamplingFrequencyAvailable[] =
    "0.000000 5.000000 40.000000";

constexpr int kPauseIndex = 50;

class FakeSamplesHandler : public SamplesHandler {
 public:
  using ScopedFakeSamplesHandler =
      std::unique_ptr<FakeSamplesHandler, decltype(&SamplesHandlerDeleter)>;

  static ScopedFakeSamplesHandler Create(
      scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      libmems::fakes::FakeIioDevice* fake_iio_device);

  void ResumeReading();
  void CheckRequestedFrequency(double max_freq);

 private:
  FakeSamplesHandler(
      scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      libmems::fakes::FakeIioDevice* fake_iio_device,
      double min_freq,
      double max_freq);

  void ResumeReadingOnThread();
  void CheckRequestedFrequencyOnThread(double max_freq);

  libmems::fakes::FakeIioDevice* fake_iio_device_;

  base::WeakPtrFactory<FakeSamplesHandler> weak_factory_{this};
};

// An observer that does nothing to samples or errors. Instead, it simply waits
// for the mojo disconnection and calls |quit_closure|.
class FakeObserver : cros::mojom::SensorDeviceSamplesObserver {
 public:
  explicit FakeObserver(base::RepeatingClosure quit_closure)
      : quit_closure_(std::move(quit_closure)) {}

  mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> GetRemote() {
    CHECK(!receiver_.is_bound());
    auto pending_remote = receiver_.BindNewPipeAndPassRemote();
    receiver_.set_disconnect_handler(base::BindOnce(
        &FakeObserver::OnObserverDisconnect, base::Unretained(this)));
    return pending_remote;
  }

  // cros::mojom::SensorDeviceSamplesObserver overrides:
  void OnSampleUpdated(const libmems::IioDevice::IioSample& sample) override {}
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override {}

 private:
  void OnObserverDisconnect() {
    receiver_.reset();
    quit_closure_.Run();
  }

  base::RepeatingClosure quit_closure_;
  mojo::Receiver<cros::mojom::SensorDeviceSamplesObserver> receiver_{this};
};

class FakeSamplesObserver : public cros::mojom::SensorDeviceSamplesObserver {
 public:
  static std::unique_ptr<FakeSamplesObserver> Create(
      libmems::IioDevice* device,
      std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures,
      double frequency,
      double frequency2,
      double dev_frequency,
      double dev_frequency2,
      int pause_index = kPauseIndex);

  ~FakeSamplesObserver() override;

  // cros::mojom::SensorDeviceSamplesObserver overrides:
  void OnSampleUpdated(const libmems::IioDevice::IioSample& sample) override;
  void OnErrorOccurred(cros::mojom::ObserverErrorType type) override;

  mojo::PendingRemote<cros::mojom::SensorDeviceSamplesObserver> GetRemote();
  bool is_bound() const;

  bool FinishedObserving() const;
  bool NoRemainingFailures() const;

  int GetSampleIndex() const;
  const libmems::IioDevice::IioSample& GetLatestSample() const;

 private:
  FakeSamplesObserver(
      libmems::IioDevice* device,
      std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures,
      double frequency,
      double frequency2,
      double dev_frequency,
      double dev_frequency2,
      int pause_index = kPauseIndex);

  void OnObserverDisconnect();

  int GetStep() const;

  libmems::IioDevice* device_;

  std::multiset<std::pair<int, cros::mojom::ObserverErrorType>> failures_;

  double frequency_;
  double frequency2_;
  double dev_frequency_;
  double dev_frequency2_;
  int pause_index_;

  int sample_index_ = 0;
  // Latest sample.
  libmems::IioDevice::IioSample sample_;

  mojo::Receiver<cros::mojom::SensorDeviceSamplesObserver> receiver_;

  SEQUENCE_CHECKER(sequence_checker_);

  base::WeakPtrFactory<FakeSamplesObserver> weak_factory_{this};
};

}  // namespace fakes

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_TEST_FAKES_H_
