#!/usr/bin/env bash
#   Copyright 2018 Carlos O'Ryan
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.
#

set -e

# By default, Fedora does not look for packages in /usr/local/lib/pkgconfig ...
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig

echo Running build=${TRAVIS_JOB_NUMBER}

# ... verify that the code is properly formatted ...
if [ "x${BUILD_EXTRA}" = "xcheck-style" ]; then
  find gh examples experiments -name '*.[hc]pp' -print0 |
      xargs -0 clang-format -i
  # Report any differences created by running clang-format.
  git diff --ignore-submodules=all --color --exit-code .
fi

# ... verify that the Doxygen code generation works ...
doxygen doc/Doxyfile

# ... build inside a sub-directory, easier to copy the artifacts that way
IMAGE="gee-h-${DISTRO?}-${DISTRO_VERSION?}"

cmake -H. -B".build/${IMAGE}" ${CMAKE_FLAGS}
cmake --build ".build/${IMAGE}" -- -j ${NCPU}
cd "/v/.build/${IMAGE}"
ctest --output-on-failure

# ... verify that the install target works as expected ...
cmake --build . --target install
if [ "x${BUILD_EXTRA}" = "xcheck-install" ]; then
  cd /v/tests/install
  cmake -H. -B.build ${CMAKE_FLAGS}
  cmake --build .build -- -j ${NCPU}
fi
