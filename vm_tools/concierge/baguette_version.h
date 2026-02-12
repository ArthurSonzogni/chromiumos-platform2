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
constexpr char kBaguetteVersion[] = "2026-02-12-000059_33bea3f02a9462c6536e077959a86863ec2cdf90";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "a1d4c340e1f5f95912b48c2a6547fbbe258d9a4947e68a2ad64af71fc283487e";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "dea1d290ebbc7f866a6a2a8d682385499ba403e47d1209b96974d5aa8a7ebce5";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
