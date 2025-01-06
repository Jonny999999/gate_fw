#include "modbus.h"

static const char *TAG = "Modbus-RTU";

// UART config
#define UART_NUM UART_NUM_1
#define TXD_PIN CONFIG_RS485_TX_GPIO // TX pin
#define RXD_PIN CONFIG_RS485_RX_GPIO // RX pin
#define RTS_PIN CONFIG_RS485_DIR_GPIO // DE pin
#define BAUD_RATE CONFIG_BAUD_RATE
#define BUF_SIZE 256

// configure to invert RX(in), TX(out) or DIR(out) signals
#define INVERT_RX_SIGNAL CONFIG_RS485_RX_INVERTED
#define INVERT_TX_SIGNAL 0
#define INVERT_DIR_SIGNAL 0 //RTS

void modbus_init(){
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, RTS_PIN, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(UART_NUM, UART_MODE_RS485_HALF_DUPLEX));
// invert out pins (due to mosfet level shifter)
    ESP_ERROR_CHECK(uart_set_line_inverse(
        UART_NUM,
        (INVERT_RX_SIGNAL ? UART_SIGNAL_RXD_INV : 0) |
        (INVERT_DIR_SIGNAL ? UART_SIGNAL_RTS_INV : 0) |
        (INVERT_TX_SIGNAL ? UART_SIGNAL_TXD_INV : 0)
    ));

    ESP_LOGW(TAG, "Modbus communication initialized");
}



// Function to calculate Modbus CRC16
uint16_t modbus_crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}



// Function to send a Modbus RTU frame
// returns 0: success, -1: uart sending failed, -2: incorrect response from vfd
int send_modbus_command(uint8_t slave_addr, uint8_t function_code, uint16_t reg_addr, uint16_t value)
{
    int len;
    ESP_LOGD(TAG, "send_modbus_command: addr %d, func: %d, regAddr: %d, value: %d", slave_addr, function_code, reg_addr, value);

    // create frame
    uint8_t frame[8];
    frame[0] = slave_addr;             // Slave address
    frame[1] = function_code;          // Function code (Write single register)
    frame[2] = (reg_addr >> 8) & 0xFF; // Register address high byte
    frame[3] = reg_addr & 0xFF;        // Register address low byte
    frame[4] = (value >> 8) & 0xFF;    // Value high byte
    frame[5] = value & 0xFF;           // Value low byte
    uint16_t crc = modbus_crc16(frame, 6);
    frame[6] = crc & 0xFF;        // CRC low byte
    frame[7] = (crc >> 8) & 0xFF; // CRC high byte

    // log created frame
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG)
    {
        printf("[%s] Sending frame with %d bytes - hex:", TAG, 8);
        for (int i = 0; i < 8; i++)
        {
            printf("%02X ", frame[i]);
        }
        printf("\n");
    }

    // clear uart buffer in case previous responses were stored in the meantime
    uart_flush(UART_NUM);

    // send frame
    len = uart_write_bytes(UART_NUM, (const char *)frame, sizeof(frame));
    if (len == -1)
    {
        ESP_LOGE(TAG, "failed sending frame via UART");
        return -1;
    }
    ESP_LOGD(TAG, "sent %d bytes via uart", len);

    // Receive response
    uint8_t response[8];
    len = uart_read_bytes(UART_NUM, response, sizeof(response), pdMS_TO_TICKS(100));
    // log response
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG)
    {
        printf("[%s] Received %d bytes after WRITE - hex:", TAG, len);
        for (int i = 0; i < len; i++)
        {
            printf("%02X ", response[i]);
        }
        printf("\n");
    }

    // verify that response is correct
    // the vfd echos the same request back with 2 additional bytes
    // e.g. request: "01 06 00 02 00 64 29 E1 "   response: "01 06 00 02 00 64 29 E1 00"
    if (memcmp(frame, response, 8) == 0)
    {
        ESP_LOGD(TAG, "Response matches the request. Command successful.");
        return 0;
    }
    else
    {
        ESP_LOGE(TAG, "send_modbus_command: Response does not match the request. Communication error!");
        return -2;
    }
}




// Function to send a Modbus RTU request and receive response
int read_modbus_register(uint8_t slave_addr, uint16_t reg_addr, uint16_t *value)
{
    int len;
    ESP_LOGD(TAG, "read_modbus_register: addr %d, regAddr: %d", slave_addr, reg_addr);

    //create frame for read command
    uint8_t request[8];
    request[0] = slave_addr;             // Slave address
    request[1] = 0x03;                   // Function code (Read holding register)
    request[2] = (reg_addr >> 8) & 0xFF; // Register address high byte
    request[3] = reg_addr & 0xFF;        // Register address low byte
    request[4] = 0x00;                   // Number of registers high byte
    request[5] = 0x01;                   // Number of registers low byte
    uint16_t crc = modbus_crc16(request, 6);
    request[6] = crc & 0xFF;        // CRC low byte
    request[7] = (crc >> 8) & 0xFF; // CRC high byte

    // log created frame
    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG)
    {
        printf("[%s] Sending frame with %d bytes - hex:", TAG, 8);
        for (int i = 0; i < 8; i++)
        {
            printf("%02X ", request[i]);
        }
        printf("\n");
    }

    // Clear the UART buffer
    // clear previous responses e.g. from command
    uart_flush(UART_NUM);

    // send frame
    len = uart_write_bytes(UART_NUM, (const char *)request, sizeof(request));
    if (len == -1)
    {
        ESP_LOGE(TAG, "failed sending frame via UART");
        return -1;
    }
    ESP_LOGD(TAG, "Sent Modbus read request (%d bytes)", len);

    // receive response
    uint8_t response[7];
    ESP_LOGV(TAG, "reading response...");
    len = uart_read_bytes(UART_NUM, response, sizeof(response), pdMS_TO_TICKS(100));

    if (esp_log_level_get(TAG) >= ESP_LOG_DEBUG)
    {
        printf("[%s] Received %d bytes after READ request - hex:", TAG, len);
        for (int i = 0; i < len; i++)
        {
            printf("%02X ", response[i]);
        }
        printf("\n");
    }

    // Validate response
    // 1. check if reponse has the expected size
     if (len != 7) {
        ESP_LOGE(TAG, "Failed to read Modbus response, response length is: %d, expected %d", len, 7);
        return -1; // Error
    }

    // 2, crc must be correct for the received data
    uint16_t response_crc = (response[len - 1] << 8) | response[len - 2];
    if (modbus_crc16(response, len - 2) != response_crc)
    {
        ESP_LOGE(TAG, "CRC error in Modbus response");
        return -2; // CRC error
    }

    // 3. function code must be correct ("Read Holding Registers (0x03)")
    if (response[1] != 0x03)
    {
        ESP_LOGE(TAG, "Invalid function code in response: 0x%02X", response[1]);
        return -3; // Invalid function code
    }

    // extract the register value
    *value = (response[3] << 8) | response[4];
    ESP_LOGD(TAG, "Read register value: %d", *value);
    return 0; // Success
}
