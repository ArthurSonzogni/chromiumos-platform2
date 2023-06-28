// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTSPLASH_SESSION_EVENT_OBSERVER_H_
#define BOOTSPLASH_SESSION_EVENT_OBSERVER_H_

#include <libchrome/base/observer_list_types.h>
#include <libchrome/base/time/time.h>

namespace bootsplash {

// Interface for observing signals from the power manager client.
class SessionEventObserver : public base::CheckedObserver {
 public:
  SessionEventObserver() = default;
  SessionEventObserver(const SessionEventObserver&) = delete;
  SessionEventObserver& operator=(const SessionEventObserver&) = delete;

  ~SessionEventObserver() override = default;

  // Called when power button is pressed or released.
  virtual void SessionManagerLoginPromptVisibleEventReceived() = 0;
};

}  // namespace bootsplash

#endif  // BOOTSPLASH_SESSION_EVENT_OBSERVER_H_
