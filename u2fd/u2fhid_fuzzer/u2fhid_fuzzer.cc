// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/daemons/daemon.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <sysexits.h>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "u2fd/hid_interface.h"
#include "u2fd/u2fhid.h"
#include "u2fd/u2fhid_fuzzer/fake_u2f_msg_handler.h"
#include "u2fd/u2fhid_fuzzer/fake_uhid_device.h"

namespace {
class FuzzerLoop : public brillo::Daemon {
 public:
  FuzzerLoop(const uint8_t* data, size_t size) : data_provider_(data, size) {}
  FuzzerLoop(const FuzzerLoop&) = delete;
  FuzzerLoop& operator=(const FuzzerLoop&) = delete;

  ~FuzzerLoop() override = default;

 protected:
  int OnInit() override {
    int exit_code = brillo::Daemon::OnInit();
    if (exit_code != EX_OK) {
      return exit_code;
    }

    fake_u2f_msg_handler_ = std::make_unique<u2f::FakeU2fMessageHandler>();
    auto fake_uhid_device = std::make_unique<u2f::FakeUHidDevice>();
    fake_uhid_device_ = fake_uhid_device.get();

    u2fhid_ = std::make_unique<u2f::U2fHid>(std::move(fake_uhid_device),
                                            fake_u2f_msg_handler_.get());

    ScheduleSendOutputReport();
    return EX_OK;
  }

 private:
  void ScheduleSendOutputReport() {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&FuzzerLoop::SendOutputReport, base::Unretained(this)));
  }

  void SendOutputReport() {
    if (data_provider_.remaining_bytes() == 0) {
      Quit();
      return;
    }

    // Sending the output report to U2fHid::ProcessReport
    fake_uhid_device_->SendOutputReport(
        data_provider_.ConsumeRandomLengthString());

    ScheduleSendOutputReport();
  }

  FuzzedDataProvider data_provider_;

  u2f::FakeUHidDevice* fake_uhid_device_;
  std::unique_ptr<u2f::FakeU2fMessageHandler> fake_u2f_msg_handler_;
  std::unique_ptr<u2f::U2fHid> u2fhid_;
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzerLoop loop(data, size);
  CHECK_EQ(loop.Run(), EX_OK);
  return 0;
}
