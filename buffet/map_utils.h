// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_MAP_UTILS_H_
#define BUFFET_MAP_UTILS_H_

#include <map>
#include <vector>

namespace chromeos {

// Given an STL map returns a vector containing all keys from the map
template<typename T>
std::vector<typename T::key_type> GetMapKeys(const T& map) {
  std::vector<typename T::key_type> keys;
  keys.reserve(map.size());
  for (auto&& pair : map)
    keys.push_back(pair.first);
  return keys;
}

// Given an STL map returns a vector containing all values from the map
template<typename T>
std::vector<typename T::mapped_type> GetMapValues(const T& map) {
  std::vector<typename T::mapped_type> values;
  values.reserve(map.size());
  for (auto&& pair : map)
    values.push_back(pair.second);
  return values;
}

// Given an STL map returns a vector of key-value pairs from the map
template<typename T>
std::vector<std::pair<typename T::key_type,
                      typename T::mapped_type>> MapToVector(const T& map) {
  std::vector<std::pair<typename T::key_type, typename T::mapped_type>> vector;
  vector.reserve(map.size());
  for (auto&& pair : map)
    vector.push_back(pair);
  return vector;
}

} // namespace chromeos

#endif // BUFFET_MAP_UTILS_H_
