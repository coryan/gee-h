# A Belated Design Doc for Gee-H

## Objective

Document how Gee-H implements a leader election protocol based on `etcd`.

## Introduction

Gee-H does not implement a novel leader election protocol.
It uses the common patterns used by other leader election libraries, notably,
the golang client library distributed with `etcd`.

However, the implementation of these protocols necessitates making several
tactical decisions with respect to concurrency, error handling,
naming conventions, etc.
This document attempts to document those issues, and only gives a
cursory introduction to the general topic of leader election protocols.

##  Background: the leader election protocol

In general, libraries that implement a leader election protocol over
`etcd` (or ZooKeeper for that matter) are based on some simple ideas:

 * Each participant in the election creates a node in the lock server to represents its willingness to join the
 election.
 * All the participants in the election create a lease with the `etcd` server.
   * The participants must periodically refresh the lease or they lose it.
   * The node is associated to the lease, if the lease is lost the node is automatically deleted by the `etcd` server.
 * The nodes use the lease id as part of the node name to ensure the node names are unique.
 * All the nodes in a single election share a common prefix, so they can discover each other.
 * The `etcd` server assigns unique, creation versions to each node.
   * These versions are simple integers, so they are easily comparable.
 * The node with the lowest creation version is declared the election winner.
 * Any node that is **not** the election winner watches the other nodes.
 * When the node with the lowest creation version is deleted a new election takes place.
 
A common optimization to avoid thundering herd problems is to have each node only watch **only** its immediate
predecessor in the list of nodes.
That way only one election participant receives a notification when a node is deleted or modified.

A longer introduction can be found in the
[ZooKeeper Recipes](https://zookeeper.apache.org/doc/trunk/recipes.html#sc_leaderElection).

### Practical Considerations

While the protocol is much simpler to implement than the classical computer science algorithms
(cf. [[1]](https://en.wikipedia.org/wiki/Leader_election)),
the implementation still needs to take into account the behavior of `etcd` in practice.
Specifically:

#### Compactions

The `etcd` server administrators can [compact](https://coreos.com/etcd/docs/latest/op-guide/maintenance.html)
its history.  Compactions can happen at any time, particularly while an election is starting or running.
Critically, the protocol depends on the client library setting up a watch to receive updates about changes
in the state of a specific node.
However, if the node is deleted and the data is immediately compacted no such update will be produced by the
`etcd` server.  Instead the application receives a message indicating that the watch is canceled due to
a compaction.
The client library must be prepared to restart the search to find if the node still exists and setup a new
watch on the node.

#### Restarted etcd servers

The `etcd` server administrators need to periodically update and restart the software and operating system on the
machine running the etcd daemon.
The client library must be prepared to restart the election process when the server restarts.


---

## Appendix: Metadata

Status: (**draft**|in review|final)<br>
Author: [Carlos O'Ryan](https://github.com/coryan)<br>
Last Updated: 2017-07

