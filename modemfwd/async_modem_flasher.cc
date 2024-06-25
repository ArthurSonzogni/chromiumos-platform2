// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <base/functional/bind.h>

#include "modemfwd/async_modem_flasher.h"

namespace modemfwd {

AsyncModemFlasher::AsyncModemFlasher(std::unique_ptr<ModemFlasher> flasher)
    : thread_("async-flasher"), flasher_(std::move(flasher)) {
  CHECK(thread_.Start());
}

void AsyncModemFlasher::ShouldFlash(
    Modem* modem, base::OnceCallback<void(bool, brillo::ErrorPtr)> callback) {
  auto result = std::make_shared<std::pair<bool, brillo::ErrorPtr>>();
  thread_.task_runner()->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&AsyncModemFlasher::ShouldFlashOnThread, this, modem,
                     result),
      base::BindOnce(&AsyncModemFlasher::OnShouldFlashResult, this,
                     std::move(callback), result));
}

void AsyncModemFlasher::BuildFlashConfig(
    Modem* modem,
    std::optional<std::string> carrier_override_uuid,
    base::OnceCallback<void(std::unique_ptr<FlashConfig>, brillo::ErrorPtr)>
        callback) {
  auto result = std::make_shared<
      std::pair<std::unique_ptr<FlashConfig>, brillo::ErrorPtr>>();
  thread_.task_runner()->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&AsyncModemFlasher::BuildFlashConfigOnThread, this, modem,
                     carrier_override_uuid, result),
      base::BindOnce(&AsyncModemFlasher::OnBuildFlashConfigResult, this,
                     std::move(callback), result));
}

void AsyncModemFlasher::RunFlash(
    Modem* modem,
    std::unique_ptr<FlashConfig> flash_cfg,
    base::OnceCallback<void(bool, base::TimeDelta, brillo::ErrorPtr)>
        callback) {
  CHECK(flash_cfg);
  auto result =
      std::make_shared<std::tuple<bool, base::TimeDelta, brillo::ErrorPtr>>();
  thread_.task_runner()->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(&AsyncModemFlasher::RunFlashOnThread, this, modem,
                     std::move(flash_cfg), result),
      base::BindOnce(&AsyncModemFlasher::OnRunFlashResult, this,
                     std::move(callback), result));
}

void AsyncModemFlasher::ShouldFlashOnThread(
    Modem* modem, std::shared_ptr<std::pair<bool, brillo::ErrorPtr>> result) {
  CHECK(thread_.task_runner()->RunsTasksInCurrentSequence());
  result->first = flasher_->ShouldFlash(modem, &result->second);
}

void AsyncModemFlasher::OnShouldFlashResult(
    base::OnceCallback<void(bool, brillo::ErrorPtr)> callback,
    std::shared_ptr<std::pair<bool, brillo::ErrorPtr>> result) {
  CHECK(!thread_.task_runner()->RunsTasksInCurrentSequence());
  std::move(callback).Run(result->first, std::move(result->second));
}

void AsyncModemFlasher::BuildFlashConfigOnThread(
    Modem* modem,
    std::optional<std::string> carrier_override_uuid,
    std::shared_ptr<std::pair<std::unique_ptr<FlashConfig>, brillo::ErrorPtr>>
        result) {
  CHECK(thread_.task_runner()->RunsTasksInCurrentSequence());
  result->first =
      flasher_->BuildFlashConfig(modem, carrier_override_uuid, &result->second);
}

void AsyncModemFlasher::OnBuildFlashConfigResult(
    base::OnceCallback<void(std::unique_ptr<FlashConfig>, brillo::ErrorPtr)>
        callback,
    std::shared_ptr<std::pair<std::unique_ptr<FlashConfig>, brillo::ErrorPtr>>
        result) {
  CHECK(!thread_.task_runner()->RunsTasksInCurrentSequence());
  std::move(callback).Run(std::move(result->first), std::move(result->second));
}

void AsyncModemFlasher::RunFlashOnThread(
    Modem* modem,
    std::unique_ptr<FlashConfig> flash_cfg,
    std::shared_ptr<std::tuple<bool, base::TimeDelta, brillo::ErrorPtr>>
        result) {
  CHECK(thread_.task_runner()->RunsTasksInCurrentSequence());
  std::get<0>(*result) = flasher_->RunFlash(
      modem, *flash_cfg, &std::get<1>(*result), &std::get<2>(*result));
}

void AsyncModemFlasher::OnRunFlashResult(
    base::OnceCallback<void(bool, base::TimeDelta, brillo::ErrorPtr)> callback,
    std::shared_ptr<std::tuple<bool, base::TimeDelta, brillo::ErrorPtr>>
        result) {
  CHECK(!thread_.task_runner()->RunsTasksInCurrentSequence());
  std::move(callback).Run(std::get<0>(*result), std::get<1>(*result),
                          std::move(std::get<2>(*result)));
}

}  // namespace modemfwd
