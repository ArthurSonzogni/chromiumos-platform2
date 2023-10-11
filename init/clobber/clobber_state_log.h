// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_CLOBBER_STATE_LOG_H_
#define INIT_CLOBBER_CLOBBER_STATE_LOG_H_

#include <string>

#include <base/files/file_path.h>

namespace init {

constexpr char kClobberLogPath[] = "/tmp/clobber-state.log";

void AppendToLog(const std::string_view& source, const std::string& contents);

}  // namespace init

#endif  // INIT_CLOBBER_CLOBBER_STATE_LOG_H_
