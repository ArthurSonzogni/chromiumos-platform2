// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
#define VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_

// This constant points to the image downloaded for new installations of
// Baguette.
// TODO(crbug.com/393151776): Point to luci recipe and builders that update this
// URL when new images are available.

// clang-format off
constexpr char kBaguetteVersion[] = "2025-07-05-000134_638c0b94a309996d547234e0f33379610faa262a";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b94e166b374aee94d5da4c24cae51110aaa314c0d3277fdb1b3da4cdc09cbe2c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "a6160413efff27dde1a579605f7c94e2f2a5d88431adb389bd046212360c7d23";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
