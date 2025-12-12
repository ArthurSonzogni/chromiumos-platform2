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
constexpr char kBaguetteVersion[] = "2025-12-12-000124_8c62d0cea479eaf223fa8f9e1cf384fb646b7f4d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a95873bdede38e2cc29f4740db47302f2eaf4a0e6f5b9fab0196926471a06113";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "af0b16d754fc6f3abf0882ed8e6bbc6696b2165d7fc2472159059300a1cf0492";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
