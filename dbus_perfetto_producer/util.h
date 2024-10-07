// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DBUS_PERFETTO_PRODUCER_UTIL_H_
#define DBUS_PERFETTO_PRODUCER_UTIL_H_

#include <cstdint>
#include <string>

namespace dbus_perfetto_producer {

bool WriteInt(int, uint64_t);
bool WriteBuf(int, const char*);
bool ReadInt(int, uint64_t&);
bool ReadBuf(int, std::string&);

}  // namespace dbus_perfetto_producer

#endif  // DBUS_PERFETTO_PRODUCER_UTIL_H_
