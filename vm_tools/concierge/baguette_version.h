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
constexpr char kBaguetteVersion[] = "2026-06-25-000138_6b25355ff502b97547ea9db5205f13a24f308856";  // NOLINT
constexpr char kBaguetteSHA256X86[] = "97a871f661cf32992e466e8d6c157c5a556418f1f526f0c9b568e17bf84fabea";  // NOLINT
constexpr char kBaguetteSHA256Arm[] = "9973815decf703df1ad0dac4fd33198daeadad0c721595dcf85a6d851b45d414";  // NOLINT
// cpplint:enable

#endif  // VM_TOOLS_CONCIERGE_BAGUETTE_VERSION_H_
