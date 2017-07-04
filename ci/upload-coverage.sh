#!/bin/bash

# Exit on the first error
set -e

if [ "x$TRAVIS_PULL_REQUEST" != "xfalse" ]; then
    echo "Code coverage disabled in pull requests"
    exit 0
fi

if [ "x${BUILD_EXTRA}" != "xCOVERAGE" ]; then
    echo "Code coverage not enabled, this is not a code coverage build"
    exit 0
fi

IMAGE="cached-${DISTRO?}-${DISTRO_VERSION?}";
sudo docker run --volume $PWD:/d --rm -it ${IMAGE}:tip cp -r /var/tmp/build-gee-h/test_coverage /d;

if [ "x${CODECOV_TOKEN}" != "x" ]; then
    cd test_coverage
    bash <(curl -s https://codecov.io/bash)  || echo "Coverage upload failed."
fi

exit 0
