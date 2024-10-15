#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <netdb.h>
#include <Arduino.h>
#include "../src/TFNetworkUtil.h"
#include "../src/TFModbusTCPClient.h"
#include "../src/TFModbusTCPClientPool.h"

static uint32_t milliseconds()
{
    struct timeval tv;
    static uint32_t baseline_sec = 0;

    gettimeofday(&tv, nullptr);

    if (baseline_sec == 0) {
        baseline_sec = tv.tv_sec;
    }

    return (tv.tv_sec - baseline_sec) * 1000 + tv.tv_usec / 1000;
}

int main()
{
    uint16_t register_buffer[2] = {0, 0};
    uint8_t coil_buffer = 0;
    char *resolve_host_name = nullptr;
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;
    bool running = true;
    TFModbusTCPClient client;

    TFNetworkUtil::set_logln_callback([](const char *message) {
        printf("%u | %s\n", milliseconds(), message);
    });

    TFNetworkUtil::set_milliseconds_callback(milliseconds);

    TFNetworkUtil::set_resolve_callback([&resolve_host_name, &resolve_callback, &resolve_callback_time](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_host_name = strdup(host_name);
        resolve_callback = callback;
        resolve_callback_time = milliseconds();
    });

    printf("%u | connect...\n", milliseconds());
    client.connect("foobar", 502,
    [&client, &register_buffer, &coil_buffer, &running](TFGenericTCPClientConnectResult result, int error_number) {
        printf("%u | connect: %s / %s (%d)\n",
               milliseconds(),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        if (result != TFGenericTCPClientConnectResult::Connected) {
            running = false;
            return;
        }

        printf("%u | read input registers...\n", milliseconds());
        client.read(TFModbusTCPDataType::InputRegister,
                                1,
                                1013,
                                2,
                                register_buffer,
                                1000,
                                [&client, &register_buffer](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = register_buffer[0];
                                    c32.r[1] = register_buffer[1];
                                    printf("%u | read input registers: %s (%d) [%u %u -> %f]\n",
                                        milliseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
                            });

        printf("%u | read coils...\n", milliseconds());
        client.read(TFModbusTCPDataType::Coil,
                                1,
                                122,
                                5,
                                &coil_buffer,
                                1000,
                                [&client, &coil_buffer](TFModbusTCPClientTransactionResult result) {
                                    printf("%u | read coils: %s (%d) [%u %u %u %u %u]\n",
                                        milliseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        (coil_buffer >> 0) & 1,
                                        (coil_buffer >> 1) & 1,
                                        (coil_buffer >> 2) & 1,
                                        (coil_buffer >> 3) & 1,
                                        (coil_buffer >> 4) & 1);
                                    client.disconnect();
                            });
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number) {
        printf("%u | disconnect: %s / %s (%d)\n",
               milliseconds(),
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        running = false;
    });

    while (running) {
        if (resolve_host_name != nullptr && resolve_callback && resolve_callback_time + 1000 < milliseconds()) {
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

        client.tick();
    }

    client.tick();
    client.tick();
    client.tick();

    return 0;
}
