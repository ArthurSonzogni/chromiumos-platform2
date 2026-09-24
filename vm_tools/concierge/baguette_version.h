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
constexpr char kBaguetteVersion[] = "2026-09-24-000113_5806634c4ef80c5f90f55b7dc92dcf075590b0f4";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4036993b416a14203b8f3cfaa0985dcee718fc7230d8f62db41861e0b72737be";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c7513794c7821014e8bc0d5644ff2a137320f7953ae6f09de14b7bd8551c9d70";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
