#!/bin/sh
#
# I have not tested this. Good luck.
#

set -e

cd .deps

# JSON-C
curl -LsSO 'https://github.com/json-c/json-c/archive/refs/tags/json-c-0.18-20240915.tar.gz'
tar -xvf json-c-0.18-20240915.tar.gz
cd json-c-json-c-0.18.20240915
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=$(pwd)/../../../libjson ..
make
make install
cd ../../

# H2O
git clone --recurse-submodules --depth=1 https://github.com/h2o/h2o.git
cd h2o
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX:PATH=$(pwd)/../../../libh2o ..
make
make install
cd ../../

# SQLite
curl -LsSO 'https://www.sqlite.org/src/tarball/sqlite.tar.gz?r=release'
tar -xvf sqlite.tar.gz
cd sqlite
./configure --prefix=$(pwd)/../../libsqlite
make
make install

# Unity
curl -LsSO 'https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz'
tar -xvf v2.6.0.tar.gz
cd Unity-2.6.0
cmake -DCMAKE_INSTALL_PREFIX:PATH=(pwd)/../../../libunity ..
make
make install
