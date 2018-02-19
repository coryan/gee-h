#!/bin/sh

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

echo "DEBUG info for install script:"
ls -l docker-images || /bin/true
ls -l docker-images/${DISTRO} || /bin/true
ls -l docker-images/${DISTRO}/${DISTRO_VERSION} || /bin/true
sudo docker image ls

exit 0
