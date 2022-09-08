// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/common.h"

#include <utility>

#include "iioservice/include/common.h"

namespace iioservice {

std::string GetDeviceTypesInString() {
  std::stringstream ss;
  for (int i = 0; i <= static_cast<int32_t>(cros::mojom::DeviceType::kMaxValue);
       ++i) {
    if (i != 0)
      ss << ", ";
    auto type = static_cast<cros::mojom::DeviceType>(i);
    ss << type << ": " << i;
  }

  return ss.str();
}

TimeoutDelegate::TimeoutDelegate(int timeout_in_milliseconds,
                                 std::string timeout_log,
                                 base::OnceCallback<void()> quit_closure) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&TimeoutDelegate::TimeoutTask, weak_factory_.GetWeakPtr(),
                     timeout_log, std::move(quit_closure)),
      base::Milliseconds(timeout_in_milliseconds));
}

void TimeoutDelegate::TimeoutTask(std::string timeout_log,
                                  base::OnceCallback<void()> quit_closure) {
  LOGF(ERROR) << "Timeout: " << timeout_log;
  std::move(quit_closure).Run();
}

}  // namespace iioservice
