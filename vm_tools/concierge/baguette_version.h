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
constexpr char kBaguetteVersion[] = "2025-04-19-000102_2868feb52e332ad363f46d01b6f74af5631ad175";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "c692e49a7a6eb8ccc2146c0835508d06d832cf533a92ef7987215e980dc14247";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "ef21d90c022bf35d560739983b280a0f273d1ab7fe75a8a6b4ad77cd221e3330";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
