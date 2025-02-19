#!/bin/sh
COMPILE="g++ -O2 -ggdb -I . -Wall -Wextra -DTF_NETWORK_UTIL_DEBUG_LOG=1 -I ../../tftools/src ../../tftools/src/TFTools/Micros.cpp ../src/TFNetworkUtil.cpp"
$COMPILE ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp ../src/TFModbusTCPCommon.cpp test_client.cpp -o test_client
$COMPILE ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp ../src/TFModbusTCPCommon.cpp ../src/TFGenericTCPClientPool.cpp ../src/TFModbusTCPClientPool.cpp test_pool.cpp -o test_pool
$COMPILE ../src/TFModbusTCPCommon.cpp ../src/TFModbusTCPServer.cpp test_server.cpp -o test_server
$COMPILE ../src/TFModbusTCPCommon.cpp ../src/TFModbusTCPServer.cpp test_sun_spec.cpp -o test_sun_spec
