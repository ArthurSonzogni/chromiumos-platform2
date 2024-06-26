// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMFWD_FLASH_TASK_H_
#define MODEMFWD_FLASH_TASK_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>

#include "modemfwd/async_modem_flasher.h"
#include "modemfwd/daemon_delegate.h"
#include "modemfwd/daemon_task.h"
#include "modemfwd/journal.h"
#include "modemfwd/logging.h"
#include "modemfwd/metrics.h"
#include "modemfwd/notification_manager.h"
#include "modemfwd/upstart_job_controller.h"

namespace modemfwd {

class FlashTask : public Task {
 public:
  struct Options {
    bool should_always_flash = false;
    std::optional<std::string> carrier_override_uuid = std::nullopt;
  };

  FlashTask(Delegate* delegate,
            Journal* journal,
            NotificationManager* notification_mgr,
            Metrics* metrics,
            scoped_refptr<dbus::Bus> bus,
            scoped_refptr<AsyncModemFlasher> modem_flasher);
  virtual ~FlashTask() = default;

  void Start(Modem* modem, const Options& options);

 private:
  class InhibitMode {
   public:
    explicit InhibitMode(Modem* modem) : modem_(modem) {
      if (!modem_->SetInhibited(true))
        ELOG(INFO) << "Inhibiting failed";
    }

    ~InhibitMode() {
      if (modem_ && !modem_->SetInhibited(false))
        ELOG(INFO) << "Uninhibiting failed";
    }

   private:
    Modem* modem_;
  };

  void OnShouldFlashCompleted(Modem* modem,
                              const Options& options,
                              bool should_flash,
                              brillo::ErrorPtr err);
  void OnBuildFlashConfigCompleted(Modem* modem,
                                   std::unique_ptr<FlashConfig> flash_cfg,
                                   brillo::ErrorPtr err);
  void OnRunFlashCompleted(
      Modem* modem,
      std::unique_ptr<InhibitMode> inhibiter,
      std::vector<std::unique_ptr<UpstartJobController>> upstart_jobs,
      std::optional<std::string> journal_entry_id,
      int32_t types_for_metrics,
      bool success,
      base::TimeDelta flash_duration,
      brillo::ErrorPtr err);

  void FlashFinished(std::optional<std::string> journal_entry_id,
                     uint32_t fw_types);

  // for bookkeeping/naming only
  static int num_flash_tasks_;

  // Owned by Daemon
  Delegate* delegate_;
  Journal* journal_;
  NotificationManager* notification_mgr_;
  Metrics* metrics_;

  scoped_refptr<dbus::Bus> bus_;
  scoped_refptr<AsyncModemFlasher> modem_flasher_;

  base::WeakPtrFactory<FlashTask> weak_ptr_factory_{this};
};

}  // namespace modemfwd

#endif  // MODEMFWD_FLASH_TASK_H_
