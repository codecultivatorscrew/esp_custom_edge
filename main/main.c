#include "board.h"
#include "time.h"
#include "ble_mesh_config_edge.h"
#include "../Secret/NetworkConfig.h"

#define TAG_M "MAIN"
#define TAG_ALL "*"
// network command length - 5 byte
#define CMD_SEND_MSG "SEND-"
#define CMD_BROADCAST_MSG "BCAST"
#define CMD_RESET_EDGE "RST-E"

uint16_t node_own_addr = 0;

/***************** Event Handler *****************/
// prov_complete_handler() get triger when a new node is provitioned to the network
static void prov_complete_handler(uint16_t node_index, const esp_ble_mesh_octet16_t uuid, uint16_t addr, uint8_t element_num, uint16_t net_idx) {
    ESP_LOGI(TAG_M, " ----------- prov_complete handler trigered -----------");
    setNodeState(CONNECTING);
}

// config_complete_handler() get triger when a new node is configured successfully after provitioning
static void config_complete_handler(uint16_t addr) {
    ESP_LOGI(TAG_M,  " ----------- Node-0x%04x config_complete -----------", addr);
    node_own_addr = addr;
    setNodeState(CONNECTED);
    // pinging Root checking connectivity
    #if HEARTBEAT_TIMER
        loop_message_connection();
    #endif
    uart_sendMsg(0, "[E] Module Configured");
}

// recv_message_handler() get triger when module recived an message
static void recv_message_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr, uint32_t opcode) {
    // ESP_LOGI(TAG_M, " ----------- recv_message handler trigered -----------");
    uint16_t node_addr = ctx->addr;
    ESP_LOGW(TAG_M, "-> Received Message \'%*s\' from node-%d", length, (char *)msg_ptr, node_addr);
    setTimeout(false); // clear edge reset timeout
    // stop_timer();

    // recived a ble-message from edge ndoe
    uart_sendData(node_addr, msg_ptr, length);

    // clear edge reset timeout
    #if TIMEOUT_TIMER
        setTimeout(false);
    #endif
    // stop_timer();

    // check if needs an response to confirm recived
    if (opcode == ECS_193_MODEL_OP_MESSAGE) {
        // only normal message no need for response
        return;
    }

    // send response
    char response[5] = "S";
    uint16_t response_length = strlen(response);
    send_response(ctx, response_length, (uint8_t *)response, opcode);
    ESP_LOGW(TAG_M, "<- Sended Response %d bytes \'%*s\'", response_length, response_length, (char *)response);
}

// recv_response_handler() get triger when module recived an response to previouse sent message that requires an response
static void recv_response_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr, uint32_t opcode) {
    // ESP_LOGI(TAG_M, " ----------- recv_response handler trigered -----------");
    ESP_LOGW(TAG_M, "-> Received Response %d bytes [%*s]\n", length , length, (char *)msg_ptr);

    // message went through, clear edge reset timeout
    #if TIMEOUT_TIMER
        setTimeout(false);
    #endif
    // stop_timer();

    // clear confirmed recived important message
    int8_t index = get_important_message_index(opcode);
    if (index != -1) {
        // resend the important message
        ESP_LOGW(TAG_M, "Confirm delivered on Important Message, index: %d", index);
        clear_important_message(index);
    }
}

// timeout_handler() get triger when module previously sent an message that requires response but didn't receive response
static void timeout_handler(esp_ble_mesh_msg_ctx_t *ctx, uint32_t opcode) {
    ESP_LOGI(TAG_M, " ----------- timeout handler trigered -----------");

    // cehck for retransmition
    int8_t index = get_important_message_index(opcode);
    if (index != -1) {
        // resend the important message
        retransmit_important_message(ctx, opcode, index);
    }

    // check for edge restart when mutiple timeout happened
    // Print the current value of timeout
    #if TIMEOUT_TIMER
        handleConnectionTimeout();
    #endif
}

