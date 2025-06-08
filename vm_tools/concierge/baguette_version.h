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
constexpr char kBaguetteVersion[] = "2025-06-08-000131_9f25169e5bf1eadd2cfa08d8dc659f8a4632c040";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "17072f05359cc9ac8dbe29d9951b6f56f3b7d973f4c8badcbbb0d048476d7f76";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "f329d9ae08ba273ea85d4c187fec37dde0d391543585bb3a1540c8384ad085e8";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
