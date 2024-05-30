// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_FLASH_TASK_H_
#define MODEMFWD_FLASH_TASK_H_

#include <optional>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>

#include "modemfwd/daemon_delegate.h"
#include "modemfwd/journal.h"
#include "modemfwd/metrics.h"
#include "modemfwd/modem_flasher.h"
#include "modemfwd/notification_manager.h"

namespace modemfwd {

class FlashTask {
 public:
  FlashTask(Delegate* delegate,
            Journal* journal,
            NotificationManager* notification_mgr,
            Metrics* metrics,
            ModemFlasher* modem_flasher);
  virtual ~FlashTask() = default;

  // Returns false and sets |err| if an error occurred.
  bool Start(Modem* modem, brillo::ErrorPtr* err);

 private:
  void FlashFinished(std::optional<std::string> journal_entry_id,
                     uint32_t fw_types);

  // Owned by Daemon
  Delegate* delegate_;
  Journal* journal_;
  NotificationManager* notification_mgr_;
  Metrics* metrics_;
  ModemFlasher* modem_flasher_;

  base::WeakPtrFactory<FlashTask> weak_ptr_factory_{this};
};

}  // namespace modemfwd

#endif  // MODEMFWD_FLASH_TASK_H_
