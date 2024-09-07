// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/clustering/clustering_factory.h"

#include <memory>
#include <utility>

#include "odml/coral/clustering/agglomerative_clustering.h"

namespace coral::clustering {

std::unique_ptr<AgglomerativeClusteringInterface>
ClusteringFactory::NewAgglomerativeClustering(Matrix matrix) {
  return make_unique<AgglomerativeClustering>(std::move(matrix));
}

}  // namespace coral::clustering