// broadcast_handler() get triger when module recived an broadcast message
static void broadcast_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr)
{
    if (ctx->addr == node_own_addr)
    {
        return; // is edge's own broadcast
    }

    uint16_t node_addr = ctx->addr;
    ESP_LOGE(TAG_M, "-> Received Broadcast Message \'%*s\' from node-%d", length, (char *) msg_ptr, node_addr);

    // ========== General case, pass up to APP level ==========
    // pass node_addr & data to to edge device using uart
    uart_sendData(node_addr, msg_ptr, length);
}

// connectivity_handler() get triger when module recived an connectivity check message (heartbeat message)
static void connectivity_handler(esp_ble_mesh_msg_ctx_t *ctx, uint16_t length, uint8_t *msg_ptr) {
    ESP_LOGI(TAG_M, "----- Connectivity Handler Triggered -----\n");

    char response[3] = "S";
    uint16_t response_length = strlen(response);
    send_response(ctx, response_length, (uint8_t *)response, ECS_193_MODEL_OP_CONNECTIVITY);
}

/***************** Other Functions *****************/
static void execute_uart_command(char *command, size_t cmd_total_len) {
    // size_t cmd_len_raw = cmd_len;

    // ESP_LOGI(TAG_M, "execute_command called - %d byte raw - %d decoded byte", cmd_len_raw, cmd_len);
    // uart_sendMsg(0, "Executing command\n");

    static const char *TAG_E = "EXE";
    static uint8_t *data_buffer = NULL;
    if (data_buffer == NULL) {
        data_buffer = (uint8_t*)malloc(128);
        if (data_buffer == NULL) {
            printf("Memory allocation failed.\n");
            return;
        }
    }

    // ============= process and execute commands from net server (from uart) ==================
    // uart command format
    // TB Finish, TB Complete
    if (cmd_total_len < 5) {
        ESP_LOGE(TAG_E, "Command [%s] with %d byte too short", command, cmd_total_len);
        return;
    }

    // ====== core commands ======
    if (strncmp(command, CMD_SEND_MSG, CMD_LEN) == 0) {
        ESP_LOGI(TAG_E, "executing \'SEND-\'");
        char *address_start = command + CMD_LEN;
        char *msg_start = address_start + NODE_ADDR_LEN;
        size_t msg_length = cmd_total_len - CMD_LEN - NODE_ADDR_LEN;

        if (cmd_total_len < CMD_LEN + NODE_ADDR_LEN) {
            uart_sendMsg(0, "Error: No Dst Address Attached\n");
            return;
        }
        else if (msg_length <= 0) {
            uart_sendMsg(0, "Error: No Message Attached\n");
            return;
        }

        uint16_t node_addr_network_order = (uint16_t)((address_start[0] << 8) | address_start[1]);
        uint16_t node_addr = ntohs(node_addr_network_order);
        if (node_addr == 0) {
            node_addr = PROV_OWN_ADDR; // root addr
        }
        
        ESP_LOGI(TAG_E, "Sending message to address-%d ...", node_addr);
        send_message(node_addr, msg_length, (uint8_t *) msg_start, false);
        ESP_LOGW(TAG_M, "<- Sended Message \'%.*s\' to node-%d", msg_length, (char*) msg_start, node_addr);
    }
    else if (strncmp(command, CMD_BROADCAST_MSG, CMD_LEN) == 0) {
        ESP_LOGI(TAG_E, "executing \'BCAST\'");
        char *msg_start = command + CMD_LEN + NODE_ADDR_LEN;
        size_t msg_length = cmd_total_len - CMD_LEN - NODE_ADDR_LEN;

        broadcast_message(msg_length, (uint8_t *)msg_start);
    } 
    else if (strncmp(command, CMD_RESET_EDGE, CMD_LEN) == 0) {
        // restart edge module
        setNodeState(DISCONNECTED);
        reset_edge();
    }
    // else if (strncmp(command, "CLEAN", 5) == 0)
    // {
    //     ESP_LOGI(TAG_E, "executing \'CLEAN\'");
    //     uart_sendMsg(0, " - Reseting Module Persistant Memory\n");
    //     reset_esp32();
    // }

    // ====== other dev/debug use command ====== 
    // else if (strncmp(command, "ECHO-", 5) == 0) {
    //     // echo test
    //     ESP_LOGW(TAG_M, "recived \'ECHO-\' command");
    //     strcpy((char*) data_buffer, command);
    //     strcpy(((char*) data_buffer) + strlen(command), "; [ESP] confirm recived from uart; \n");
    //     uart_sendData(0, data_buffer, strlen((char* ) data_buffer) + 1);
    // }

    // ====== Not Supported  command ======
    else {
        ESP_LOGE(TAG_E, "Command not Vaild");
    }

    ESP_LOGI(TAG_E, "Command [%.*s] executed", cmd_total_len, command);
}

