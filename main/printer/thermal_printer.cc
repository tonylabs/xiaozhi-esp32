#include "thermal_printer.h"

#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr char TAG[] = "ThermalPrinter";
constexpr size_t kUartRxBufferSize = 1024;
constexpr size_t kUartTxBufferSize = 0;

constexpr uint8_t kCmdInit[] = {0x1B, 0x40};
constexpr uint8_t kCmdSelfTest[] = {0x12, 0x54};
constexpr uint8_t kCmdCheckPaper[] = {0x10, 0x04, 0x01};
constexpr uint8_t kCmdSetBaudPrefix[] = {0x1F, 0x2D, 0x55, 0x01}; // Last byte is the baud index (m)
constexpr uint8_t kCmdSerialOpen[] = {0x1F, 0x77, 0x00};
constexpr uint8_t kCmdSerialClose[] = {0x1F, 0x77, 0x01};
} // namespace

ThermalPrinter::ThermalPrinter(ThermalPrinterModel type,
                               uart_port_t uart_port,
                               gpio_num_t tx_pin,
                               gpio_num_t rx_pin,
                               gpio_num_t dtr_pin,
                               int baud_rate)
    : type_(type),
      uart_port_(uart_port),
      tx_pin_(tx_pin),
      rx_pin_(rx_pin),
      dtr_pin_(dtr_pin),
      baud_rate_(baud_rate),
      initialized_(false) {}

ThermalPrinter::~ThermalPrinter() {
    if (initialized_) {
        uart_driver_delete(uart_port_);
    }
}

esp_err_t ThermalPrinter::ConfigureUart() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = baud_rate_;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_param_config(uart_port_, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_set_pin(uart_port_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_driver_install(uart_port_, kUartRxBufferSize, kUartTxBufferSize, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t ThermalPrinter::ConfigureDtr() {
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = 1ULL << dtr_pin_;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(dtr_pin_, 1);
    return ESP_OK;
}

esp_err_t ThermalPrinter::WriteCommand(const uint8_t* cmd, size_t length) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    const int written = uart_write_bytes(uart_port_, reinterpret_cast<const char*>(cmd), length);
    if (written < 0 || static_cast<size_t>(written) != length) {
        ESP_LOGE(TAG, "uart_write_bytes failed, written=%d", written);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ThermalPrinter::Init() {
    esp_err_t err = ConfigureUart();
    if (err != ESP_OK) {
        return err;
    }

    err = ConfigureDtr();
    if (err != ESP_OK) {
        return err;
    }

    initialized_ = true;
    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return err;
}

esp_err_t ThermalPrinter::SelfTest() {
    esp_err_t err = WriteCommand(kCmdSerialOpen, sizeof(kCmdSerialOpen));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err != ESP_OK) {
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    err = WriteCommand(kCmdSelfTest, sizeof(kCmdSelfTest));

    // Close the session to avoid printer-side sleep/lock
    WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
    return err;
}

esp_err_t ThermalPrinter::PrintText(const std::string& text, bool append_newline) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    // Open UART session on printer side
    esp_err_t err = WriteCommand(kCmdSerialOpen, sizeof(kCmdSerialOpen));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // Ensure printer is initialized before printing
    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err != ESP_OK) {
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    std::string payload = text;

    if (append_newline) {
        payload.append("\r\n");
    }

    const int written = uart_write_bytes(uart_port_, payload.data(), payload.size());
    if (written < 0 || static_cast<size_t>(written) != payload.size()) {
        ESP_LOGE(TAG, "Failed to write text to printer, written=%d", written);
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return ESP_FAIL;
    }

    // Give the printer a brief moment to process the buffer, then close the session
    uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(100));
    err = WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to close printer UART session: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

esp_err_t ThermalPrinter::SendRaw(const uint8_t* data, size_t length) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (data == nullptr || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const int written = uart_write_bytes(uart_port_, reinterpret_cast<const char*>(data), length);
    if (written < 0 || static_cast<size_t>(written) != length) {
        ESP_LOGE(TAG, "Failed to send raw data, written=%d", written);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t ThermalPrinter::SetBaudRateIndex(uint8_t m_value) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd[sizeof(kCmdSetBaudPrefix) + 1];
    memcpy(cmd, kCmdSetBaudPrefix, sizeof(kCmdSetBaudPrefix));
    cmd[sizeof(kCmdSetBaudPrefix)] = m_value;

    return WriteCommand(cmd, sizeof(cmd));
}

esp_err_t ThermalPrinter::QueryPaperStatus(bool& paper_present) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = WriteCommand(kCmdCheckPaper, sizeof(kCmdCheckPaper));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t resp[3] = {0};
    const int len = uart_read_bytes(uart_port_, resp, sizeof(resp), pdMS_TO_TICKS(200));
    if (len != static_cast<int>(sizeof(resp))) {
        ESP_LOGW(TAG, "Paper status read timeout/short read, len=%d", len);
        return ESP_ERR_TIMEOUT;
    }

    if (resp[0] == 0xFE && resp[1] == 0x23 && resp[2] == 0x12) {
        paper_present = true;
        return ESP_OK;
    }

    if (resp[0] == 0xEF && resp[1] == 0x23 && resp[2] == 0x1A) {
        paper_present = false;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unexpected paper status response: %02X %02X %02X", resp[0], resp[1], resp[2]);
    return ESP_ERR_INVALID_RESPONSE;
}

