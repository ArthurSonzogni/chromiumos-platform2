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
constexpr char kBaguetteVersion[] = "2025-04-23-000119_0a86f71356de22565d3de0398665e714fd142c10";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "ab04168703acd1b46ce5f0cd8e30ebbfff96b34b3607aef24e0449e5e4ad6afe";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8b146e4b3942ef8e6a5acdccd6c123cade675f3371da06bba9050d9258a40ea0";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
