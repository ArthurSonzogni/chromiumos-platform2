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
constexpr char kBaguetteVersion[] = "2026-10-26-000122_bfbf5d584b0fa3c1201043483c5a1008dc17a88d";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cc5c6685ee001a8610104f5d14c664479a9c1caa42fd0329438d214363624330";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "fe640fdb18c09b63fba20b42a029beba298be2d9d2b22216ac3c273d34a144ae";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
