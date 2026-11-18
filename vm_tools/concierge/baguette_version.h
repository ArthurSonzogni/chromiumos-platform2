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
constexpr char kBaguetteVersion[] = "2026-11-18-000122_c86eb9c8d2de4117128f657d92de8537c1acbb63";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "29dc67c17bc8970bd5b4f8506bd692c13d551891726ef10dd24b261e2f4b6930";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "59a329c762b68608a79b6bac52376ba1d3a9ae65c24ffc4aeab1096887e44d04";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
