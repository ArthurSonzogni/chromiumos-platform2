// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_API_DBUS_FUSEBOX_DBUS_CONSTANTS_H_
#define SYSTEM_API_DBUS_FUSEBOX_DBUS_CONSTANTS_H_

namespace fusebox {

// FuseBoxService interface/name/path (chrome)
const char kFuseBoxServiceInterface[] = "org.chromium.FuseBoxService";
const char kFuseBoxServiceName[] = "org.chromium.FuseBoxService";
const char kFuseBoxServicePath[] = "/org/chromium/FuseBoxService";

// FuseBoxService entry stat method.
const char kStatMethod[] = "Stat";

// FuseBoxService directory entry methods.
const char kReadDirMethod[] = "ReadDir";
const char kMkDirMethod[] = "MkDir";
const char kRmDirMethod[] = "RmDir";

// FuseBoxService file entry methods.
const char kOpenMethod[] = "Open";
const char kReadMethod[] = "Read";
const char kWriteMethod[] = "Write";
const char kTruncateMethod[] = "Truncate";
const char kFlushMethod[] = "Flush";
const char kCloseMethod[] = "Close";
const char kCreateMethod[] = "Create";
const char kUnlinkMethod[] = "Unlink";

// FuseBoxReverseService interface/name/path (chromeos /usr/bin/fusebox daemon)
const char kFuseBoxReverseServiceInterface[] =
    "org.chromium.FuseBoxReverseService";
const char kFuseBoxReverseServiceName[] = "org.chromium.FuseBoxReverseService";
const char kFuseBoxReverseServicePath[] = "/org/chromium/FuseBoxReverseService";

// FuseBoxReverseService methods.
const char kAttachStorageMethod[] = "AttachStorage";
const char kDetachStorageMethod[] = "DetachStorage";
const char kReplyToReadDirMethod[] = "ReplyToReadDir";

}  // namespace fusebox

#endif  // SYSTEM_API_DBUS_FUSEBOX_DBUS_CONSTANTS_H_
