// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_HERMES_COMMON_H_
#define HERMES_HERMES_COMMON_H_

#include <string>

#include <base/callback.h>
#include <base/time/time.h>
#include <dbus/object_path.h>

namespace hermes {

using ResultCallback = base::OnceCallback<void(int)>;

constexpr auto kLpaRetryDelay = base::TimeDelta::FromSeconds(2);

std::string GetTrailingChars(const std::string& pii, int num_chars);
// Used to redact PII in logs
std::string GetObjectPathForLog(const dbus::ObjectPath& dbus_path);

void IgnoreErrorRunClosure(base::OnceCallback<void()> cb, int err);

}  // namespace hermes

#endif  // HERMES_HERMES_COMMON_H_
