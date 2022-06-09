// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/executor/executor.h"

#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>

#include "rmad/executor/mojom/executor.mojom.h"

namespace rmad {

Executor::Executor(
    mojo::PendingReceiver<chromeos::rmad::mojom::Executor> receiver)
    : receiver_{this, std::move(receiver)} {
  receiver_.set_disconnect_handler(
      base::BindOnce([]() { std::exit(EXIT_SUCCESS); }));
}

void Executor::MountAndWriteLog(uint8_t device_id,
                                const std::string& log_string,
                                MountAndWriteLogCallback callback) {
  // TODO(chenghan): This is now fake.
  std::move(callback).Run(std::nullopt);
}

void Executor::MountAndCopyFirmwareUpdater(
    uint8_t device_id, MountAndCopyFirmwareUpdaterCallback callback) {
  // TODO(chenghan): This is now fake.
  std::move(callback).Run(false);
}

}  // namespace rmad
