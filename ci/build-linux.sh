#!/bin/sh
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

set -e

if [ "${TRAVIS_OS_NAME}" != "linux" ]; then
    echo "Skipping build; as this is not a Linux build."
    exit 0
fi

IMAGE="gee-h-${DISTRO?}-${DISTRO_VERSION?}"

exec sudo docker run --rm -it \
    --env DISTRO="${DISTRO}" \
    --env DISTRO_VERSION=${DISTRO_VERSION?} \
    --env CXX=${CXX?} \
    --env CC=${CC?} \
    --env CMAKE_FLAGS="${CMAKE_FLAGS}" \
    --env BUILD_EXTRA=${BUILD_EXTRA} \
    --env TRAVIS_JOB_NUMBER=${TRAVIS_JOB_NUMBER} \
    --volume $PWD:/v --workdir /v \
     "${IMAGE}:tip" /v/ci/build-in-docker.sh

exit 0
