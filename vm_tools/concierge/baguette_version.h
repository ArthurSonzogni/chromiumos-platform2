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
constexpr char kBaguetteVersion[] = "2025-06-14-000056_d2cf8cbdde865d8d0a2413a7cc7a8aca85b63e49";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "8ec83e02f7bb96ff1362a156b1c5d5051f332dccf6cbaa79fe51673833088e6e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "c771ad165f6636fd8ad6222f08ad51a2849d6086b2d824bc51d367ae5b2ff4d0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
