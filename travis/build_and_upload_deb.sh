#!/bin/bash

# Copyright 2018, OpenSoft Inc.
# All rights reserved.

# Redistribution and use in source and binary forms, with or without modification, are permitted
# provided that the following conditions are met:

#     * Redistributions of source code must retain the above copyright notice, this list of
# conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright notice, this list of
# conditions and the following disclaimer in the documentation and/or other materials provided
# with the distribution.
#     * Neither the name of OpenSoft Inc. nor the names of its contributors may be used to endorse
# or promote products derived from this software without specific prior written permission.

# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
# FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Author: denis.kormalev@opensoftdev.com (Denis Kormalev)
# Author: vasiliy.sorokin@opensoftdev.ru (Vasiliy Sorokin)

set -e

TARGET_NAME=poppler

travis_fold start "prepare.awscli" && travis_time_start;
echo -e "\033[1;33mInstalling awscli...\033[0m";
pip install --user awscli;
travis_time_finish && travis_fold end "prepare.awscli";
echo " ";

travis_fold start "prepare.docker" && travis_time_start;
echo -e "\033[1;33mDownloading and starting Docker container...\033[0m";
docker pull opensoftdev/proof-builder-base:latest;
docker run -id --name builder -w="/sandbox" -v $(pwd):/sandbox/target_src -v $HOME/full_build:/sandbox/build \
    -e "BUILD_ROOT=/sandbox/build" -e "PACKAGE_ROOT=/sandbox/package-$TARGET_NAME" -e "TARGET_NAME=$TARGET_NAME" \
    opensoftdev/proof-builder-base tail -f /dev/null;
docker ps;
travis_time_finish && travis_fold end "prepare.docker";
echo " ";

travis_fold start "prepare.apt_cache" && travis_time_start;
echo -e "\033[1;33mUpdating apt cache...\033[0m";
docker exec -t builder apt-get update;
travis_time_finish && travis_fold end "prepare.apt_cache";
echo " ";

travis_fold start "prepare.dirs" && travis_time_start;
echo -e "\033[1;33mPreparing dirs structure...\033[0m";
echo "$ cp build/package-$TARGET_NAME.tar.gz ./ && tar -xzf package-$TARGET_NAME.tar.gz";
docker exec -t builder bash -c "cp build/package-$TARGET_NAME.tar.gz ./ && tar -xzf package-$TARGET_NAME.tar.gz";
travis_time_finish && travis_fold end "prepare.dirs";
echo " ";

travis_fold start "pack.deb" && travis_time_start;
echo -e "\033[1;33mCreating deb package...\033[0m";
echo "$ fakeroot dpkg-deb --build package-$TARGET_NAME";
docker exec -t builder bash -c "fakeroot dpkg-deb --build package-$TARGET_NAME /sandbox/target_src/xinput-calibrator.deb";
travis_time_finish && travis_fold end "pack.deb";
echo " ";

DEB_FILENAME=`find -maxdepth 1 -name "*.deb" -exec basename "{}" \; -quit`
if [ -z  "$DEB_FILENAME" ]; then
    echo -e "\033[1;31mCan't find created deb package, halting\033[0m";
    exit 1
fi

travis_time_start;
echo -e "\033[1;33mUploading to AWS S3...\033[0m";
aws s3 cp "$DEB_FILENAME" "s3://proof.travis.builds/__dependencies/$DEB_FILENAME"

travis_time_finish
