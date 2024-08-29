#!/bin/sh
g++ -O0 -ggdb -I . -Wall -Wextra ../src/TFNetworkUtil.cpp ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp test_client.cpp -o test_client
g++ -O0 -ggdb -I . -Wall -Wextra ../src/TFNetworkUtil.cpp ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp ../src/TFModbusTCPClientPool.cpp test_pool.cpp -o test_pool
