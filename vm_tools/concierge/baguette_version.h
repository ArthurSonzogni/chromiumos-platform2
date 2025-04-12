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
constexpr char kBaguetteVersion[] = "2025-04-12-000101_6e28d0224b5d5fbb472047ef01a11fb30f424552";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "4f15683dbdeaa85991e63de04420ba5ec28b7df2d26bd716ab119cdb06c2c4f3";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "8d851cf420d762c1998e0673a0d39a3298c8e88e97bfa48fb6998a4bda5533a9";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
