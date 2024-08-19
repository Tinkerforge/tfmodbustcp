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

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <functional>

// specification
#define TF_MODBUS_TCP_CLIENT_HEADER_LENGTH 7
#define TF_MODBUS_TCP_CLIENT_MIN_FRAME_LENGTH 3
#define TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH 253
#define TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH 1
#define TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH (TF_MODBUS_TCP_CLIENT_MAX_FRAME_LENGTH - TF_MODBUS_TCP_CLIENT_FRAME_IN_HEADER_LENGTH)
#define TF_MODBUS_TCP_CLIENT_MIN_REGISTER_COUNT 1
#define TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT 125

// configuration
#define TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT 16
#define TF_MODBUS_TCP_CLIENT_MAX_TICK_DURATION 10 // milliseconds
#define TF_MODBUS_TCP_CLIENT_MIN_RECONNECT_DELAY 1000 // milliseconds
#define TF_MODBUS_TCP_CLIENT_MAX_RECONNECT_DELAY 16000 // milliseconds
#define TF_MODBUS_TCP_CLIENT_CONNECT_TIMEOUT 3000 // milliseconds
#define TF_MODBUS_TCP_CLIENT_REQUEST_TIMEOUT 100 // milliseconds
#define TF_MODBUS_TCP_CLIENT_RESPONSE_TIMEOUT 1000 // milliseconds

enum class TFModbusTCPClientRegisterType
{
    HoldingRegister,
    InputRegister,
};

enum class TFModbusTCPClientTransactionResult
{
    Success = 0,

    ModbusIllegalFunction = 0x01,
    ModbusIllegalDataAddress = 0x02,
    ModbusIllegalDataValue = 0x03,
    ModbusServerDeviceFailure = 0x04,
    ModbusAcknowledge = 0x05,
    ModbusServerDeviceBusy = 0x06,
    ModbusMemoryParityError = 0x08,
    ModbusGatewayPathUnvailable = 0x0a,
    ModbusGatewayTargetDeviceFailedToRespond = 0x0b,

    InvalidArgument = 256,
    AbortedByDisconnect,
    AbortedByOtherError,
    AllTransactionsPending,
    NotConnected,
    DisconnectedByPeer,
    RequestSendFailed,
    RequestTimeout,
    ResponseReceiveFailed,
    ResponseTimeout,
    ResponseShorterThanMinimum,
    ResponseLongerThanMaximum,
    ResponseUnitIDMismatch,
    ResponseFunctionCodeMismatch,
    ResponseByteCountInvalid,
    ResponseRegisterCountMismatch,
    ResponseTooShort,
};

const char *get_tf_modbus_tcp_client_transaction_result_name(TFModbusTCPClientTransactionResult result);

enum class TFModbusTCPClientConnectionStatus
{
    InvalidArgument, // final
    ConcurrentConnect, // final
    NoResolveCallback, // final
    ResolveInProgress,
    ResolveFailed, // errno as received from resolve callback
    Resolved,
    SocketCreateFailed, // errno
    SocketGetFlagsFailed, // errno
    SocketSetFlagsFailed, // errno
    SocketConnectFailed, // errno
    SocketSelectFailed, // errno
    SocketGetOptionFailed, // errno
    SocketConnectAsyncFailed, // errno
    SocketReceiveFailed, // errno
    SocketIoctlFailed, // errno
    SocketSendFailed, // errno
    ConnectInProgress,
    ConnectTimeout,
    Connected,
    Disconnected, // final
    DisconnectedByPeer,
    ModbusProtocolError,
};

const char *get_tf_modbus_tcp_client_connection_status_name(TFModbusTCPClientConnectionStatus status);

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wattributes"
#endif

union TFModbusTCPClientHeader
{
    struct [[gnu::packed]] {
        uint16_t transaction_id;
        uint16_t protocol_id;
        uint16_t frame_length;
        uint8_t unit_id;
    };
    uint8_t bytes[TF_MODBUS_TCP_CLIENT_HEADER_LENGTH];
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

struct TFModbusTCPClientTransaction
{
    uint16_t transaction_id;
    uint8_t unit_id;
    uint8_t function_code;
    uint16_t register_count;
    uint16_t *buffer;
    uint32_t request_sent;
    std::function<void(TFModbusTCPClientTransactionResult result)> callback;
};

union TFModbusTCPClientRegisterResponsePayload
{
    struct {
        uint8_t function_code;
        union {
            uint8_t exception_code;
            uint8_t byte_count;
        };
        uint16_t register_values[TF_MODBUS_TCP_CLIENT_MAX_REGISTER_COUNT];
    };
    uint8_t bytes[TF_MODBUS_TCP_CLIENT_MAX_PAYLOAD_LENGTH];
};

void set_tf_modbus_tcp_client_resolve_callback(std::function<void(String host_name, std::function<void(IPAddress host_address, int error_number)> &&callback)> &&callback);

class TFModbusTCPClient
{
public:
    TFModbusTCPClient() { memset(transactions, 0, sizeof(transactions)); }

    void connect(String host_name, uint16_t port, std::function<void(TFModbusTCPClientConnectionStatus status, int error_number)> &&callback);
    void disconnect();
    bool is_connected() const { return socket_fd >= 0; }
    void tick();
    void read_register(TFModbusTCPClientRegisterType register_type,
                       uint8_t unit_id,
                       uint16_t start_address,
                       uint16_t register_count,
                       uint16_t *buffer,
                       std::function<void(TFModbusTCPClientTransactionResult result)> &&callback);

private:
    ssize_t receive_payload(size_t length);
    TFModbusTCPClientTransaction *take_transaction(uint16_t transaction_id);
    void finish_transaction(uint16_t transaction_id, TFModbusTCPClientTransactionResult result);
    void finish_transaction(TFModbusTCPClientTransaction *transaction, TFModbusTCPClientTransactionResult result);
    void finish_all_transactions(TFModbusTCPClientTransactionResult result);
    void check_transaction_timeout();
    void reset_pending_response();
    void abort_connection(TFModbusTCPClientConnectionStatus status, int error_number);

    String host_name;
    uint16_t port = 0;
    std::function<void(TFModbusTCPClientConnectionStatus status, int error_number)> status_callback;
    uint32_t reconnect_deadline = 0;
    uint32_t reconnect_delay = 0;
    bool resolve_pending = false;
    uint32_t resolve_id = 0;
    IPAddress pending_host_address;
    int pending_socket_fd = -1;
    uint32_t connect_id = 0;
    uint32_t connect_timeout_deadline = 0;
    int socket_fd = -1;
    uint16_t next_transaction_id = 0;
    TFModbusTCPClientTransaction *transactions[TF_MODBUS_TCP_CLIENT_MAX_TRANSACTION_COUNT];
    TFModbusTCPClientHeader pending_header;
    size_t pending_header_used = 0;
    bool pending_header_checked = false;
    TFModbusTCPClientRegisterResponsePayload pending_payload;
    size_t pending_payload_used = 0;
};
