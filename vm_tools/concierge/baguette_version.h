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
constexpr char kBaguetteVersion[] = "2025-08-10-000131_f3cc1f77dc1a90d2ca8ba23216bc8536bd80e455";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "45e4016641ce39d35fc428fd5e1df4ae16756496d0cdb19df63da2dbd86b3e62";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "25a2b06ad2dcd8caeca7e3ec0f26c042a8c8dd20cf7d8c547f048c21edfc207a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
