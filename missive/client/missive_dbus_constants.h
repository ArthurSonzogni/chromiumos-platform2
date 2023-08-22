// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_CLIENT_MISSIVE_DBUS_CONSTANTS_H_
#define MISSIVE_CLIENT_MISSIVE_DBUS_CONSTANTS_H_

namespace missive {

constexpr char kMissiveServiceInterface[] = "org.chromium.Missived";
constexpr char kMissiveServicePath[] = "/org/chromium/Missived";
constexpr char kMissiveServiceName[] = "org.chromium.Missived";

// Methods exported by missived.
constexpr char kEnqueueRecord[] = "EnqueueRecord";
constexpr char kFlushPriority[] = "FlushPriority";
constexpr char kConfirmRecordUpload[] = "ConfirmRecordUpload";
constexpr char kUpdateConfigInMissive[] = "UpdateConfigInMissive";
constexpr char kUpdateEncryptionKey[] = "UpdateEncryptionKey";

}  // namespace missive

#endif  // MISSIVE_CLIENT_MISSIVE_DBUS_CONSTANTS_H_
