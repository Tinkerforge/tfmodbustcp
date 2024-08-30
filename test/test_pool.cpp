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
    TFModbusTCPClient client;
    uint16_t buffer1[2] = {0, 0};
    uint16_t buffer2[2] = {0, 0};
    std::function<void(uint32_t host_address, int error_number)> resolve_callback;
    uint32_t resolve_callback_time;
    int running = 2;
    TFModbusTCPClientPool pool;
    TFGenericTCPClientPoolHandle *handle_ptr1 = nullptr;
    TFGenericTCPClientPoolHandle *handle_ptr2 = nullptr;

    TFNetworkUtil::set_milliseconds_callback(milliseconds);

    TFNetworkUtil::set_resolve_callback([&resolve_callback, &resolve_callback_time](const char *host_name, std::function<void(uint32_t host_address, int error_number)> &&callback) {
        resolve_callback = callback;
        resolve_callback_time = milliseconds();
    });

    printf("%u | acquire1...\n", milliseconds());
    pool.acquire("foobar", 502,
    [&pool, &handle_ptr1, &buffer1, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%u | connect1 handle=%p: %s / %s (%d)\n",
               milliseconds(),
               handle,
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        handle_ptr1 = handle;

#if 1
        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        pool.release(handle);
#else
        printf("%u | read_register1... handle=%p\n", milliseconds(), handle);
        handle->client->read_register(TFModbusTCPClientRegisterType::InputRegister,
                                1,
                                1013,
                                2,
                                buffer1,
                                [&pool, handle, &buffer1, &running](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = buffer1[0];
                                    c32.r[1] = buffer1[1];
                                    printf("%u | read_register: %s (%d) [%u %u -> %f]\n",
                                        milliseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
                                printf("%u | release1 handle=%p\n",
                                        milliseconds(),
                                        handle);
                                pool.release(handle);
                            });
#endif
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%u | disconnect1 handle=%p: %s / %s (%d)\n",
               milliseconds(),
               handle,
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        --running;
    });

    printf("%u | acquire2...\n", milliseconds());
    pool.acquire("foobar", 502,
    [&pool, &handle_ptr2, &buffer2, &running](TFGenericTCPClientConnectResult result, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%u | connect2 handle=%p: %s / %s (%d)\n",
               milliseconds(),
               handle,
               get_tf_generic_tcp_client_connect_result_name(result),
               strerror(error_number),
               error_number);

        handle_ptr2 = handle;

        if (result != TFGenericTCPClientConnectResult::Connected) {
            --running;
            return;
        }

        printf("%u | read_register2... handle=%p\n", milliseconds(), handle);
        static_cast<TFModbusTCPClient *>(handle->client)->read_register(TFModbusTCPClientRegisterType::InputRegister,
                                1,
                                1013,
                                2,
                                buffer2,
                                [&pool, handle, &buffer2, &running](TFModbusTCPClientTransactionResult result) {
                                    union {
                                        float f;
                                        uint16_t r[2];
                                    } c32;

                                    c32.r[0] = buffer2[0];
                                    c32.r[1] = buffer2[1];
                                    printf("%u | read_register2: %s (%d) [%u %u -> %f]\n",
                                        milliseconds(),
                                        get_tf_modbus_tcp_client_transaction_result_name(result),
                                        static_cast<int>(result),
                                        c32.r[0],
                                        c32.r[1],
                                        static_cast<double>(c32.f));
                                printf("%u | release2 handle=%p\n",
                                        milliseconds(),
                                        handle);
                                pool.release(handle);
                            });
    },
    [&running](TFGenericTCPClientDisconnectReason reason, int error_number, TFGenericTCPClientPoolHandle *handle) {
        printf("%u | disconnect2 handle=%p: %s / %s (%d)\n",
               milliseconds(),
               handle,
               get_tf_generic_tcp_client_disconnect_reason_name(reason),
               strerror(error_number),
               error_number);

        --running;
    });

    while (running > 0) {
        if (resolve_callback && resolve_callback_time + 1000 < milliseconds()) {
            resolve_callback(IPAddress(10, 2, 80, 4), 0);
            resolve_callback = nullptr;
        }

        pool.tick();
    }

    pool.tick();
    pool.tick();
    pool.tick();

    return 0;
}
