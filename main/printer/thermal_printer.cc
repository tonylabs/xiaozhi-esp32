#include "thermal_printer.h"

#include <cstring>
#include <vector>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "settings.h"

namespace {
    constexpr char TAG[] = "ThermalPrinter";
    constexpr size_t kUartRxBufferSize = 1024;
    constexpr size_t kUartTxBufferSize = 0;
    constexpr int kPrinterPowerOnBaud = 9600;
    constexpr uint8_t kCmdInit[] = {0x1B, 0x40};
    constexpr uint8_t kCmdSelfTest[] = {0x12, 0x54};
    constexpr uint8_t kCmdCheckPaper[] = {0x10, 0x04, 0x01};
    constexpr uint8_t kCmdSetBaudPrefix[] = {0x1F, 0x2D, 0x55, 0x01}; // Last byte is the baud index (m)
    constexpr uint8_t kCmdSerialOpen[] = {0x1F, 0x77, 0x00};
    constexpr uint8_t kCmdSerialClose[] = {0x1F, 0x77, 0x01};
    constexpr uint8_t kCmdFeedLinesBase[] = {0x1F, 0x2D, 0x35, 0x04, 0x00 /*m*/, 0x00 /*k*/, 0xC8 /*tL*/, 0x00 /*tH*/};

    struct BaudRateEntry {
        int baud_rate;
        uint8_t m_index;
    };

    constexpr BaudRateEntry kSupportedBaudRates[] = {
        {1200, 0x00},
        {2400, 0x01},
        {3600, 0x02},
        {4800, 0x03},
        {7200, 0x04},
        {9600, 0x05},
        {14400, 0x06},
        {19200, 0x07},
        {28800, 0x08},
        {38400, 0x09},
        {57600, 0x0A},
        {76800, 0x0B},
        {115200, 0x0C},
        {153600, 0x0D},
        {230400, 0x0E},
        {307200, 0x0F},
        {460800, 0x10},
        {614400, 0x11},
        {921600, 0x12},
        {1228800, 0x13},
        {1843200, 0x14},
    };

    constexpr char kPrinterSettingsNs[] = "thermal_printer";
    constexpr char kBaudRateKey[] = "baud_rate";

    bool LookupBaudIndex(int baud_rate, uint8_t& index_out) {
        for (const auto& entry : kSupportedBaudRates) {
            if (entry.baud_rate == baud_rate) {
                index_out = entry.m_index;
                return true;
            }
        }
        return false;
    }

    bool IsSupportedBaudRateInternal(int baud_rate) {
        uint8_t unused = 0;
        return LookupBaudIndex(baud_rate, unused);
    }

    void PersistBaudRateToSettings(int baud_rate) {
        Settings settings(kPrinterSettingsNs, true);
        settings.SetInt(kBaudRateKey, baud_rate);
    }
}

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
    // Start at printer power-on baud to send the baud-change command, then switch to target baud.
    ESP_LOGI(TAG, "Init start: target baud=%d power-on baud=%d", baud_rate_, kPrinterPowerOnBaud);
    int target_baud = baud_rate_;
    baud_rate_ = kPrinterPowerOnBaud;

    esp_err_t err = ConfigureUart();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "UART configured at %d for baud negotiation", baud_rate_);

    err = ConfigureDtr();
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "DTR configured/high");

    initialized_ = true; // Enable WriteCommand/SetBaudRate

    // Always verify printer baud against board config; reprogram printer + host if different.
    if (target_baud != baud_rate_) {
        ESP_LOGI(TAG, "Verifying printer baud -> %d", target_baud);
        err = SetBaudRate(target_baud, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set printer baud to %d: %s", target_baud, esp_err_to_name(err));
            return err;
        }
    }

    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Printer init sequence sent at baud %d", baud_rate_);
    }

    return err;
}

