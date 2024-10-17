#!/bin/sh
g++ -O2 -ggdb -I . -Wall -Wextra -DTF_NETWORK_UTIL_DEBUG_LOG=1 ../src/TFNetworkUtil.cpp ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp ../src/TFModbusTCPCommon.cpp test_client.cpp -o test_client
g++ -O2 -ggdb -I . -Wall -Wextra -DTF_NETWORK_UTIL_DEBUG_LOG=1 ../src/TFNetworkUtil.cpp ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp ../src/TFModbusTCPCommon.cpp ../src/TFGenericTCPClientPool.cpp ../src/TFModbusTCPClientPool.cpp test_pool.cpp -o test_pool
g++ -O2 -ggdb -I . -Wall -Wextra -DTF_NETWORK_UTIL_DEBUG_LOG=1 ../src/TFNetworkUtil.cpp ../src/TFModbusTCPCommon.cpp ../src/TFModbusTCPServer.cpp test_server.cpp -o test_server
