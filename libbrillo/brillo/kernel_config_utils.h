// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_KERNEL_CONFIG_UTILS_H_
#define LIBBRILLO_BRILLO_KERNEL_CONFIG_UTILS_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>

namespace brillo {

// Fetch the config of the current kernel. Returns the contents of
// `/proc/cmdline` file if successful, nullopt otherwise.
BRILLO_EXPORT std::optional<std::string> GetCurrentKernelConfig();

// Conveniently invoke the external dump_kernel_config library.
BRILLO_EXPORT std::optional<std::string> DumpKernelConfig(
    const base::FilePath& kernel_dev);

// ExtractKernelNamedArg(DumpKernelConfig(..), "root") -> /dev/dm-0.
// This understands quoted values. dm -> "a b c, foo=far" (strips quotes).
// Returns nullopt if no key found, otherwise returns value.
// Does not support escaped quotes that might be present in values
// (e.g.: foo="bar\" bar2").
BRILLO_EXPORT std::optional<std::string> ExtractKernelArgValue(
    const std::string& kernel_config,
    const std::string& key,
    const bool strip_quotes = true);

// Take a kernel style argument list and modify a single argument value.
// Quotes will be added to the value if value contains any whitespace. No
// escaping will be added for existing characters (ex: values with quotes would
// break setting).  Note that this only supports modification of exiting keys,
// and not addition of new key/value pairs.
BRILLO_EXPORT bool SetKernelArg(const std::string& key,
                                const std::string& value,
                                std::string& kernel_config);

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_KERNEL_CONFIG_UTILS_H_
