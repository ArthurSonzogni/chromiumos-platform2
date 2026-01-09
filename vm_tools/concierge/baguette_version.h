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
constexpr char kBaguetteVersion[] = "2026-01-09-000101_236fa43191ebbfcdeca6b65cf0577c3049c4c816";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "dbeaa84064c853b0ee4f9546325f02c8f162fde5cc0e0a934f590b5c4913b9c3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "68c136ea619fe728b7fd8690a1de9f84b13706cb871763599b4cdee6b09882ea";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
