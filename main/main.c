#include "board.h"
#include "time.h"
#include "ble_mesh_config_edge.h"
#include "../Secret/NetworkConfig.h"

#define TAG_M "MAIN"
#define TAG_ALL "*"

uint16_t node_own_addr = 0;

static void prov_complete_handler(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) {
    ESP_LOGI(TAG_M, " ----------- prov_complete handler trigered -----------");
    setNodeState(CONNECTED);
    loop_message_connection();
}

static void config_complete_handler(uint16_t addr) {
    ESP_LOGI(TAG_M,  " ----------- Node-0x%04x config_complete -----------", addr);
    node_own_addr = addr;
    uart_sendMsg(0, " ----------- config_complete -----------");
}

static void recv_message_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_message handler trigered -----------");
    uint16_t node_addr = ctx->addr;
    ESP_LOGW(TAG_M, "-> Received Message \'%s\' from node-%d", (char*)msg_ptr, node_addr);

    // recived a ble-message from edge ndoe
    // ========== potential special case ==========
    if (strncmp((char*)msg_ptr, "Special Case", 12) == 0) {
        // place holder for special case that need to be handled in esp-root module
        // handle locally
        char response[5] = "S";
        uint16_t response_length = strlen(response);
        send_response(ctx, response_length, (uint8_t*) response);
        ESP_LOGW(TAG_M, "<- Sended Response \'%s\'", (char*) response);
        return; // or continue to execute
    }

    // ========== General case, pass up to APP level ==========
    // pass node_addr & data to to edge device using uart
    uart_sendData(node_addr, msg_ptr, length);

    // send response
    char response[5] = "S";
    uint16_t response_length = strlen(response);
    send_response(ctx, response_length, (uint8_t *) response);
    ESP_LOGW(TAG_M, "<- Sended Response \'%s\'", (char*) response);
}

static void recv_response_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    // ESP_LOGI(TAG_M, " ----------- recv_response handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Received Response [%s]\n", (char*)msg_ptr);

    // //message went through, reset the timer
    // setTimeout(false);
}

static void timeout_handler(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) {
    ESP_LOGI(TAG_M, " ----------- timeout handler trigered -----------");

    // Print the current value of timeout
    // bool currentTimeout = getTimeout();
    // ESP_LOGI(TAG_M, " Current timeout value: %s", currentTimeout ? "true" : "false");

    // if(!currentTimeout) {
    //     ESP_LOGI(TAG_M, " Timer is starting to count down ");
    //     startTimer();
    //     setTimeout(true);
    // }
    // else if(getTimeElapsed() > 20.0) // that means timeout already happened once -- and if timeout persist for 20 seconds then reset itself.
    // {
    //     setNodeState(DISCONNECTED);
    //     stop_timer(); //for timer_h
    //     stop_periodic_timer(); //for esp_timer
    //     ESP_LOGI(TAG_M, " Resetting the Board "); //i should make a one-hit timer just before resetting.
    //     esp_restart();
    // }
}

//Create a new handler to handle broadcasting
static void broadcast_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr)
{
    // if (ctx->addr == node_own_addr)
    // {
    //     return; // is root's own broadcast
    // }

    uint16_t node_addr = ctx->addr;
    ESP_LOGE(TAG_M, "-> Received Broadcast Message \'%s\' from node-%d", (char *)msg_ptr, node_addr);

    // ========== General case, pass up to APP level ==========
    // pass node_addr & data to to edge device using uart
    send_message(ctx->addr, length, msg_ptr); // testing log
    uart_sendData(node_addr, msg_ptr, length);
    ESP_LOGE(TAG_M, "=== Handled Broadcast Message \'%s\' from node-%d ===", (char *)msg_ptr, node_addr);
}

static void connectivity_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    ESP_LOGI(TAG_M, "Checking connectivity\n");

    static uint8_t *data_buffer = NULL;
    if (data_buffer == NULL) {
        data_buffer = (uint8_t*)malloc(128);
        if (data_buffer == NULL) {
            printf("Memory allocation failed.\n");
            return;
        }
    }
    
    ESP_LOGI(TAG_M, "----------- (send_message) iniate message for checking purposes -----------");
    strcpy((char*)data_buffer, "Are we still connected, from edge");
    send_message(ctx->addr, strlen("Are we still connected, from edge") + 1, data_buffer);
    ESP_LOGW(TAG_M, "<- Sended Message [%s]", (char*)data_buffer);

    return;
}

