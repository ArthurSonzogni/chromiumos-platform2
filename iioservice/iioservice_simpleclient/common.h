// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_IIOSERVICE_SIMPLECLIENT_COMMON_H_
#define IIOSERVICE_IIOSERVICE_SIMPLECLIENT_COMMON_H_

#include <sstream>
#include <string>

#include <base/memory/weak_ptr.h>

#include "iioservice/mojo/sensor.mojom.h"

namespace iioservice {

std::string GetDeviceTypesInString();

class TimeoutDelegate {
 public:
  TimeoutDelegate(int timeout_in_milliseconds,
                  std::string timeout_log,
                  base::OnceCallback<void()> quit_closure);

 private:
  void TimeoutTask(std::string timeout_log,
                   base::OnceCallback<void()> quit_closure);

  base::WeakPtrFactory<TimeoutDelegate> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_IIOSERVICE_SIMPLECLIENT_COMMON_H_
