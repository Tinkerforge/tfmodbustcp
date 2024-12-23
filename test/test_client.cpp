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

static volatile bool running = true;

void sigint_handler(int dummy)
{
    (void)dummy;

    TFNetworkUtil::logfln("received SIGINT");

    running = false;
}

int main()
{
    uint16_t read_register_buffer[2] = {0, 0};
    uint16_t write_register_buffer;
    uint8_t read_coil_buffer[2] = {0, 0};
    uint8_t write_coil_buffer;
    char *resolve_host_name = nullptr;
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    TFModbusTCPClient client(TFModbusTCPByteOrder::Host);
    micros_t next_read_time = -1_s;
    micros_t next_reconnect;

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", (int64_t)now_us());
        vprintf(format, args);
        puts("");
    };

    TFNetworkUtil::resolve =
    [&resolve_host_name, &resolve_callback](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_host_name = strdup(host_name);
        resolve_callback = std::move(callback);
    };

    TFNetworkUtil::logfln(" connect...");
    client.connect("localhost", 502,
    [&next_read_time](TFGenericTCPClientConnectResult result, int error_number) {
        TFNetworkUtil::logfln("connect 1st: %s / %s (%d)",
                              get_tf_generic_tcp_client_connect_result_name(result),
                              strerror(error_number),
                              error_number);

        if (result == TFGenericTCPClientConnectResult::Connected) {
            next_read_time = calculate_deadline(100_ms);
        }
        else {
            //running = false;
        }
    },
    [](TFGenericTCPClientDisconnectReason reason, int error_number) {
        TFNetworkUtil::logfln("disconnect 1st: %s / %s (%d)",
                              get_tf_generic_tcp_client_disconnect_reason_name(reason),
                              strerror(error_number),
                              error_number);

        //running = false;
    });

    next_reconnect = calculate_deadline(5_s);

    while (running) {
        if (resolve_host_name != nullptr && resolve_callback) {
            hostent *result = gethostbyname(resolve_host_name);

            free(resolve_host_name);
            resolve_host_name = nullptr;

            if (result == nullptr) {
                resolve_callback(0, h_errno);
                resolve_callback = nullptr;
                continue;
            }

            resolve_callback(((struct in_addr *)result->h_addr)->s_addr, 0);
            resolve_callback = nullptr;
        }

        if (next_read_time >= 0_s && deadline_elapsed(next_read_time)) {
            next_read_time = -1_s;

            TFNetworkUtil::logfln("read input registers...");
            client.transact(1, TFModbusTCPFunctionCode::ReadInputRegisters, 1013, 2, read_register_buffer, 1_s,
            [&read_register_buffer](TFModbusTCPClientTransactionResult result) {
                union {
                    float f;
                    uint16_t r[2];
                } c32;

                c32.r[0] = read_register_buffer[0];
                c32.r[1] = read_register_buffer[1];

                TFNetworkUtil::logfln("read input registers: %s (%d) [%u %u -> %f]",
                                      get_tf_modbus_tcp_client_transaction_result_name(result),
                                      static_cast<int>(result),
                                      c32.r[0],
                                      c32.r[1],
                                      static_cast<double>(c32.f));
            });

            TFNetworkUtil::logfln("read coils...");
            client.transact(1, TFModbusTCPFunctionCode::ReadCoils, 122, 10, read_coil_buffer, 1_s,
            [&next_read_time, &read_coil_buffer](TFModbusTCPClientTransactionResult result) {
                TFNetworkUtil::logfln("read coils: %s (%d) [%u %u %u %u %u %u %u %u %u %u]",
                                      get_tf_modbus_tcp_client_transaction_result_name(result),
                                      static_cast<int>(result),
                                      (read_coil_buffer[0] >> 0) & 1,
                                      (read_coil_buffer[0] >> 1) & 1,
                                      (read_coil_buffer[0] >> 2) & 1,
                                      (read_coil_buffer[0] >> 3) & 1,
                                      (read_coil_buffer[0] >> 4) & 1,
                                      (read_coil_buffer[0] >> 5) & 1,
                                      (read_coil_buffer[0] >> 6) & 1,
                                      (read_coil_buffer[0] >> 7) & 1,
                                      (read_coil_buffer[1] >> 0) & 1,
                                      (read_coil_buffer[1] >> 1) & 1);

                next_read_time = calculate_deadline(100_ms);
            });

            write_register_buffer = 5678;

            TFNetworkUtil::logfln("write register...");
            client.transact(1, TFModbusTCPFunctionCode::WriteSingleRegister, 2233, 1, &write_register_buffer, 1_s,
            [](TFModbusTCPClientTransactionResult result) {
                TFNetworkUtil::logfln("write register: %s (%d)",
                                      get_tf_modbus_tcp_client_transaction_result_name(result),
                                      static_cast<int>(result));
            });

            write_coil_buffer = 1;

            TFNetworkUtil::logfln("write coil...");
            client.transact(1, TFModbusTCPFunctionCode::WriteSingleCoil, 4567, 1, &write_coil_buffer, 1_s,
            [](TFModbusTCPClientTransactionResult result) {
                TFNetworkUtil::logfln("write coil: %s (%d)",
                                      get_tf_modbus_tcp_client_transaction_result_name(result),
                                      static_cast<int>(result));
            });
        }

        if (next_reconnect >= 0_s && deadline_elapsed(next_reconnect)) {
            next_reconnect = -1_s;

            TFNetworkUtil::logfln("disconnect...");
            client.disconnect();

            TFNetworkUtil::logfln("reconnect...");
            client.connect("localhost", 502,
            [&next_read_time](TFGenericTCPClientConnectResult result, int error_number) {
                TFNetworkUtil::logfln("connect 2nd: %s / %s (%d)",
                                      get_tf_generic_tcp_client_connect_result_name(result),
                                      strerror(error_number),
                                      error_number);

                if (result == TFGenericTCPClientConnectResult::Connected) {
                    next_read_time = calculate_deadline(100_ms);
                }
                else {
                    //running = false;
                }
            },
            [](TFGenericTCPClientDisconnectReason reason, int error_number) {
                TFNetworkUtil::logfln("disconnect 2nd: %s / %s (%d)",
                                      get_tf_generic_tcp_client_disconnect_reason_name(reason),
                                      strerror(error_number),
                                      error_number);

                //running = false;
            });
        }

        client.tick();
        usleep(100);
    }

    client.disconnect();

    return 0;
}
