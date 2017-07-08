#!/bin/sh

set -e

if [ "x${LINUX_BUILD}" != "xyes" ]; then
    echo "LINUX_BUILD is not set, exit successfully."
    exit 0
fi

# Update docker, the default version installed in Travis is 17.04, just shy of what we need (17.06).
sudo apt-get update
sudo apt-get install -y docker-ce
sudo docker --version

# Restore the docker image from the cached directory.  That way we can reuse the steps in the docker image that
# install pre-requisites and build dependencies ...
TARBALL=docker-images/${DISTRO?}/${DISTRO_VERSION?}/saved.tar.gz
if [ "${LINUX_BUILD?}" = "yes" -a -f ${TARBALL?} ]; then
    gunzip <${TARBALL?} | sudo docker load || echo "Could not load saved image, continue without cache";
fi

echo "DEBUG info for install script:"
ls -l docker-images || /bin/true
ls -l docker-images/${DISTRO} || /bin/true
ls -l docker-images/${DISTRO}/${DISTRO_VERSION} || /bin/true
sudo docker image ls

exit 0
