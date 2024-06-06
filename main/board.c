/* board.c - Board-specific hooks */

/*
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include "esp_log.h"
#include "iot_button.h"
#include <string.h>
#include <time.h>
#include "board.h"

#ifdef LOCAL_EDGE_DEVICE_ENABLED
#include "local_edge_device.c"
#endif

#define TAG_B "BOARD"
#define TAG_W "Debug"

extern void send_message(uint16_t dst_address, uint16_t length, uint8_t *data_ptr, bool require_response);
extern void printNetworkInfo();

clock_t start_time;
bool timeout = false;

void startTimer() {
    start_time = clock();
}

void setTimeout(bool boolean) {
    timeout = boolean;
}

double getTimeElapsed() {
    clock_t end_time = clock();
    return ((double) (end_time - start_time)) / CLOCKS_PER_SEC;
}

bool getTimeout() {
    return timeout;
}

void setLEDState(enum State nodeState) {
    if(nodeState == DISCONNECTED) {
        board_led_operation(10, 0, 0); // Red LED Color
    }
    else if (nodeState == CONNECTING) {
        board_led_operation(0, 0, 10); // Blue LED Color
    }
    else if (nodeState ==  CONNECTED) {
        board_led_operation(0, 10, 0); // Green LED Color
    }
    else if (nodeState == WORKING) {
        board_led_operation(10, 10, 0); // Yellow LED Color
    }
    else {
        board_led_operation(0, 0, 0); // No Color == No State
    }
}

void board_led_operation(uint8_t r, uint8_t g, uint8_t b)
{
    rmt_led_set(r,g,b);
}

static void board_led_init(void)
{
    rmt_encoder_init();
}

static void button_tap_cb(void* arg)
{
    ESP_LOGW(TAG_W, "button pressed ------------------------- ");
    // static uint8_t *data_buffer = NULL;
    // if (data_buffer == NULL) {
    //     data_buffer = (uint8_t*)malloc(128);
    //     if (data_buffer == NULL) {
    //         printf("Memory allocation failed.\n");
    //         return;
    //     }
    // }

    // strcpy((char*)data_buffer, "Broadcast sent");
    // send_broadcast(strlen("Broadcast sent") + 1, data_buffer);

    // static int control = 1;
    // // if (control == 0) {
    // //     // TSTITEST0
    // //     ESP_LOGW(TAG_W, "send RST------");
    // //     char data[20] = "RST";
    // //     uart_sendData(0, (uint8_t*) data, strlen(data));
    // //     ESP_LOGW(TAG_W, "sended RST-------");
    // //     control = 1;
    // // }else
    // if (control == 1) {
    //     // TSTITEST0
    //     ESP_LOGW(TAG_W, "sending------");
    //     char data[20] = "TSTITEST0";
    //     uart_sendData(0, (uint8_t*) data, strlen(data));
    //     ESP_LOGW(TAG_W, "sended-------");
    //     control = 2;
    // } else {
    //     // start test
    //     ESP_LOGW(TAG_W, "sending------");
    //     char data[20] = "TSTS";
    //     uart_sendData(0, (uint8_t*) data, strlen(data));
    //     ESP_LOGW(TAG_W, "sended-------");
    //     control = 1;
    // }
    ESP_LOGW(TAG_W, "sending------");
    char data[20] = "TSTS";
    send_message(PROV_OWN_ADDR, strlen(data), (uint8_t*) data, false);
    ESP_LOGW(TAG_W, "sended-------");

}

static void board_button_init(void)
{
    button_handle_t btn_handle = iot_button_create(BUTTON_IO_NUM, BUTTON_ACTIVE_LEVEL);
    if (btn_handle) {
        iot_button_set_evt_cb(btn_handle, BUTTON_CB_RELEASE, button_tap_cb, "RELEASE");
    }
}

static void uart_init() {  // Uart ===========================================================
    const int uart_num = UART_NUM;
    const int uart_buffer_size = UART_BUF_SIZE * 2;
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl= UART_HW_FLOWCTRL_DISABLE, // = UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = UART_SCLK_DEFAULT, // = 122,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, uart_buffer_size,
                                        uart_buffer_size, 0, NULL, 0)); // not using queue
                                        // uart_buffer_size, 20, &uart_queue, 0));
    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    // Set UART pins                      (TX,      RX,      RTS,     CTS)
    ESP_ERROR_CHECK(uart_set_pin(uart_num, TXD_PIN, RXD_PIN, RTS_PIN, CTS_PIN));

    ESP_LOGI(TAG_B, "Uart init done");
}

// escape char
int uart_write_encoded_bytes(uart_port_t uart_num, uint8_t* data, size_t length) {
    uint8_t esacpe_byte = ESCAPE_BYTE;

    int byte_wrote = 0;
    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] < esacpe_byte) {
            uart_write_bytes(UART_NUM, byte_itr, 1);
            byte_wrote += 1;
            continue;
        }

        // nned 2 byte encoded
        uint8_t encoded = byte_itr[0] ^ esacpe_byte; // bitwise Xor
        uart_write_bytes(UART_NUM, &esacpe_byte, 1);
        uart_write_bytes(UART_NUM, &encoded, 1);
        byte_wrote += 2;
    }

    return byte_wrote;
}

// Able to wrote back to the same buffer, since decoded data is always shorter
int uart_decoded_bytes(uint8_t* data, size_t length, uint8_t* decoded_data) {
    int decoed_len = 0;
    uint8_t* decode_itr = decoded_data;

    for (uint8_t* byte_itr = data; byte_itr < data + length; ++byte_itr) {
        if (byte_itr[0] != ESCAPE_BYTE) {
            // not a ESCAPE_BYTE
            decode_itr[0] = byte_itr[0];
            decode_itr += 1;
            decoed_len += 1;
            continue;
        }

        // ESCAPE_BYTE, decode 2 byte into 1
        byte_itr += 1; // move to next to get encoded byte
        uint8_t encoded = byte_itr[0];
        
        uint8_t decoded = encoded ^ ESCAPE_BYTE; // bitwise Xor
        decode_itr[0] = decoded;
        decode_itr += 1;
        decoed_len += 1;
    }
    
    return decoed_len;
}


// TB Finish, need to encode the send data for escape bytes
// do we need to regulate the message length?
int uart_sendData(uint16_t node_addr, uint8_t* data, size_t length)
{
#ifdef LOCAL_EDGE_DEVICE_ENABLED
    // enabled local_edge_device, pass message to local_edge_device
    local_edge_device_network_message_handler(node_addr, data, length);
    return length;
#else
    // not enabled local_edge_device, pass message to uart with uart encoding
    uint8_t uart_start = UART_START;
    uint8_t uart_end = UART_END;
    int txBytes = 0;

    uint16_t node_addr_big_endian = htons(node_addr); 
    txBytes += uart_write_bytes(UART_NUM, &uart_start, 1); // 0xFF
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) &node_addr_big_endian, 2);
    txBytes += uart_write_encoded_bytes(UART_NUM, data, length);
    txBytes += uart_write_bytes(UART_NUM, &uart_end, 1);  // 0xFE

    ESP_LOGI("[UART]", "Wrote %d bytes Data on uart-tx", txBytes);
    return txBytes;
#endif
}

// TB Finish, need to encode the send data for escape bytes
int uart_sendMsg(uint16_t node_addr, char* msg)
{
    size_t length = strlen(msg);
    uint8_t uart_start = UART_START;
    uint8_t uart_end = UART_END;
    int txBytes = 0;

    uint16_t node_addr_big_endian = htons(node_addr); 
    txBytes += uart_write_bytes(UART_NUM, &uart_start, 1); // 0xFF
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) &node_addr_big_endian, 2);
    txBytes += uart_write_encoded_bytes(UART_NUM, (uint8_t*) msg, length);
    txBytes += uart_write_bytes(UART_NUM, &uart_end, 1);  // 0xFE

    ESP_LOGI("[UART]", "Wrote %d bytes Msg on uart-tx", txBytes);
    return txBytes;
}

void board_init(void)
{
    uart_init();
    board_led_init();
    board_button_init();

#ifdef LOCAL_EDGE_DEVICE_ENABLED
    // enabled local_edge_device, initialize the local device
    local_edge_device_init();
#endif
}
