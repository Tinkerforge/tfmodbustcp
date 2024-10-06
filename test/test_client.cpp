#include <stdio.h>
#include <stdint.h>
#include <time.h>
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
    uint16_t buffer[2] = {0, 0};
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;
    bool running = true;
    TFModbusTCPClient client;

    TFNetworkUtil::set_logln_callback([](const char *message) {
        printf("%u | %s\n", milliseconds(), message);
    });

    TFNetworkUtil::set_milliseconds_callback(milliseconds);

    TFNetworkUtil::set_resolve_callback([&resolve_callback, &resolve_callback_time](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_callback = callback;
        resolve_callback_time = milliseconds();
    });

    printf("%u | connect...\n", milliseconds());
    client.connect("foobar", 502,
    [&client, &buffer, &running](TFGenericTCPClientConnectResult result, int error_number) {
        printf("%u | connect: %s / %s (%d)\n",
               milliseconds(),
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        if (result != TFGenericTCPClientConnectResult::Connected) {
            running = false;
            return;
        }

        printf("%u | read_register...\n", milliseconds());
        client.read_register(TFModbusTCPClientRegisterType::InputRegister,
                                1,
                                1013,
                                //40000,
                                2,
                                buffer,
                                [&client, &buffer](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = buffer[0];
                                    c32.r[1] = buffer[1];
                                    printf("%u | read_register: %s (%d) [%u %u -> %f]\n",
                                        milliseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
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
        if (resolve_callback && resolve_callback_time + 1000 < milliseconds()) {
            resolve_callback(IPAddress(10, 2, 80, 4), 0);
            resolve_callback = nullptr;
        }

        client.tick();
    }

    client.tick();
    client.tick();
    client.tick();

    return 0;
}
