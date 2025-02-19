/* TFModbusTCP
 * Copyright (C) 2025 Matthias Bolte <matthias@tinkerforge.com>
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
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPServer.h"

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
    TFModbusTCPServer server(TFModbusTCPByteOrder::Host);

    signal(SIGINT, sigint_handler);

    TFNetworkUtil::vlogfln =
    [](const char *format, va_list args) {
        printf("%lu | ", static_cast<int64_t>(now_us()));
        vprintf(format, args);
        puts("");
    };

#define R(a, b) (((a) << 8) | (b))

    uint16_t register_data[] = {
        0x5375, 0x6E53, // Sun Spec ID

        // Common Model header
        1, // ID
        65, // L

        // Common Model block
        R('T', 'i'), R('n', 'k'), R('e', 'r'), R('f', 'o'), R('r', 'g'), R('e', ' '), R('G', 'm'), R('b', 'H'), 0, 0, 0, 0, 0, 0, 0, 0, // Mn
        R('S', 'i'), R('m', 'u'), R('l', 'a'), R('t', 'o'), R('r', '\0'), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // Md
        0, 0, 0, 0, 0, 0, 0, 0, // Opt
        R('1', '.'), R('0', '.'), R('0', '\0'), 0, 0, 0, 0, 0, // Vr
        R('X', '1'), R('Y', '2'), R('Z', '3'), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // SN
        1, // DA

        // Model 714 header
        714, // ID
        18 + 25 * 2, // L

        // Model 714 block
        UINT16_MAX, UINT16_MAX, // PrtAlrms
        2, // NPrt
        (uint16_t)INT16_MIN, // DCA
        (uint16_t)INT16_MIN, // DCW
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhInj
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhAbs
        (uint16_t)INT16_MIN, // DCA_SF
        (uint16_t)INT16_MIN, // DCV_SF
        (uint16_t)INT16_MIN, // DCW_SF
        (uint16_t)INT16_MIN, // DCWH_SF
        (uint16_t)INT16_MIN, // Tmp_SF
        0, // PrtTyp
        1, // ID
        R('F', 'a'), R('k', 'e'), R('-', '1'), 0, 0, 0, 0, 0, // IDStr
        123, // DCA
        45, // DCV
        123 * 45, // DCW
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhInj
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhAbs
        99, // Tmp
        1, // DCSta
        UINT16_MAX, UINT16_MAX, // DCAlrm
        0, // PrtTyp
        2, // ID
        R('F', 'a'), R('k', 'e'), R('-', '2'), 0, 0, 0, 0, 0, // IDStr
        4, // DCA
        321, // DCV
        4 * 321, // DCW
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhInj
        UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, // DCWhAbs
        (uint16_t)-99, // Tmp
        1, // DCSta
        UINT16_MAX, UINT16_MAX, // DCAlrm

        // End Model header
        UINT16_MAX,
        0
    };

    uint16_t register_count = sizeof(register_data) / sizeof(register_data[0]);

    server.start(0, 502,
    [](uint32_t peer_address, uint16_t port) {
        TFNetworkUtil::logfln("connected peer_address=%u port=%u", peer_address, port);
    },
    [](uint32_t peer_address, uint16_t port, TFModbusTCPServerDisconnectReason reason, int error_number) {
        TFNetworkUtil::logfln("disconnected peer_address=%u port=%u reason=%s error_number=%d",
                              peer_address,
                              port,
                              get_tf_modbus_tcp_server_client_disconnect_reason_name(reason),
                              error_number);
    },
    [register_data, register_count](uint8_t unit_id, TFModbusTCPFunctionCode function_code, uint16_t start_address, uint16_t data_count, void *data_values) {
        if (unit_id != 1) {
            return TFModbusTCPExceptionCode::GatewayPathUnvailable;
        }

        if (function_code != TFModbusTCPFunctionCode::ReadHoldingRegisters) {
            return TFModbusTCPExceptionCode::IllegalFunction;
        }

        TFNetworkUtil::logfln("read_holding_registers unit_id=%u start_address=%u data_count=%u data_values=...", unit_id, start_address, data_count);

        if (start_address < 40000) {
            return TFModbusTCPExceptionCode::IllegalDataAddress;
        }

        uint16_t register_offset = start_address - 40000;

        if (register_offset + data_count > register_count) {
            return TFModbusTCPExceptionCode::IllegalDataAddress;
        }

        memcpy(data_values, &register_data[register_offset], data_count * 2);

        return TFModbusTCPExceptionCode::Success;
    });

    while (running) {
        server.tick();
        usleep(100);
    }

    server.stop();

    return 0;
}
