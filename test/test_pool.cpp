/* TFModbusTCP
 * Copyright (C) 2024 Matthias Bolte <matthias@tinkerforge.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPClient.h"
#include "../src/TFModbusTCPClientPool.h"

micros_t now_us()
{
    struct timeval tv;
    static int64_t baseline_sec = 0;

    gettimeofday(&tv, nullptr);

    if (baseline_sec == 0) {
        baseline_sec = tv.tv_sec;
    }

    return micros_t{(static_cast<int64_t>(tv.tv_sec) - baseline_sec) * 1000000 + tv.tv_usec};
}

static volatile int running = 2;

void sigint_handler(int dummy)
{
    (void)dummy;

    TFNetworkUtil::logfln("received SIGINT");

    running = 0;
}

int main()
{
    uint16_t buffer1[2] = {0, 0};
    uint16_t buffer2[2] = {0, 0};
    TFModbusTCPClientPool pool(TFModbusTCPByteOrder::Host);
    TFGenericTCPSharedClient *client_ptr1 = nullptr;
    TFGenericTCPSharedClient *client_ptr2 = nullptr;
    micros_t next_reconnect;

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", static_cast<int64_t>(now_us()));
        vprintf(format, args);
        puts("");
    };

    TFNetworkUtil::resolve =
    [](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        hostent *result = gethostbyname(host_name);

        if (result == nullptr) {
            callback(0, h_errno);
        }
        else {
            callback(((struct in_addr *)result->h_addr)->s_addr, 0);
        }
    };

    TFNetworkUtil::logfln("acquire1...");
    pool.acquire("localhost", 502,
    [&pool, &client_ptr1, &buffer1](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
        TFNetworkUtil::logfln("connect1 1st client=%p: %s / %s (%d)",
                              static_cast<void *>(client),
                              get_tf_generic_tcp_client_connect_result_name(result),
                              strerror(error_number),
                              error_number);

        client_ptr1 = client;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        TFNetworkUtil::logfln("read1... client=%p", static_cast<void *>(client));
        static_cast<TFModbusTCPSharedClient *>(client)->transact(1, TFModbusTCPFunctionCode::ReadInputRegisters, 1013, 2, buffer1, 1_s,
        [&pool, client, &buffer1](TFModbusTCPClientTransactionResult result) {
            union {
                float f;
                uint16_t r[2];
            } c32;

            c32.r[0] = buffer1[0];
            c32.r[1] = buffer1[1];

            TFNetworkUtil::logfln("read1: %s (%d) [%u %u -> %f]",
                                  get_tf_modbus_tcp_client_transaction_result_name(result),
                                  static_cast<int>(result),
                                  c32.r[0],
                                  c32.r[1],
                                  static_cast<double>(c32.f));
        });
    },
    [&client_ptr1](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
        TFNetworkUtil::logfln("disconnect1 1st client=%p: %s / %s (%d)",
                              static_cast<void *>(client),
                              get_tf_generic_tcp_client_disconnect_reason_name(reason),
                              strerror(error_number),
                              error_number);

        client_ptr1 = nullptr;
        --running;
    });

    TFNetworkUtil::logfln("acquire2...");
    pool.acquire("localhost", 1502,
    [&pool, &client_ptr2, &buffer2](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
        TFNetworkUtil::logfln("connect2 client=%p: %s / %s (%d)",
                              static_cast<void *>(client),
                              get_tf_generic_tcp_client_connect_result_name(result),
                              strerror(error_number),
                              error_number);

        client_ptr2 = client;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        TFNetworkUtil::logfln("read2... client=%p", static_cast<void *>(client));
        static_cast<TFModbusTCPSharedClient *>(client)->transact(1, TFModbusTCPFunctionCode::ReadInputRegisters, 1013, 2, buffer2, 1_s,
        [&pool, &buffer2](TFModbusTCPClientTransactionResult result) {
            union {
                float f;
                uint16_t r[2];
            } c32;

            c32.r[0] = buffer2[0];
            c32.r[1] = buffer2[1];

            TFNetworkUtil::logfln("read2: %s (%d) [%u %u -> %f]",
                                  get_tf_modbus_tcp_client_transaction_result_name(result),
                                  static_cast<int>(result),
                                  c32.r[0],
                                  c32.r[1],
                                  static_cast<double>(c32.f));
        });
    },
    [&client_ptr2](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
        TFNetworkUtil::logfln("disconnect2 client=%p: %s / %s (%d)",
                              static_cast<void *>(client),
                              get_tf_generic_tcp_client_disconnect_reason_name(reason),
                              strerror(error_number),
                              error_number);

        client_ptr2 = nullptr;
        --running;
    });

    next_reconnect = calculate_deadline(5_s);

    while (running > 0) {
        if (client_ptr1 != nullptr && next_reconnect >= 0_s && deadline_elapsed(next_reconnect)) {
            next_reconnect = -1_s;

            TFNetworkUtil::logfln("release1...");
            pool.release(client_ptr1);
            client_ptr1 = nullptr;

            TFNetworkUtil::logfln("reacquire1...");
            pool.acquire("localhost", 502,
            [&pool, &client_ptr1, &buffer1](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPSharedClient *client) {
                TFNetworkUtil::logfln("connect1 2nd client=%p: %s / %s (%d)",
                                      static_cast<void *>(client),
                                      get_tf_generic_tcp_client_connect_result_name(result),
                                      strerror(error_number),
                                      error_number);

                client_ptr1 = client;
            },
            [&client_ptr1](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPSharedClient *client) {
                TFNetworkUtil::logfln("disconnect1 2nd client=%p: %s / %s (%d)",
                                      static_cast<void *>(client),
                                      get_tf_generic_tcp_client_disconnect_reason_name(reason),
                                      strerror(error_number),
                                      error_number);

                client_ptr1 = nullptr;
            });
        }

        pool.tick();
        usleep(100);
    }

    if (client_ptr1 != nullptr) {
        TFNetworkUtil::logfln("release1 client=%p", static_cast<void *>(client_ptr1));
        pool.release(client_ptr1);
    }

    if (client_ptr2 != nullptr) {
        TFNetworkUtil::logfln("release2 client=%p", static_cast<void *>(client_ptr2));
        pool.release(client_ptr2);
    }

    pool.tick();

    return 0;
}
