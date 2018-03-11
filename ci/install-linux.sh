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

# Restore the docker image from the cached directory.  That way we can reuse the steps in the docker image that
# install pre-requisites and build dependencies ...
TARBALL=docker-images/${DISTRO?}/${DISTRO_VERSION?}/saved.tar.gz
if [ -f ${TARBALL?} ]; then
    gunzip <${TARBALL?} | sudo docker load || echo "Could not load saved image, continue without cache";
fi

IMAGE="gee-h-${DISTRO?}-${DISTRO_VERSION?}"
latest_id=$(sudo docker inspect -f '{{ .Id }}' ${IMAGE?}:latest 2>/dev/null || echo "")
cacheargs="";
if [ "x${latest_id}" != "x" ]; then
    cacheargs="--cache-from ${IMAGE?}:latest"
fi

echo "DEBUG info for install script:"
sudo docker image ls
echo cache args = $cacheargs;
echo IMAGE = $IMAGE;
echo IMAGE LATEST ID = $latest_id;

EXTRA_ARGS=""
if [ "x${NCPU}" != "x" ]; then
  EXTRA_ARGS="--build-arg NCPU=${NCPU} "
fi

exec sudo docker build -t ${IMAGE?}:tip ${cacheargs?} \
    --build-arg DISTRO_VERSION=${DISTRO_VERSION?} \
    ${EXTRA_ARGS?} \
    -f ci/Dockerfile.${DISTRO?} ci
