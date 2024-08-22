#!/bin/sh
g++ -O0 -ggdb -I . -Wall -Wextra ../src/TFNetworkUtil.cpp ../src/TFGenericTCPClient.cpp ../src/TFModbusTCPClient.cpp test.cpp
