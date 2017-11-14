#!/bin/sh

set -e

if [ "x${LINUX_BUILD}" != "xyes" ]; then
    echo "LINUX_BUILD is not set, exit successfully."
    exit 0
fi

IMAGE="cached-${DISTRO?}-${DISTRO_VERSION?}"
latest_id=$(sudo docker inspect -f '{{ .Id }}' ${IMAGE?}:latest 2>/dev/null || echo "")
cacheargs="";
if [ "x${latest_id}" != "x" ]; then
    cacheargs="--cache-from ${IMAGE?}:latest"
fi

echo "DEBUG information for build:"
echo cache args = $cacheargs;
echo IMAGE = $IMAGE;
echo IMAGE LATEST ID = $latest_id;

EXTRA_ARGS=""
if [ "x${NCPU}" != "x" ]; then
  EXTRA_ARGS="--build-arg NCPU=${NCPU} "
fi

exec sudo docker build -t ${IMAGE?}:tip ${cacheargs?} \
    --build-arg DISTRO_VERSION=${DISTRO_VERSION?} \
    --build-arg CXX=${CXX?} \
    --build-arg CC=${CC?} \
    --build-arg CMAKE_FLAGS="${CMAKE_FLAGS}" \
    --build-arg BUILD_EXTRA=${BUILD_EXTRA} \
    --build-arg TRAVIS_JOB_NUMBER=${TRAVIS_JOB_NUMBER} \
    ${EXTRA_ARGS?} \
    -f ci/Dockerfile.${DISTRO?} .
