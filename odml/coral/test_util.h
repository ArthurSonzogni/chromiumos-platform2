// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_TEST_UTIL_H_
#define ODML_CORAL_TEST_UTIL_H_

#include <utility>
#include <vector>

#include <gmock/gmock.h>

#include "odml/coral/clustering/engine.h"
#include "odml/coral/embedding/engine.h"
#include "odml/coral/title_generation/engine.h"
#include "odml/mojom/coral_service.mojom.h"

// Common helper functions for coral unittests. This header should only be
// included in tests.
namespace coral {

inline std::vector<mojom::EntityPtr> GetFakeEntities() {
  std::vector<mojom::EntityPtr> ret;
  // The following 3 entities are similar.
  ret.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ABC 1", url::mojom::Url::New("abc1.com"))));
  ret.push_back(mojom::Entity::NewApp(mojom::App::New("ABC app 1", "abc1")));
  ret.push_back(mojom::Entity::NewApp(mojom::App::New("ABC app 2", "abc2")));
  // The following 2 entities are similar.
  ret.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("DEF", url::mojom::Url::New("def.com"))));
  ret.push_back(mojom::Entity::NewApp(mojom::App::New("DEF app", "def")));
  // 1 different entity from above.
  ret.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("GHI", url::mojom::Url::New("ghi.com"))));
  return ret;
}

inline std::vector<mojom::EntityPtr> GetFakeSuppressionContext() {
  std::vector<mojom::EntityPtr> ret;
  // The following 2 entities are similar, they exists in the group of 3 above
  // in GetFakeEntities().
  ret.push_back(mojom::Entity::NewTab(
      mojom::Tab::New("ABC 1", url::mojom::Url::New("abc1.com"))));
  ret.push_back(mojom::Entity::NewApp(mojom::App::New("ABC app 1", "abc1")));
  // The following 1 entities are similar, they exists in the group of 2 above
  // in GetFakeEntities().
  ret.push_back(mojom::Entity::NewApp(mojom::App::New("DEF app", "def")));
  return ret;
}

inline EmbeddingResponse GetFakeEmbeddingResponse() {
  return EmbeddingResponse{.embeddings = {
                               // 3 similar items.
                               {0.1, 0.2, 0.3},
                               {0.11, 0.21, 0.31},
                               {0.12, 0.22, 0.32},
                               // 2 similar items.
                               {-0.1, -0.2, -0.3},
                               {-0.11, -0.21, -0.31},
                               // 1 different item from above.
                               {3, -1, 0},
                           }};
}

inline EmbeddingResponse GetFakeSuppressionContextEmbeddingResponse() {
  return EmbeddingResponse{.embeddings = {
                               // 2 items from the group of 3 above.
                               {0.1, 0.2, 0.3},
                               {0.11, 0.21, 0.31},
                               // 1 items from the group of 2 above.
                               {-0.11, -0.21, -0.31},

                           }};
}

inline ClusteringResponse GetFakeClusteringResponse() {
  std::vector<mojom::EntityPtr> entities = GetFakeEntities();
  Cluster cluster1, cluster2, cluster3;
  // In each group, the entities are sorted by the distance to its center.
  cluster1.entities.push_back(std::move(entities[1]));
  cluster1.entities.push_back(std::move(entities[2]));
  cluster1.entities.push_back(std::move(entities[0]));
  cluster2.entities.push_back(std::move(entities[4]));
  cluster2.entities.push_back(std::move(entities[3]));
  cluster3.entities.push_back(std::move(entities[5]));
  ClusteringResponse response;
  response.clusters.push_back(std::move(cluster1));
  response.clusters.push_back(std::move(cluster2));
  response.clusters.push_back(std::move(cluster3));
  return response;
}

inline TitleGenerationResponse GetFakeTitleGenerationResponse() {
  TitleGenerationResponse response;
  std::vector<mojom::EntityPtr> entities = GetFakeEntities();
  auto group1 = mojom::Group::New();
  group1->title = "ABC";
  auto group2 = mojom::Group::New();
  group2->title = "DEF";
  auto group3 = mojom::Group::New();
  group3->title = "GHI";
  // In each group, the entities are sorted by the distance to its center.
  group1->entities.push_back(std::move(entities[1]));
  group1->entities.push_back(std::move(entities[2]));
  group1->entities.push_back(std::move(entities[0]));
  group2->entities.push_back(std::move(entities[4]));
  group2->entities.push_back(std::move(entities[3]));
  group3->entities.push_back(std::move(entities[5]));

  response.groups.push_back(std::move(group1));
  response.groups.push_back(std::move(group2));
  response.groups.push_back(std::move(group3));
  return response;
}

inline mojom::GroupRequestPtr GetFakeGroupRequest() {
  auto request = mojom::GroupRequest::New();
  request->embedding_options = mojom::EmbeddingOptions::New();
  request->clustering_options = mojom::ClusteringOptions::New();
  request->title_generation_options = mojom::TitleGenerationOptions::New();
  request->entities = GetFakeEntities();
  return request;
}

inline mojom::GroupResultPtr GetFakeGroupResult() {
  TitleGenerationResponse title_generation_response =
      GetFakeTitleGenerationResponse();
  return mojom::GroupResult::NewResponse(
      mojom::GroupResponse::New(std::move(title_generation_response.groups)));
}

}  // namespace coral

#endif  // ODML_CORAL_TEST_UTIL_H_
