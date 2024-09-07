// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_CLUSTERING_CLUSTERING_FACTORY_H_
#define ODML_CORAL_CLUSTERING_CLUSTERING_FACTORY_H_

#include <memory>

#include "odml/coral/clustering/agglomerative_clustering.h"

namespace coral::clustering {

class ClusteringFactoryInterface {
 public:
  virtual ~ClusteringFactoryInterface() = default;

  virtual std::unique_ptr<AgglomerativeClusteringInterface>
  NewAgglomerativeClustering(Matrix matrix) = 0;
};

class ClusteringFactory : public ClusteringFactoryInterface {
 public:
  virtual std::unique_ptr<AgglomerativeClusteringInterface>
  NewAgglomerativeClustering(Matrix matrix);
};

}  // namespace coral::clustering

#endif  // ODML_CORAL_CLUSTERING_CLUSTERING_FACTORY_H_
