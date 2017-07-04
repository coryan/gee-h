# Gee-H

A C++14 client library for `etcd` leader election.

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

