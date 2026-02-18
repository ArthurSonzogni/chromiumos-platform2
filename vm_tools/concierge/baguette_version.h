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
constexpr char kBaguetteVersion[] = "2026-02-18-000102_1b8a8aed1fb369c2823f6f6b8d41e44c2633fabe";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "b658eb9818fb4464c9fa59c0e6a4c1db6ba83570a9d5d35bf89fc461cc36a7e5";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "85ecd2c5640f493b1a6c17bb9b0dc73b49af0a8e4626601ce83122c3db3afbaf";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
