// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUSEBOX_UTIL_H_
#define FUSEBOX_UTIL_H_

#include <string>

#include <dbus/message.h>

// Returns errno from |reader| containing the |response| message.
int GetResponseErrno(dbus::MessageReader* reader, dbus::Response* response);

// Returns errno for an |error| that matches base::File::Error codes.
int FileErrorToErrno(int error);

// Returns fuse open flags string: eg., "O_RDWR|O_CREAT|O_TRUNC".
std::string OpenFlagsToString(int flags);

// Returns fuse `to_set` flags string.
std::string ToSetFlagsToString(int flags);

// Redacts name to "redacted" depending on base::logging LOG level.
std::string Redact(const std::string& name);

#endif  // FUSEBOX_UTIL_H_
