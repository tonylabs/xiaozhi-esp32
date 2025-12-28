#include "ts040wvs02np.h"

#include <esp_err.h>
#include <esp_check.h>
#include <esp_lcd_panel_interface.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_commands.h>
#include <esp_log.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "ts040wvs02np";

typedef struct {
    esp_lcd_panel_t base;
    esp_lcd_panel_io_handle_t io;
    int reset_gpio_num;
    int x_gap;
    int y_gap;
    uint8_t madctl_val;
    uint8_t colmod_val;
} ts040_panel_t;

typedef struct {
    int cmd;
    const void *data;
    size_t data_bytes;
    unsigned int delay_ms;
} ts040_lcd_init_cmd_t;

// Vendor-specific init sequence for TS040WVS02NP (RGB565, 480x480)
static const ts040_lcd_init_cmd_t ts040_init_cmds[] = {
    {0x5A, (uint8_t[]){0x01}, 1, 10},                        // Soft reset
    {0x3A, (uint8_t[]){0x55}, 1, 0},                         // RGB565
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},       // Column 0..479
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},       // Row 0..479
    {0xAC, (uint8_t[]){0x00}, 1, 0},                         // No rotation/mirror
    {0x71, (uint8_t[]){0x30, 0x01, 0xE0, 0x01, 0xE0}, 5, 0}, // CLK=48MHz, H/V=480
    {0x29, NULL, 0, 20},                                     // Display ON
};

static inline uint32_t encode_cmd(uint8_t cmd) {
    // CODE1=0x02, CODE2=0x00, CODE3=cmd, CODE4=0x00
    return (0x02u << 24) | (0x00u << 16) | ((uint32_t)cmd << 8) | 0x00u;
}

static inline uint32_t encode_ramwr(void) {
    // CODE1=0x12, CODE2=0x00, CODE3=0x2C, CODE4=0x00
    return (0x12u << 24) | (0x00u << 16) | ((uint32_t)LCD_CMD_RAMWR << 8) | 0x00u;
}

static esp_err_t ts040_panel_del(esp_lcd_panel_t *panel) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    if (p->reset_gpio_num >= 0) {
        gpio_reset_pin((gpio_num_t)p->reset_gpio_num);
    }
    free(p);
    return ESP_OK;
}

static esp_err_t ts040_panel_reset(esp_lcd_panel_t *panel) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    if (p->reset_gpio_num >= 0) {
        gpio_set_level((gpio_num_t)p->reset_gpio_num, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)p->reset_gpio_num, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ESP_OK;
}

static esp_err_t ts040_panel_init(esp_lcd_panel_t *panel) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    ESP_RETURN_ON_FALSE(p->io, ESP_ERR_INVALID_STATE, TAG, "panel io not set");

    for (size_t i = 0; i < sizeof(ts040_init_cmds) / sizeof(ts040_init_cmds[0]); i++) {
        uint32_t cmd_enc;
        if (ts040_init_cmds[i].cmd == LCD_CMD_RAMWR) {
            cmd_enc = encode_ramwr();
        } else {
            cmd_enc = encode_cmd(ts040_init_cmds[i].cmd);
        }
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, cmd_enc, ts040_init_cmds[i].data, ts040_init_cmds[i].data_bytes), TAG, "init tx failed");
        if (ts040_init_cmds[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(ts040_init_cmds[i].delay_ms));
        }
    }

    p->colmod_val = 0x55;
    p->madctl_val = 0x00;
    return ESP_OK;
}

