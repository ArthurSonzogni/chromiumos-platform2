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

inline mojom::EntityKeyPtr EntityToKey(mojom::EntityPtr entity) {
  switch (entity->which()) {
    case mojom::Entity::Tag::kUnknown:
      return mojom::EntityKey::NewUnknown(false);
    case mojom::Entity::Tag::kTab:
      return mojom::EntityKey::NewTabUrl(std::move(entity->get_tab()->url));
    case mojom::Entity::Tag::kApp:
      return mojom::EntityKey::NewAppId(std::move(entity->get_app()->id));
  }
}

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

inline ClusteringResponse GetFakeClusteringResponse() {
  std::vector<mojom::EntityPtr> entities = GetFakeEntities();
  std::vector<mojom::EntityPtr> cluster1, cluster2, cluster3;
  for (size_t i = 0; i < 3; i++) {
    cluster1.push_back(std::move(entities[i]));
  }
  for (size_t i = 3; i < 5; i++) {
    cluster2.push_back(std::move(entities[i]));
  }
  cluster3.push_back(std::move(entities[5]));
  ClusteringResponse response;
  response.clusters.emplace_back(std::move(cluster1));
  response.clusters.emplace_back(std::move(cluster2));
  response.clusters.emplace_back(std::move(cluster3));
  return response;
}

inline TitleGenerationResponse GetFakeTitleGenerationResponse() {
  TitleGenerationResponse response;
  std::vector<mojom::EntityPtr> entities = GetFakeEntities();
  std::vector<mojom::EntityKeyPtr> group1, group2, group3;
  for (size_t i = 0; i < 3; i++) {
    group1.push_back(EntityToKey(std::move(entities[i])));
  }
  for (size_t i = 3; i < 5; i++) {
    group2.push_back(EntityToKey(std::move(entities[i])));
  }
  group3.push_back(EntityToKey(std::move(entities[5])));
  response.groups.emplace_back(mojom::Group::New("ABC", std::move(group1)));
  response.groups.emplace_back(mojom::Group::New("DEF", std::move(group2)));
  response.groups.emplace_back(mojom::Group::New("GHI", std::move(group3)));
  return response;
}

inline mojom::GroupRequestPtr GetFakeGroupRequest() {
  auto embedding_options = mojom::EmbeddingOptions::New();
  embedding_options->request_safety_thresholds = mojom::SafetyThresholds::New();
  auto clustering_options = mojom::ClusteringOptions::New();
  auto title_generation_options = mojom::TitleGenerationOptions::New();
  title_generation_options->response_safety_thresholds =
      mojom::SafetyThresholds::New();
  auto request = mojom::GroupRequest::New();
  request->embedding_options = std::move(embedding_options);
  request->clustering_options = std::move(clustering_options);
  request->title_generation_options = std::move(title_generation_options);
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
