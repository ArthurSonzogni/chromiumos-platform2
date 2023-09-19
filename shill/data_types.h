// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DATA_TYPES_H_
#define SHILL_DATA_TYPES_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include <brillo/variant_dictionary.h>
#include <dbus/object_path.h>

namespace shill {

class KeyValueStore;

using Boolean = bool;
using ByteArray = std::vector<uint8_t>;
using ByteArrays = std::vector<ByteArray>;
using Integer = int;
using Integers = std::vector<int>;
using KeyValueStores = std::vector<KeyValueStore>;
using RpcIdentifier = dbus::ObjectPath;
using RpcIdentifiers = std::vector<RpcIdentifier>;
using String = std::string;
using Strings = std::vector<std::string>;
using Stringmap = std::map<std::string, std::string>;
using Stringmaps = std::vector<Stringmap>;
using Uint16s = std::vector<uint16_t>;
using VariantDictionaries = std::vector<brillo::VariantDictionary>;

}  // namespace shill

#endif  // SHILL_DATA_TYPES_H_
