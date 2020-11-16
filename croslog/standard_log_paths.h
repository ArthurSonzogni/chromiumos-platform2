// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROSLOG_STANDARD_LOG_PATHS_H_
#define CROSLOG_STANDARD_LOG_PATHS_H_

namespace croslog {

static const char* kLogSources[] = {
    // Log files from rsyslog:
    // clang-format off
    "/var/log/arc.log",
    "/var/log/boot.log",
    "/var/log/hammerd.log",
    "/var/log/messages",
    "/var/log/net.log",
    "/var/log/secure",
    "/var/log/upstart.log",
    // clang-format on
};

static const char kAuditLogSources[] = "/var/log/audit/audit.log";

}  // namespace croslog

#endif  // CROSLOG_STANDARD_LOG_PATHS_H_
