// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_EXECUTOR_EXECUTOR_H_
#define RMAD_EXECUTOR_EXECUTOR_H_

#include <string>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include "rmad/executor/mojom/executor.mojom.h"

namespace rmad {

// Production implementation of the chromeos::rmad::mojom::Executor Mojo
// interface.
class Executor final : public chromeos::rmad::mojom::Executor {
 public:
  explicit Executor(
      mojo::PendingReceiver<chromeos::rmad::mojom::Executor> receiver);
  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  ~Executor() override = default;

  // chromeos::rmad::mojom::Executor overrides.
  void MountAndWriteLog(uint8_t device_id,
                        const std::string& log_string,
                        MountAndWriteLogCallback callback) override;
  void MountAndCopyFirmwareUpdater(
      uint8_t device_id, MountAndCopyFirmwareUpdaterCallback callback) override;

 private:
  // Provides a Mojo endpoint that rmad can call to access the executor's Mojo
  // methods.
  mojo::Receiver<chromeos::rmad::mojom::Executor> receiver_;
};

}  // namespace rmad

#endif  // RMAD_EXECUTOR_EXECUTOR_H_
