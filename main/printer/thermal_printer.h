    #pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_err.h>

enum ThermalPrinterModel {
    FTP_628MCL101_50MM = 0,
};

class ThermalPrinter {
public:
    ThermalPrinter(ThermalPrinterModel type,
                   uart_port_t uart_port,
                   gpio_num_t tx_pin,
                   gpio_num_t rx_pin,
                   gpio_num_t dtr_pin,
                   int baud_rate);
    ~ThermalPrinter();

    esp_err_t Init();
    esp_err_t SelfTest();
    esp_err_t PrintText(const std::string& text, bool append_newline = true);
    /**
     * Print an RGB565 image using ESC/POS raster bit image (1D 76 30 00).
     * Image will be scaled to printer width (max 384px) with simple nearest resampling
     * and thresholded to monochrome.
     * @param data pointer to RGB565 pixels
     * @param width image width in pixels
     * @param height image height in pixels
     * @param stride_pixels pixels per row (>= width)
     */
    esp_err_t PrintImageRgb565(const uint16_t* data, int width, int height, int stride_pixels);
    esp_err_t SendRaw(const uint8_t* data, size_t length);
    esp_err_t SetBaudRate(int baud_rate, bool persist = true);
    static bool IsSupportedBaudRate(int baud_rate);
    esp_err_t SetBaudRateIndex(uint8_t m_value);
    esp_err_t QueryPaperStatus(bool& paper_present);
    esp_err_t FeedLines(uint8_t lines);

    inline bool initialized() const { return initialized_; }
    inline ThermalPrinterModel type() const { return type_; }
    inline int baud_rate() const { return baud_rate_; }

private:
    esp_err_t ConfigureUart();
    esp_err_t ConfigureDtr();
    esp_err_t WriteCommand(const uint8_t* cmd, size_t length);

    ThermalPrinterModel type_;
    uart_port_t uart_port_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;
    gpio_num_t dtr_pin_;
    int baud_rate_;
    bool initialized_;
};

