// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_FUZZED_PROTO_GENERATOR_H_
#define LIBBRILLO_BRILLO_FUZZED_PROTO_GENERATOR_H_

#include <vector>

#include <brillo/brillo_export.h>
#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <google/protobuf/unknown_field_set.h>

namespace brillo {

// Generates a fuzzed protobuf buffer: the result is either a valid
// serialization (of protobuf that corresponds to *some* schema) or a
// "corrupted" value that might be close to a valid one to some degree.
class BRILLO_EXPORT FuzzedProtoGenerator final {
 public:
  // `provider` is used to generate the resulting protobuf; it must outlive this
  // instance.
  explicit FuzzedProtoGenerator(FuzzedDataProvider& provider);
  // `byte_breadcrumbs` are byte sequences that can be included in the generated
  // protobuf as-is (but in arbitrary places).
  FuzzedProtoGenerator(std::vector<Blob> byte_breadcrumbs,
                       FuzzedDataProvider& provider);

  FuzzedProtoGenerator(const FuzzedProtoGenerator&) = delete;
  FuzzedProtoGenerator& operator=(const FuzzedProtoGenerator&) = delete;

  ~FuzzedProtoGenerator();

  // Generates the result.
  Blob Generate();

 private:
  // Generates either a fuzzed protobuf message (with recursively generated
  // fuzzed contents) or a byte blob.
  Blob GenerateMessageOrBlob(int nesting_depth);
  // Generates a fuzzed protobuf field (which, potentially, is itself a
  // recursively generated message) and adds it to the field set. Returns
  // `false` when no field was added.
  bool GenerateAndAddField(int nesting_depth,
                           google::protobuf::UnknownFieldSet& field_set);

  FuzzedDataProvider& provider_;
  const std::vector<Blob> byte_breadcrumbs_;
};

}  // namespace brillo

#endif  // LIBBRILLO_BRILLO_FUZZED_PROTO_GENERATOR_H_
