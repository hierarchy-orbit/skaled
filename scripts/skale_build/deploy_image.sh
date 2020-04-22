#!/bin/bash
set -e

VERSION=$1
BRANCH=$2
DOCKER_USERNAME=$3
DOCKER_PASSWORD=$4
IMAGE_NAME="skalenetwork/schain:$VERSION"
LATEST_IMAGE_NAME="skalenetwork/schain:$BRANCH-latest"

echo "$DOCKER_PASSWORD" | docker login --username $DOCKER_USERNAME --password-stdin
docker push $IMAGE_NAME
docker push $LATEST_IMAGE_NAME
