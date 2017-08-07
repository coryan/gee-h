# Gee-H

A C++14 client library for leader election using `etcd`.

## Status
[![Build Status](https://travis-ci.org/coryan/gee-h.svg?branch=master)](https://travis-ci.org/coryan/gee-h)
[![codecov](https://codecov.io/gh/coryan/gee-h/branch/master/graph/badge.svg)](https://codecov.io/gh/coryan/gee-h)

This library is work in progress, I have a working prototype in a separate project
([JayBeams](https://github.com/coryan/jaybeams/)) which I will be migrating to Gee-H.

## Install

This library is built using `cmake(1)`.  On Linux and other Unix variants the usual commands to build it are:

```commandline
cmake .
make
make test
make install
```

## Motivation

[Leader election](https://en.wikipedia.org/wiki/Leader_election)
is a building block for large distributed systems.
The context is some component of the distributed system that needs to be
replicated for increased availability, but that cannot have more
than one instance active at a time because the component acts as a coordinator to
distribute some non-trivially parallelizable workload.

The common solution is to perform a leader election between the instances
of the component in question.
The instance that "wins" the election plays the leader role and remains active.
The other instances passively wait until the leader terminates, gracefully or not.

Gee-H solves this coordination problem using the
[`etcd` lock service](https://en.wikipedia.org/wiki/Container_Linux_by_CoreOS#Cluster_infrastructure).

## Watching an Election

The `examples/observe_election.cpp` example shows how to watch an election.
After some initialization boilerplate the main class is created using:

```cpp
  auto channel = grpc::CreateChannel(...);
  auto queue = std::make_shared<gh::active_completion_queue>();
  gh::watch_election (queue.cq(), channel, election_name);
```

the class monitors the given election (`election_name` in this case).
The application can create callbacks to receive updates about the election:

```cpp
auto subscriber = [](std::string const& key, std::string const& value) {
  if (key.empty()) {
    std::cout << "no current leader" << std::endl;
    return;
  }
  std::cout << "current leader is " << key << ", with value=" << value << std::endl;
};
auto token = election_observer->subscribe(std::move(subscriber));
```

## Joining an Election

The `examples/join_election.cpp` example shows how to join an election.
After some initialization boilerplate the main class is created using:

```cpp
  auto channel = grpc::CreateChannel(...);
  auto queue = std::make_shared<gh::active_completion_queue>();
  gh::leader_election election(queue, channel, "my-election", "my-value", std::chrono::seconds(5));
```

The application can then block until the current candidate wins the election:

```cpp
auto fut = election.campaign();
bool elected = fut.get();
```

## What is that name?

[Gee-H](https://en.wikipedia.org/wiki/Gee-H_(navigation)) was a radio navigation system developed during
World War II.  It has nothing to do with leader election, or C++14, or the `etcd` server.
I just like short names for namespaces: `gh::` in this case.


## LICENSE

gee-h is distributed under the Apache License, Version 2.0.
The full licensing terms can be found in the `LICENSE` file.

---

   Copyright 2017 Carlos O'Ryan

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

