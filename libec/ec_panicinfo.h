// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_EC_PANICINFO_H_
#define LIBEC_EC_PANICINFO_H_

#include <stdio.h>

#include <brillo/brillo_export.h>
#include <chromeos/ec/panic_defs.h>

namespace ec {

/**
 * Prints panic information to stdout.
 *
 * @param pdata  Panic information to print
 * @return 0 if success or non-zero error code if error.
 */
BRILLO_EXPORT
int parse_panic_info(const char* data, size_t size);

/**
 * Read stdin to data.
 *
 * @param data  Raw information to store.
 * @param max_size  Maximum size can be stored to data.
 * @return data length if success or non-zero code if error.
 */
BRILLO_EXPORT
int get_panic_input(char *data, size_t max_size);

}  // namespace ec

#endif  // LIBEC_EC_PANICINFO_H_
