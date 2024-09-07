// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_MOCK_AGGLOMERATIVE_CLUSTERING_H_
#define ODML_CORAL_CLUSTERING_MOCK_AGGLOMERATIVE_CLUSTERING_H_

#include <memory>

#include "odml/coral/clustering/agglomerative_clustering.h"

namespace coral::clustering {

class MockAgglomerativeClustering : public AgglomerativeClusteringInterface {
 public:
  MOCK_METHOD(std::optional<Groups>,
              Run,
              (LinkageType, std::optional<int>, std::optional<Distance>),
              (const override));
};

}  // namespace coral::clustering

#endif  // ODML_CORAL_CLUSTERING_MOCK_AGGLOMERATIVE_CLUSTERING_H_
