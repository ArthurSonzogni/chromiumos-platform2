// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_NOOP_SUBPROCESS_CONTROLLER_H_
#define PATCHPANEL_NOOP_SUBPROCESS_CONTROLLER_H_

#include <base/functional/callback_forward.h>
#include <gmock/gmock.h>

#include "patchpanel/ipc.h"
#include "patchpanel/subprocess_controller.h"

namespace patchpanel {

class NoopSubprocessController : public SubprocessControllerInterface {
 public:
  NoopSubprocessController();
  ~NoopSubprocessController() override;

  void SendControlMessage(const ControlMessage& proto) const override;
  void Listen() override;
  void RegisterFeedbackMessageHandler(
      base::RepeatingCallback<void(const FeedbackMessage&)> handler) override;
};

}  // namespace patchpanel
#endif  // PATCHPANEL_NOOP_SUBPROCESS_CONTROLLER_H_
