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
        {9600, 0x00},
        {19200, 0x01},
        {38400, 0x02},
        {57600, 0x03},
        {115200, 0x04},
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

    int LoadBaudRateFromSettings(int default_baud_rate) {
        Settings settings(kPrinterSettingsNs);
        return settings.GetInt(kBaudRateKey, default_baud_rate);
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
    const int default_baud_rate = baud_rate_;
    baud_rate_ = LoadBaudRateFromSettings(default_baud_rate);
    if (!IsSupportedBaudRateInternal(baud_rate_)) {
        ESP_LOGW(TAG, "Unsupported stored baud rate %d, fallback to default %d", baud_rate_, default_baud_rate);
        baud_rate_ = default_baud_rate;
    }

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

esp_err_t ThermalPrinter::PrintImageRgb565(const uint16_t* data, int width, int height, int stride_pixels) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || width <= 0 || height <= 0 || stride_pixels < width) {
        return ESP_ERR_INVALID_ARG;
    }

    // Limit width to typical 384-dot head (50mm at 8 dots/mm)
    const int max_width = 384;
    const int target_width = (width > max_width) ? max_width : width;
    const int target_height = (width > 0) ? std::max(1, (height * target_width) / width) : height;
    const int bytes_per_row = (target_width + 7) / 8;

    // Open UART session on printer side
    esp_err_t err = WriteCommand(kCmdSerialOpen, sizeof(kCmdSerialOpen));
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // Initialize printer
    err = WriteCommand(kCmdInit, sizeof(kCmdInit));
    if (err != ESP_OK) {
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));

    // Send raster bit image header: 1D 76 30 00 xL xH yL yH
    uint8_t header[8] = {
        0x1D, 0x76, 0x30, 0x00,
        static_cast<uint8_t>(bytes_per_row & 0xFF),
        static_cast<uint8_t>((bytes_per_row >> 8) & 0xFF),
        static_cast<uint8_t>(target_height & 0xFF),
        static_cast<uint8_t>((target_height >> 8) & 0xFF)
    };

    int written = uart_write_bytes(uart_port_, reinterpret_cast<const char*>(header), sizeof(header));
    if (written < 0 || written != static_cast<int>(sizeof(header))) {
        ESP_LOGE(TAG, "Failed to write image header, written=%d", written);
        WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
        return ESP_FAIL;
    }

    std::vector<uint8_t> row(bytes_per_row, 0);
    for (int y = 0; y < target_height; ++y) {
        std::fill(row.begin(), row.end(), 0);
        // Nearest-neighbor scaling
        int src_y = (y * height) / target_height;
        const uint16_t* src_row = data + src_y * stride_pixels;

        for (int x = 0; x < target_width; ++x) {
            int src_x = (x * width) / target_width;
            uint16_t pixel = src_row[src_x];
            uint8_t r = (pixel >> 11) & 0x1F;
            uint8_t g = (pixel >> 5) & 0x3F;
            uint8_t b = pixel & 0x1F;
            // Scale to 0-255
            uint16_t r8 = (r * 527 + 23) >> 6;   // fast 5-bit to 8-bit
            uint16_t g8 = (g * 259 + 33) >> 6;   // fast 6-bit to 8-bit
            uint16_t b8 = (b * 527 + 23) >> 6;   // fast 5-bit to 8-bit
            uint16_t lum = (r8 * 299 + g8 * 587 + b8 * 114) / 1000;
            bool black = lum < 128; // simple threshold; could be improved with dithering
            if (black) {
                row[x >> 3] |= (0x80 >> (x & 0x07));
            }
        }

        written = uart_write_bytes(uart_port_, reinterpret_cast<const char*>(row.data()), bytes_per_row);
        if (written < 0 || written != bytes_per_row) {
            ESP_LOGE(TAG, "Failed to write image row %d, written=%d", y, written);
            WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
            return ESP_FAIL;
        }
    }

    uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(200));
    // Advance paper a few lines to avoid tearing the last line when tearing off
    FeedLines(25);
    err = WriteCommand(kCmdSerialClose, sizeof(kCmdSerialClose));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to close printer UART session after image: %s", esp_err_to_name(err));
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

esp_err_t ThermalPrinter::SetBaudRate(int baud_rate, bool persist) {
    if (!IsSupportedBaudRateInternal(baud_rate)) {
        ESP_LOGW(TAG, "Unsupported baud rate requested: %d", baud_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized_) {
        baud_rate_ = baud_rate;
        if (persist) {
            PersistBaudRateToSettings(baud_rate);
        }
        return ESP_OK;
    }

    if (baud_rate_ == baud_rate) {
        if (persist) {
            PersistBaudRateToSettings(baud_rate);
        }
        return ESP_OK;
    }

    uint8_t m_value = 0;
    if (!LookupBaudIndex(baud_rate, m_value)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = SetBaudRateIndex(m_value);
    if (err != ESP_OK) {
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    err = uart_set_baudrate(uart_port_, baud_rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_baudrate failed: %s", esp_err_to_name(err));
        return err;
    }

    baud_rate_ = baud_rate;
    if (persist) {
        PersistBaudRateToSettings(baud_rate);
    }

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

