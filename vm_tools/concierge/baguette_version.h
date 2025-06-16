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
constexpr char kBaguetteVersion[] = "2025-06-16-000127_d2cf8cbdde865d8d0a2413a7cc7a8aca85b63e49";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "187faf520db4459743d2b88321ef83c8e4b208a17a3e13fa8c87ecc64695955a";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c23a4ce56e5b9b85d4d00f741e49b0161f73921a4761b87fddcbc9e1a020f57c";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
