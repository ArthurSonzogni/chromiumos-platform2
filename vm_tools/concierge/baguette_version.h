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
constexpr char kBaguetteVersion[] = "2026-02-03-000107_9f1fefb7a67babd7016a64a5694e96c257b5edb1";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "24e3e4699fd6dbf0a4e3e08f94c623b12afb5d0b3cc0ad7c3f9e6d591637979c";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "cc72ca95383dc12ee602e103a2dea4c36c0a003ef5782105971b2f4c8744e8f8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
