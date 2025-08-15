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
constexpr char kBaguetteVersion[] = "2025-08-15-000104_806c0298a0272f88eee93a8eb59e2100fb82f636";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "3929685d5e83ebd827005d9f09721cd6b2eb0a23c1f535b0fbf3d49004663508";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8f280a9c6e8decbbcb2775895f39eef5dfda7b9108ffc99aa222d2eb454dd764";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