esp_err_t ThermalPrinter::SelfTest() {
    // Ensure DTR is HIGH before self-test (per RULE.md: DTR should be HIGH for printer communication)
    gpio_set_level(dtr_pin_, 1);
    
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

    // Ensure DTR is HIGH before printing (per RULE.md: DTR should be HIGH for printer communication)
    gpio_set_level(dtr_pin_, 1);

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

esp_err_t ThermalPrinter::PrintImageRgb565(const uint16_t* data, int width, int height, int stride_pixels) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || width <= 0 || height <= 0 || stride_pixels < width) {
        return ESP_ERR_INVALID_ARG;
    }

    // Ensure DTR is HIGH before printing (per RULE.md: DTR should be HIGH for printer communication)
    gpio_set_level(dtr_pin_, 1);

    // Limit width to 384-dot head per printer specification (1 ≤ width ≤ 384)
    const int max_width = 384;
    const int min_width = 1;
    int target_width = (width > max_width) ? max_width : width;
    if (target_width < min_width) {
        target_width = min_width;
    }
    const int target_height = (width > 0) ? std::max(1, (height * target_width) / width) : height;
    
    ESP_LOGI(TAG, "Printing image: %dx%d -> %dx%d (printer max width: %d)", 
             width, height, target_width, target_height, max_width);

    // Convert RGB565 to grayscale with proper scaling
    std::vector<uint8_t> gray_image(target_width * target_height);
    for (int y = 0; y < target_height; ++y) {
        int src_y = (y * height) / target_height;
        const uint16_t* src_row = data + src_y * stride_pixels;
        for (int x = 0; x < target_width; ++x) {
            int src_x = (x * width) / target_width;
            uint16_t pixel = src_row[src_x];
            // Extract RGB565 components
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;
            // Scale to 0-255
            uint16_t r8 = (r * 527 + 23) >> 6;
            uint16_t g8 = (g * 259 + 33) >> 6;
            uint16_t b8 = (b * 527 + 23) >> 6;
            // Calculate luminance
            uint16_t lum = (r8 * 299 + g8 * 587 + b8 * 114) / 1000;
            gray_image[y * target_width + x] = static_cast<uint8_t>(lum);
        }
    }

    // Apply Floyd-Steinberg dithering for better image quality
    std::vector<int16_t> error_buffer(target_width * target_height);
    for (int i = 0; i < target_width * target_height; ++i) {
        error_buffer[i] = static_cast<int16_t>(gray_image[i]);
    }

    for (int y = 0; y < target_height; ++y) {
        for (int x = 0; x < target_width; ++x) {
            int idx = y * target_width + x;
            int16_t old_pixel = error_buffer[idx];
            uint8_t new_pixel = (old_pixel > 127) ? 255 : 0;
            gray_image[idx] = new_pixel;
            int16_t quant_error = old_pixel - new_pixel;

            // Distribute error to neighboring pixels
            if (x + 1 < target_width) {
                error_buffer[idx + 1] += (quant_error * 7) / 16;
            }
            if (y + 1 < target_height) {
                if (x > 0) {
                    error_buffer[idx + target_width - 1] += (quant_error * 3) / 16;
                }
                error_buffer[idx + target_width] += (quant_error * 5) / 16;
                if (x + 1 < target_width) {
                    error_buffer[idx + target_width + 1] += (quant_error * 1) / 16;
                }
            }
        }
    }

    // Ensure DTR is HIGH before opening UART session
    gpio_set_level(dtr_pin_, 1);
    
    // Open UART session on printer side
    esp_err_t err = WriteCommand(kCmdSerialOpen, sizeof(kCmdSerialOpen));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Initialize printer
    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err != ESP_OK) {
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    // Set alignment to center (1B 61 01)
    const uint8_t cmd_center[] = {0x1B, 0x61, 0x01};
    err = WriteCommand(cmd_center, sizeof(cmd_center));
    if (err != ESP_OK) {
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // Use ESC * m (1B 2A) for vertical bit image mode
    // m=33 means 24-dot double density (×1 horizontal, ×1 vertical)
    const uint8_t m = 33;
    const int dots_per_pass = 24; // 24-dot mode
    const int num_passes = (target_height + dots_per_pass - 1) / dots_per_pass;

    for (int pass = 0; pass < num_passes; ++pass) {
        // ESC * m nL nH [data]
        // nL + nH*256 = horizontal dots (must satisfy: 1 ≤ nL + nH*256 ≤ 384 per RULE.md)
        // Always send both nL and nH bytes for protocol compatibility
        // Verify width is within valid range (1-384)
        if (target_width < 1 || target_width > 384) {
            ESP_LOGE(TAG, "Invalid target_width: %d (must be 1-384)", target_width);
            WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
            return ESP_ERR_INVALID_ARG;
        }
        
        uint8_t cmd_header[5] = {
            0x1B, 0x2A, m,
            static_cast<uint8_t>(target_width & 0xFF),        // nL (low byte)
            static_cast<uint8_t>((target_width >> 8) & 0xFF)  // nH (high byte)
        };
        
        err = WriteCommand(cmd_header, sizeof(cmd_header));
        
        if (err != ESP_OK) {
            WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
            return err;
        }

        // Send vertical bit image data (3 bytes per column for 24-dot mode)
        std::vector<uint8_t> column_data(target_width * 3, 0);
        for (int x = 0; x < target_width; ++x) {
            // Each column has 3 bytes (24 bits)
            for (int bit = 0; bit < dots_per_pass && (pass * dots_per_pass + bit) < target_height; ++bit) {
                int y = pass * dots_per_pass + bit;
                uint8_t pixel = gray_image[y * target_width + x];
                if (pixel == 0) { // black pixel
                    int byte_idx = bit / 8;
                    int bit_idx = bit % 8;
                    column_data[x * 3 + byte_idx] |= (0x01 << bit_idx);
                }
            }
        }

        // Send column data
        int written = uart_write_bytes(uart_port_, 
                                      reinterpret_cast<const char*>(column_data.data()), 
                                      target_width * 3);
        if (written < 0 || written != target_width * 3) {
            ESP_LOGE(TAG, "Failed to write image data pass %d, written=%d", pass, written);
            WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
            return ESP_FAIL;
        }

        // Line feed after each pass
        const uint8_t lf[] = {0x0D, 0x0A};
        uart_write_bytes(uart_port_, reinterpret_cast<const char*>(lf), sizeof(lf));
        vTaskDelay(pdMS_TO_TICKS(10)); // Give printer time to process
    }

    uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Reset alignment to left
    const uint8_t cmd_left[] = {0x1B, 0x61, 0x00};
    WriteCommand(cmd_left, sizeof(cmd_left));
    
    // Advance paper a few lines to avoid tearing
    FeedLines(25);
    
    err = WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to close printer UART session after image: %s", esp_err_to_name(err));
    }
    
    ESP_LOGI(TAG, "Image printed successfully: %dx%d (target: %dx%d)", 
             width, height, target_width, target_height);
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

esp_err_t ThermalPrinter::SetBaudRate(int baud_rate, bool persist) {
    if (!IsSupportedBaudRateInternal(baud_rate)) {
        ESP_LOGW(TAG, "Unsupported baud rate requested: %d", baud_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized_) {
        ESP_LOGI(TAG, "Set baud pre-init: %d (persist=%d)", baud_rate, persist ? 1 : 0);
        baud_rate_ = baud_rate;
        if (persist) {
            PersistBaudRateToSettings(baud_rate);
        }
        return ESP_OK;
    }

    if (baud_rate_ == baud_rate) {
        ESP_LOGI(TAG, "Baud already %d (persist=%d)", baud_rate, persist ? 1 : 0);
        if (persist) {
            PersistBaudRateToSettings(baud_rate);
        }
        return ESP_OK;
    }

    uint8_t m_value = 0;
    if (!LookupBaudIndex(baud_rate, m_value)) {
        ESP_LOGW(TAG, "Baud %d not in table", baud_rate);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Changing printer baud: current=%d target=%d (m=0x%02X)", baud_rate_, baud_rate, m_value);
    // Keep using current UART baud to talk to printer while sending the change command.
    // Open printer UART session per protocol before sending.
    esp_err_t err = WriteCommand(kCmdSerialOpen, sizeof(kCmdSerialOpen));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open session for baud change: %s", esp_err_to_name(err));
        return err;
    }

    err = SetBaudRateIndex(m_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send baud change index 0x%02X: %s", m_value, esp_err_to_name(err));
        return err;
    }

    uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(20)); // allow printer to apply new baud internally

    // Close the session on old baud (optional; printer may ignore)
    WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));

    ESP_LOGI(TAG, "Switching host UART to %d", baud_rate);
    err = uart_set_baudrate(uart_port_, baud_rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_baudrate failed: %s", esp_err_to_name(err));
        return err;
    }

    baud_rate_ = baud_rate;
    if (persist) {
        PersistBaudRateToSettings(baud_rate);
    }

    // Re-init at the new baud to sync state.
    ESP_LOGI(TAG, "Re-init printer at new baud %d", baud_rate);
    WriteCommand(kCmdInit, sizeof(kCmdInit));
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "Thermal printer baud rate updated to %d", baud_rate);
    return ESP_OK;
}

bool ThermalPrinter::IsSupportedBaudRate(int baud_rate) {
    return IsSupportedBaudRateInternal(baud_rate);
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

esp_err_t ThermalPrinter::FeedLines(uint8_t lines) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (lines == 0) {
        return ESP_OK;
    }
    uint8_t cmd[sizeof(kCmdFeedLinesBase)];
    memcpy(cmd, kCmdFeedLinesBase, sizeof(cmd));
    cmd[4] = 0x00;          // m = 0 : feed
    cmd[5] = lines;         // k lines
    // tL/tH keep default 200ms timeout window
    return WriteCommand(cmd, sizeof(cmd));
}

