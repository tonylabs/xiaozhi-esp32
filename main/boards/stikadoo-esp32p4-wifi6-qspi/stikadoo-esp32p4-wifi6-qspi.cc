#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "application.h"
#include "display/lcd_display.h"
#include "display/panels/ts040wvs02np.h"
#include "button.h"
#include "config.h"
#include "printer/thermal_printer.h"
#include <driver/i2c_master.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_dev.h>
#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "StikadooEsp32p4Wifi6Qspi"
#define LCD_OPCODE_WRITE_CMD (0x02ULL)

namespace {
uint32_t BuildQspiCmd(uint8_t cmd) {
    return (LCD_OPCODE_WRITE_CMD << 24) | (static_cast<uint32_t>(cmd) << 8);
}

void SendVendorBacklightInit(esp_lcd_panel_io_handle_t panel_io) {
    const struct {
        uint8_t cmd;
        uint8_t data;
        uint8_t data_len;
        uint32_t delay_ms;
    } init_cmds[] = {
        {0x29, 0x00, 0, 1000}, // Display ON
        {0x21, 0x64, 1, 1000}, // PWM frequency 100 kHz
        {0x20, 0x00, 1, 1000}, // Duty 0%
        {0x20, 0x64, 1, 1000}, // Duty 100%
    };

    for (const auto& entry : init_cmds) {
        const uint8_t* payload = entry.data_len ? &entry.data : nullptr;
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, BuildQspiCmd(entry.cmd), payload, entry.data_len));
        if (entry.delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(entry.delay_ms));
        }
    }
}
} // namespace

class CustomLcdDisplay : public SpiLcdDisplay {
    public:
        static void rounder_event_cb(lv_event_t * e) {

        }

        CustomLcdDisplay(esp_lcd_panel_io_handle_t io_handle, 
                        esp_lcd_panel_handle_t panel_handle,
                        int width,
                        int height,
                        int offset_x,
                        int offset_y,
                        bool mirror_x,
                        bool mirror_y,
                        bool swap_xy) 
            : SpiLcdDisplay(io_handle, panel_handle, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy) {
            DisplayLockGuard lock(this);
            lv_display_add_event_cb(display_, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
        }
};

class CustomBacklight : public Backlight {
public:
    explicit CustomBacklight(esp_lcd_panel_io_handle_t panel_io) : panel_io_(panel_io) {}

protected:
    esp_lcd_panel_io_handle_t panel_io_;

    void SetBrightnessImpl(uint8_t brightness) override {
        // Guard LCD IO access to avoid bus contention (timer callback context)
        auto display = Board::GetInstance().GetDisplay();
        DisplayLockGuard lock(display);
        uint8_t duty = brightness;
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io_, BuildQspiCmd(0x20), &duty, sizeof(duty)));
    }
};

class StikadooEsp32p4Wifi6Qspi : public WifiBoard {

private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    LcdDisplay *display_;
    CustomBacklight *backlight_;
    ThermalPrinter *thermal_printer_ = nullptr;

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
                .allow_pd = 0,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize QSPI bus");
        spi_bus_config_t bus_config = {
            .data0_io_num = QSPI_PIN_NUM_LCD_DATA0,
            .data1_io_num = QSPI_PIN_NUM_LCD_DATA1,
            .sclk_io_num = QSPI_PIN_NUM_LCD_PCLK,
            .data2_io_num = QSPI_PIN_NUM_LCD_DATA2,
            .data3_io_num = QSPI_PIN_NUM_LCD_DATA3,
            .data4_io_num = GPIO_NUM_NC,
            .data5_io_num = GPIO_NUM_NC,
            .data6_io_num = GPIO_NUM_NC,
            .data7_io_num = GPIO_NUM_NC,
            .data_io_default_level = 0,
            .max_transfer_sz = QSPI_LCD_H_RES * QSPI_LCD_V_RES * sizeof(uint16_t),
            .flags = SPICOMMON_BUSFLAG_QUAD,
            .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
            .intr_flags = 0,
        };
        ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
    }

    void InitializeLCD() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGI(TAG, "Install panel IO");

        esp_lcd_panel_io_spi_config_t io_config = {
            .cs_gpio_num = QSPI_PIN_NUM_LCD_CS,
            .dc_gpio_num = -1,
            .spi_mode = 0,
            .pclk_hz = 60 * 1000 * 1000, // match vendor reference
            .trans_queue_depth = 20, // Increased from 10 to prevent queue overflow during heavy operations
            .on_color_trans_done = nullptr,
            .user_ctx = nullptr,
            .lcd_cmd_bits = 32,
            .lcd_param_bits = 8,
            .cs_ena_pretrans = 0,
            .cs_ena_posttrans = 0,
            .flags = {
                .dc_high_on_cmd = 0,
                .dc_low_on_data = 0,
                .dc_low_on_param = 0,
                .octal_mode = 0,
                .quad_mode = 1,
                .sio_mode = 0,
                .lsb_first = 0,
                .cs_high_active = 0,
            },
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));

        ESP_LOGI(TAG, "Install TS040WVS02NP panel driver (custom)");
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
            .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
            .flags = {
                .reset_active_high = 0,
            },
            .vendor_config = nullptr,
        };

        ESP_ERROR_CHECK(ts040wvs02np_new_panel(panel_io, &panel_config, &panel));

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_disp_on_off(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        SendVendorBacklightInit(panel_io);
        display_ = new CustomLcdDisplay(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        backlight_ = new CustomBacklight(panel_io);
        backlight_->RestoreBrightness();
    }

    void InitializeButtons() {
        // Behavior: press-and-hold to listen, release to stop.
        boot_button_.OnPressDown([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.StartListening();
        });

        boot_button_.OnPressUp([]() {
            auto& app = Application::GetInstance();
            app.StopListening();
        });
    }

public:
    StikadooEsp32p4Wifi6Qspi() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeCodecI2c();
        InitializeSpi();
        InitializeLCD();
        InitializeButtons();
        InitializeThermalPrinter();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_1, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE, AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN, AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual Backlight *GetBacklight() override {
         return backlight_;
     }

    virtual ThermalPrinter *GetThermalPrinter() override {
        return thermal_printer_;
    }

    void InitializeThermalPrinter() {
        thermal_printer_ = new ThermalPrinter(
            THERMAL_PRINTER_TYPE,
            THERMAL_PRINTER_UART_PORT,
            THERMAL_PRINTER_UART_TXD,
            THERMAL_PRINTER_UART_RXD,
            THERMAL_PRINTER_UART_DTR,
            THERMAL_PRINTER_UART_BAUD_RATE);

        esp_err_t err = thermal_printer_->Init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize thermal printer: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Thermal printer initialized");
        }
    }

};

DECLARE_BOARD(StikadooEsp32p4Wifi6Qspi);
