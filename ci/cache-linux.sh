#!/bin/sh

set -e

if [ "${TRAVIS_OS_NAME}" != "linux" ]; then
    echo "Skipping build; as this is not a Linux build."
    exit 0
fi

# Save the docker image back to the tarball
IMAGE=cached-${DISTRO?}-${DISTRO_VERSION?}
TARBALL=docker-images/${DISTRO?}/${DISTRO_VERSION?}/saved.tar.gz

sudo docker image tag ${IMAGE?}:tip ${IMAGE?}:latest
sudo docker save ${IMAGE?}:latest | gzip - > ${TARBALL?}

exit 0
