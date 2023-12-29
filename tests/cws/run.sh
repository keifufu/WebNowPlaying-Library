#!/usr/bin/env bash

RETURN_PATH=$(pwd)
SCRIPT_PATH=$(dirname "$(readlink -f "$0")")
cd $SCRIPT_PATH

clang -Wno-unknown-pragmas -Wno-int-to-void-pointer-cast -I../../src ../../src/cws.c echo.c -o echo
mkdir -p reports

./echo & \
docker run -it --rm \
    -u 1000:100 \
    -v "${SCRIPT_PATH}/config:/config" \
    -v "${SCRIPT_PATH}/reports:/reports" \
    --name fuzzingclient \
    --network host \
    crossbario/autobahn-testsuite \
    wstest -m fuzzingclient \
    -s /config/fuzzingclient.json

cd $RETURN_PATH