void execute_network_command(char *command, size_t cmd_total_len) {
#if LOCAL_EDGE_DEVICE
    // meant to be only used by local_edge_device
    execute_uart_command(command, cmd_total_len);
#endif
}

static void uart_task_handler(char *data) {
    ESP_LOGW(TAG_M, "uart_task_handler called ------------------");

    int cmd_start = 0;
    int cmd_end = 0;
    int cmd_len = 0;

    for (int i = 0; i < UART_BUF_SIZE; i++) {
        if (data[i] == 0xFF) {
            // located start of message
            cmd_start = i + 1; // start byte of actual message
        } else if (data[i] == 0xFE) {
            // located end of message
            cmd_end = i; // 0xFE byte
            // uart_sendMsg(0, "Found End of Message!!\n");
        }

        if (cmd_end > cmd_start) {
            // located a message, message at least 1 byte
            uint8_t* command = (uint8_t *) (data + cmd_start);
            cmd_len = cmd_end - cmd_start;
            cmd_len = uart_decoded_bytes(command, cmd_len, command); // decoded cmd will be put back to command pointer
            ESP_LOGE("Decoded Data", "i:%d, cmd_start:%d, cmd_len:%d", i, cmd_start, cmd_len);

            execute_uart_command(data + cmd_start, cmd_len); // TB Finish, don't execute at the moment
            cmd_start = cmd_end;
        }
    }

    if (cmd_start > cmd_end) {
        // one message is only been read half into buffer, edge case. Not consider at the moment
        ESP_LOGE("E", "Buffer might have remaining half message!! cmd_start:%d, cmd_end:%d", cmd_start, cmd_end);
        uart_sendMsg(0, "Error: Buffer might have remaining half message!!\n");
    }
}

// TB Finish, do we need to make sure there isn't haf of message left in buffer
// when read entire bufffer, no message got read as half, or metho to recover it? 
static void rx_task(void *arg)
{
    // esp_log_level_set(TAG_ALL, ESP_LOG_NONE);

    static const char *RX_TASK_TAG = "RX";
    uint8_t* data = (uint8_t*) malloc(UART_BUF_SIZE + 1);
    ESP_LOGW(RX_TASK_TAG, "rx_task called ------------------");
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
    // turn off log - Important, bc the server counting on uart escape byte 0xff and 0xfe
    //              - So need to enforce all uart signal
    //              - use uart_sendMsg or uart_sendData for message, the esp_log for dev debug
    // Edge Module is fine since is using uart pin, seperate from usb-uart logging channel
    // esp_log_level_set(TAG_ALL, ESP_LOG_NONE);
    // uart_sendMsg(0, "[UART] Turning off all Log's from esp_log\n");

    esp_err_t err = esp_module_edge_init(prov_complete_handler, config_complete_handler, recv_message_handler, recv_response_handler, timeout_handler, broadcast_handler, connectivity_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_M, "Network Module Initialization failed (err %d)", err);
        uart_sendMsg(0, "Error: Network Module Initialization failed\n");
        return;
    }
    
    board_init();
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES - 1, NULL);

    char message[15] = "[E]online\n";
    uart_sendData(0, (uint8_t *)message, strlen(message));
    // uart_sendMsg(0, "[UART] ----------- app_main done -----------\n");
}