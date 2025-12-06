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
constexpr char kBaguetteVersion[] = "2025-12-06-000109_c78fcb6fa1c46a3aa2590ae4ea46de59a9469f24";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "cb1ab419ac15fc2bc90e6e0d0d073f608f952b9dd4fe2236121f1787e45c0313";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "d83e7159df45dddaa0aefe80b1c283b425c92ac88e5ec1c4a0f17c83e1c9da23";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
