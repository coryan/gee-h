//   Copyright 2017 Carlos O'Ryan
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include "gh/etcd_client.hpp"

#include <gmock/gmock.h>

TEST(etcd_client, simple) {
  auto cq = std::make_shared<gh::completion_queue<>>();
  gh::etcd_client client(grpc::InsecureChannelCredentials(), "localhost:2359");

  client.grant_lease(1234, cq, gh::use_future());
}