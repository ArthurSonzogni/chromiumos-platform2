// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_CONNECTIVITY_UTIL_H_
#define CRASH_REPORTER_CONNECTIVITY_UTIL_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <session_manager/dbus-proxies.h>

namespace connectivity_util {
struct Session {
  std::string username;
  std::string userhash;
};

// This function fetches the primary logged in username and userhash. This
// username is later checked to see if the user is allowed to record fwdumps.
std::optional<Session> GetPrimaryUserSession(
    org::chromium::SessionManagerInterfaceProxyInterface*
        session_manager_proxy);

// IsConnectivityFwdumpEnabled() checks if connectivity fw dump is enabled
// by checking if the user is a googler or in allowlist and if policy to
// collect connectivity fw dump is set.
bool IsConnectivityFwdumpAllowed(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    const std::string& username);

// GetDaemonStoreFbPreprocessordDirectory function returns complete
// fbpreprocessord daemon-store path for logged in user.
std::optional<base::FilePath> GetDaemonStoreFbPreprocessordDirectory(
    const Session& primary_session);

}  // namespace connectivity_util

#endif  // CRASH_REPORTER_CONNECTIVITY_UTIL_H_
