// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_MOCK_CLUSTERING_FACTORY_H_
#define ODML_CORAL_CLUSTERING_MOCK_CLUSTERING_FACTORY_H_

#include <memory>

#include "odml/coral/clustering/agglomerative_clustering.h"

namespace coral::clustering {

class MockClusteringFactory : public ClusteringFactoryInterface {
 public:
  MOCK_METHOD(std::unique_ptr<AgglomerativeClusteringInterface>,
              NewAgglomerativeClustering,
              (Matrix),
              (override));
};

}  // namespace coral::clustering

#endif  // ODML_CORAL_CLUSTERING_MOCK_CLUSTERING_FACTORY_H_
