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
    esp_err_t SendRaw(const uint8_t* data, size_t length);
    esp_err_t SetBaudRateIndex(uint8_t m_value);
    esp_err_t QueryPaperStatus(bool& paper_present);

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