static esp_err_t ts040_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    x_start += p->x_gap;
    x_end += p->x_gap;
    y_start += p->y_gap;
    y_end += p->y_gap;

    // CASET
    uint8_t col_data[] = {
        static_cast<uint8_t>((x_start >> 8) & 0xFF), static_cast<uint8_t>(x_start & 0xFF),
        static_cast<uint8_t>(((x_end - 1) >> 8) & 0xFF), static_cast<uint8_t>((x_end - 1) & 0xFF),
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, encode_cmd(LCD_CMD_CASET), col_data, sizeof(col_data)), TAG, "tx CASET failed");

    // RASET
    uint8_t row_data[] = {
        static_cast<uint8_t>((y_start >> 8) & 0xFF), static_cast<uint8_t>(y_start & 0xFF),
        static_cast<uint8_t>(((y_end - 1) >> 8) & 0xFF), static_cast<uint8_t>((y_end - 1) & 0xFF),
    };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, encode_cmd(LCD_CMD_RASET), row_data, sizeof(row_data)), TAG, "tx RASET failed");

    // RAMWR
    size_t len = (x_end - x_start) * (y_end - y_start) * p->colmod_val / 8; // colmod 0x55 => 16bpp
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(p->io, encode_ramwr(), color_data, len), TAG, "tx RAMWR failed");
    return ESP_OK;
}

static esp_err_t ts040_panel_mirror(esp_lcd_panel_t *panel, bool mirror_x, bool mirror_y) {
    (void)panel;
    (void)mirror_x;
    (void)mirror_y;
    return ESP_OK;
}

static esp_err_t ts040_panel_swap_xy(esp_lcd_panel_t *panel, bool swap_axes) {
    (void)panel;
    (void)swap_axes;
    return ESP_OK;
}

static esp_err_t ts040_panel_set_gap(esp_lcd_panel_t *panel, int x_gap, int y_gap) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    p->x_gap = x_gap;
    p->y_gap = y_gap;
    return ESP_OK;
}

static esp_err_t ts040_panel_disp_on_off(esp_lcd_panel_t *panel, bool on_off) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    uint32_t cmd_enc = encode_cmd(on_off ? LCD_CMD_DISPON : LCD_CMD_DISPOFF);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, cmd_enc, NULL, 0), TAG, "tx disp on/off failed");
    return ESP_OK;
}

static esp_err_t ts040_panel_invert_color(esp_lcd_panel_t *panel, bool invert_color_data) {
    ts040_panel_t *p = __containerof(panel, ts040_panel_t, base);
    uint32_t cmd_enc = encode_cmd(invert_color_data ? LCD_CMD_INVON : LCD_CMD_INVOFF);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(p->io, cmd_enc, NULL, 0), TAG, "tx invert failed");
    return ESP_OK;
}

extern "C" esp_err_t ts040wvs02np_new_panel(const esp_lcd_panel_io_handle_t io,
                                            const esp_lcd_panel_dev_config_t *panel_dev_config,
                                            esp_lcd_panel_handle_t *ret_panel) {
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG, "bad args");

    ts040_panel_t *p = (ts040_panel_t *)calloc(1, sizeof(ts040_panel_t));
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "no mem");

    p->io = io;
    p->reset_gpio_num = panel_dev_config->reset_gpio_num;
    p->madctl_val = 0x00;
    p->colmod_val = (panel_dev_config->bits_per_pixel == 16) ? 0x55 : 0x66;

    if (p->reset_gpio_num >= 0) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << p->reset_gpio_num,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
#if SOC_GPIO_SUPPORT_PIN_HYS_FILTER
            .hys_ctrl_mode = GPIO_HYS_SOFT_DISABLE,
#endif
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    p->base.del = ts040_panel_del;
    p->base.reset = ts040_panel_reset;
    p->base.init = ts040_panel_init;
    p->base.draw_bitmap = ts040_panel_draw_bitmap;
    p->base.invert_color = ts040_panel_invert_color;
    p->base.set_gap = ts040_panel_set_gap;
    p->base.mirror = ts040_panel_mirror;
    p->base.swap_xy = ts040_panel_swap_xy;
    p->base.disp_on_off = ts040_panel_disp_on_off;

    *ret_panel = &p->base;
    ESP_LOGI(TAG, "TS040WVS02NP panel created");
    return ESP_OK;
}


