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
constexpr char kBaguetteVersion[] = "2025-07-19-000122_38881b9f6c005f4587b69f58c0bfa9fdb396e1c5";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "624e9e343186ab0aec35508bd116fde727987dae4bf147fd1b30d5a5e492ba61";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "771ecdf84f67aa7c4dcf726d87c7ce8bcc9f5074e2ec1266702fbfea5e2b4445";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
