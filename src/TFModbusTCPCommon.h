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

#include <stdint.h>

// specification
#define TF_MODBUS_TCP_HEADER_LENGTH                       7u
#define TF_MODBUS_TCP_MIN_FRAME_LENGTH                    3u
#define TF_MODBUS_TCP_MAX_FRAME_LENGTH                    253u
#define TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH              1u
#define TF_MODBUS_TCP_RESPONSE_PAYLOAD_BEFORE_DATA_LENGTH 2u
#define TF_MODBUS_TCP_MAX_PAYLOAD_LENGTH                  (TF_MODBUS_TCP_MAX_FRAME_LENGTH - TF_MODBUS_TCP_FRAME_IN_HEADER_LENGTH)
#define TF_MODBUS_TCP_MIN_READ_COIL_COUNT                 1u
#define TF_MODBUS_TCP_MAX_READ_COIL_COUNT                 2000u
#define TF_MODBUS_TCP_MIN_READ_COIL_BYTE_COUNT            1u
#define TF_MODBUS_TCP_MAX_READ_COIL_BYTE_COUNT            ((TF_MODBUS_TCP_MAX_READ_COIL_COUNT + 7u) / 8u)
#define TF_MODBUS_TCP_MIN_WRITE_COIL_COUNT                1u
#define TF_MODBUS_TCP_MAX_WRITE_COIL_COUNT                1968u
#define TF_MODBUS_TCP_MIN_WRITE_COIL_BYTE_COUNT           1u
#define TF_MODBUS_TCP_MAX_WRITE_COIL_BYTE_COUNT           ((TF_MODBUS_TCP_MAX_WRITE_COIL_COUNT + 7u) / 8u)
#define TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT             1u
#define TF_MODBUS_TCP_MAX_READ_REGISTER_COUNT             125u
#define TF_MODBUS_TCP_MIN_WRITE_REGISTER_COUNT            1u
#define TF_MODBUS_TCP_MAX_WRITE_REGISTER_COUNT            123u

enum class TFModbusTCPDataType
{
    Coil,
    DiscreteInput,
    InputRegister,
    HoldingRegister,
};

const char *get_tf_modbus_tcp_data_type_name(TFModbusTCPDataType data_type);

enum class TFModbusTCPFunctionCode : uint8_t
{
    ReadCoils              = 1,
    ReadDiscreteInputs     = 2,
    ReadHoldingRegisters   = 3,
    ReadInputRegisters     = 4,
    WriteSingleCoil        = 5,
    WriteSingleRegister    = 6,
    WriteMultipleCoils     = 15,
    WriteMultipleRegisters = 16,
};

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wattributes"
#endif

union TFModbusTCPHeader
{
    struct [[gnu::packed]] {
        uint16_t transaction_id;
        uint16_t protocol_id;
        uint16_t frame_length;
        uint8_t unit_id;
    };
    uint8_t bytes[TF_MODBUS_TCP_HEADER_LENGTH];
};

#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

union TFModbusTCPRequestPayload
{
    struct [[gnu::packed]] {
        uint8_t function_code;
        uint16_t start_address;  // Read Coils (1),
                                 // Read Discrete Inputs (2),
                                 // Read Holding Registers (3),
                                 // Read Input Registers(4),
                                 // Write Single Coil (5),
                                 // Write Single Register (6),
                                 // Write Multiple Coils (15),
                                 // Write Multiple Registers (16)
        union {
            uint16_t data_count; // Read Coils (1),
                                 // Read Discrete Inputs (2),
                                 // Read Holding Registers (3),
                                 // Read Input Registers (4),
                                 // Write Multiple Coils (15),
                                 // Write Multiple registers (16)
            uint16_t data_value; // Write Single Coil (5),
                                 // Write Single Register (6)
        };
        uint8_t byte_count;      // Write Multiple Coils (15),
                                 // Write Multiple Registers (16)
        union {
            uint8_t coil_values[TF_MODBUS_TCP_MAX_WRITE_COIL_BYTE_COUNT];     // Write Multiple Coils (15),
            uint16_t register_values[TF_MODBUS_TCP_MAX_WRITE_REGISTER_COUNT]; // Write Multiple Registers (16)
        };
    };
    uint8_t bytes[TF_MODBUS_TCP_MAX_PAYLOAD_LENGTH];
};

union TFModbusTCPRequest
{
    struct [[gnu::packed]] {
        TFModbusTCPHeader header;
        TFModbusTCPRequestPayload payload;
    };
    uint8_t bytes[TF_MODBUS_TCP_HEADER_LENGTH + TF_MODBUS_TCP_MAX_PAYLOAD_LENGTH];
};

union TFModbusTCPResponsePayload
{
    struct [[gnu::packed]] {
        uint8_t function_code;
        union {
            struct [[gnu::packed]] {
                union {
                    uint8_t exception_code;
                    uint8_t byte_count;  // Read Coils (1),
                                         // Read Discrete Inputs (2),
                                         // Read Holding Registers (3),
                                         // Read Input Registers (4)
                };
                union {
                    uint8_t coil_values[TF_MODBUS_TCP_MAX_READ_COIL_BYTE_COUNT];     // Read Coils (1),
                                                                                     // Read Discrete Inputs (2)
                    uint16_t register_values[TF_MODBUS_TCP_MIN_READ_REGISTER_COUNT]; // Read Holding Registers (3),
                                                                                     // Read Input Registers (4)
                };
            };
            struct [[gnu::packed]] {
                uint16_t start_address;  // Write Single Coil (5),
                                         // Write Single Register (6),
                                         // Write Multiple Coils (15)
                union {
                    uint16_t data_value; // Write Single Coil (5),
                                         // Write Single Register (6)
                    uint16_t data_count; // Write Multiple Coils (15),
                                         // Write Multiple Registers (16)
                };
            };
        };
    };
    uint8_t bytes[TF_MODBUS_TCP_MAX_PAYLOAD_LENGTH];
};