static void execute_uart_command(char* command, size_t cmd_len) {
    // size_t cmd_len_raw = cmd_len;

    // ESP_LOGI(TAG_M, "execute_command called - %d byte raw - %d decoded byte", cmd_len_raw, cmd_len);
    // uart_sendMsg(0, "Executing command\n");

    static const char *TAG_E = "EXE";
    // static uint8_t *data_buffer = NULL;
    // if (data_buffer == NULL) {
    //     data_buffer = (uint8_t*)malloc(128);
    //     if (data_buffer == NULL) {
    //         printf("Memory allocation failed.\n");
    //         return;
    //     }
    // }

    // ============= process and execute commands from net server (from uart) ==================
    // uart command format
    // TB Finish, TB Complete
    if (cmd_len < 5) {
        // ESP_LOGE(TAG_E, "Command [%s] with %d byte too short", command, cmd_len);
        // uart_sendMsg(0, "Command too short\n");
        return;
    }
    const size_t CMD_LEN = 5;
    const size_t ADDR_LEN = 2;
    const size_t MSG_SIZE_NUM_LEN = 1;

    if (strncmp(command, "SEND-", 5) == 0) {
        ESP_LOGI(TAG_E, "executing \'SEND-\'");
        char *address_start = command + CMD_LEN;
        char *msg_len_start = address_start + ADDR_LEN;
        char *msg_start = msg_len_start + MSG_SIZE_NUM_LEN;

        uint16_t node_addr = (uint16_t)((address_start[0] << 8) | address_start[1]);
        if (node_addr == 0) {
            node_addr = PROV_OWN_ADDR; // root addr
        }
        size_t msg_length = (size_t)msg_len_start[0];

        // uart_sendData(0, msg_start, msg_length); // sedn back form uart for debugging
        // uart_sendMsg(0, "-feedback \n"); // sedn back form uart for debugging
        // uart_sendMsg(0, node_addr + "--addr-feedback \n"); // sedn back form uart for debugging
        
        ESP_LOGI(TAG_E, "Sending message to address-%d ...", node_addr);
        send_message(node_addr, msg_length, (uint8_t *) msg_start);
        ESP_LOGW(TAG_M, "<- Sended Message \'%.*s\' to node-%d", msg_length, (char*) msg_start, node_addr);
    } else {
        // ESP_LOGE(TAG_E, "Command not Vaild");
    }
    
    // ESP_LOGI(TAG_E, "Command [%.*s] executed", cmd_len, command);
}

// static void print_bytes(char* data, size_t length) {
//     for(int i = 0; i< length; ++i) {
        
//     }
// }

static void uart_task_handler(char *data) {
    ESP_LOGW(TAG_M, "uart_task_handler called ------------------");

    int cmd_start = 0;
    int cmd_end = 0;
    int cmd_len = 0;

    for (int i = 0; i < UART_BUF_SIZE; i++) {
        if (data[i] == 0xFF) {
            // located start of message
            cmd_start = i + 1; // start byte of actual message
        }else if (data[i] == 0xFE) {
            // located end of message
            cmd_end = i; // 0xFE byte
            // uart_sendMsg(0, "Found End of Message!!\n");
        }

        if (cmd_end > cmd_start) {
            // located a message, message at least 1 byte
            uint8_t* command = (uint8_t *) (data + cmd_start);
            cmd_len = cmd_end - cmd_start;
            cmd_len = uart_decoded_bytes(command, cmd_len, command); // decoded cmd will be put back to command pointer
            // ESP_LOGE("Decoded Data", "i:%d, cmd_start:%d, cmd_len:%d", i, cmd_start, cmd_len);

            execute_uart_command(data + cmd_start, cmd_len); // TB Finish, don't execute at the moment
            cmd_start = cmd_end;
        }
    }

    if (cmd_start > cmd_end) {
        // one message is only been read half into buffer, edge case. Not consider at the moment
        // ESP_LOGE("E", "Buffer might have remaining half message!! cmd_start:%d, cmd_end:%d", cmd_start, cmd_end);
        // uart_sendMsg(0, "[Error] Buffer might have remaining half message!!\n");
    }
}

// TB Finish, do we need to make sure there isn't haf of message left in buffer
// when read entire bufffer, no message got read as half, or metho to recover it? 
static void rx_task(void *arg)
{
    static const char *RX_TASK_TAG = "RX";
    uint8_t* data = (uint8_t*) malloc(UART_BUF_SIZE + 1);
    ESP_LOGW(RX_TASK_TAG, "rx_task called ------------------");
    // esp_log_level_set(TAG_ALL, ESP_LOG_NONE);

    while (1) {
        memset(data, 0, UART_BUF_SIZE);
        const int rxBytes = uart_read_bytes(UART_NUM, data, UART_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            // uart_sendMsg(rxBytes, " readed from RX\n");

            uart_task_handler((char*) data);
        }
    }
    free(data);
}

void app_main(void)
{
    // turn off log - important, bc the server counting on '[E]' as end of message instaed of '\0'
    //              - since the message from uart carries data
    //              - use uart_sendMsg or uart_sendData for message, the esp_log for dev debug
    // esp_log_level_set(TAG_ALL, ESP_LOG_NONE);
    // uart_sendMsg(0, "[Ignore_prev][UART] Turning off all Log's from esp_log\n");

    ESP_LOGI(TAG_M, " ----------- staringing -----------");
    esp_err_t err = esp_module_edge_init(prov_complete_handler, config_complete_handler, recv_message_handler, recv_response_handler, timeout_handler, broadcast_handler, connectivity_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_M, "Network Module Initialization failed (err %d)", err);
        return;
    }
    board_init();
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);

    ESP_LOGI(TAG_M, " ----------- app_main done -----------");
    // uart_sendMsg(0, "[UART] ----------- app_main done -----------\n");
}