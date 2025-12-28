#pragma once

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_dev.h>
#include <esp_lcd_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a TS040WVS02NP QSPI panel instance.
 *
 * QSPI packets:
 *  - Commands: CODE1=0x02, CODE2=0x00, CODE3=cmd, CODE4=0x00
 *  - RAMWR:    CODE1=0x12, CODE2=0x00, CODE3=0x2C, CODE4=0x00
 */
esp_err_t ts040wvs02np_new_panel(const esp_lcd_panel_io_handle_t io,
                                 const esp_lcd_panel_dev_config_t *panel_dev_config,
                                 esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif


