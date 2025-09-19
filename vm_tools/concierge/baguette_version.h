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
constexpr char kBaguetteVersion[] = "2025-09-19-000101_e8256a78f7e92b45e37e6ad13f76839557056832";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cb75a2dca5a9397501a2df60af57a4c7f5791e7cf90c02db50a9db9043509648";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "05e84a2ee8f8bc9d763e526fe09a8a925607534bb21dbd8aae4fdf974b8bd21a";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
