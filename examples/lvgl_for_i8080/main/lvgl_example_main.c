/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lcd_ili9488.h"

static const char *TAG = "example";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)
#define LCD_I80_BUS_WIDTH 16


#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_DATA0          47
#define PIN_NUM_DATA1          21
#define PIN_NUM_DATA2          14
#define PIN_NUM_DATA3          13
#define PIN_NUM_DATA4          12
#define PIN_NUM_DATA5          11
#define PIN_NUM_DATA6          10
#define PIN_NUM_DATA7          9
#if LCD_I80_BUS_WIDTH > 8
#define PIN_NUM_DATA8          3
#define PIN_NUM_DATA9          8
#define PIN_NUM_DATA10         16
#define PIN_NUM_DATA11         15
#define PIN_NUM_DATA12         7
#define PIN_NUM_DATA13         6
#define PIN_NUM_DATA14         5
#define PIN_NUM_DATA15         4
#endif
#define PIN_NUM_PCLK           18
#define PIN_NUM_CS             -1
#define PIN_NUM_DC             45
#define PIN_NUM_RST            -1
#define PIN_NUM_BK_LIGHT       46
#define PIN_NUM_RD             48 
#define LCD_RD_ON_LEVEL        1

// The pixel number in horizontal and vertical
#define LCD_H_RES              320
#define LCD_V_RES              480
// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

#define LVGL_TICK_PERIOD_MS    2

// extern void lvgl_demo_ui(lv_obj_t *scr);

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp_driver = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp_driver);
    return false;
}

static void disp_flush(lv_display_t *display, const lv_area_t *area, uint8_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(display);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_display_flush_ready(display);
}

static void increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

void app_main(void)
{
    static lv_display_t * disp;

    ESP_LOGI(TAG, "Turn on RD Pin");
    gpio_config_t rd_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_RD
    };
    ESP_ERROR_CHECK(gpio_config(&rd_gpio_config));
    gpio_set_level(PIN_NUM_RD, LCD_RD_ON_LEVEL);

    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_OFF_LEVEL);

    ESP_LOGI(TAG, "Initialize Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        .clk_src = LCD_CLK_SRC_DEFAULT,
#endif
        .dc_gpio_num = PIN_NUM_DC,
        .wr_gpio_num = PIN_NUM_PCLK,
        .data_gpio_nums = {
            PIN_NUM_DATA0,
            PIN_NUM_DATA1,
            PIN_NUM_DATA2,
            PIN_NUM_DATA3,
            PIN_NUM_DATA4,
            PIN_NUM_DATA5,
            PIN_NUM_DATA6,
            PIN_NUM_DATA7,
#if LCD_I80_BUS_WIDTH > 8
            PIN_NUM_DATA8,
            PIN_NUM_DATA9,
            PIN_NUM_DATA10,
            PIN_NUM_DATA11,
            PIN_NUM_DATA12,
            PIN_NUM_DATA13,
            PIN_NUM_DATA14,
            PIN_NUM_DATA15,
#endif
        },
        .bus_width = LCD_I80_BUS_WIDTH,
        .max_transfer_bytes = LCD_H_RES * 40 * sizeof(uint16_t)
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install LCD driver of ili9488");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9488(io_handle, &panel_config,LCD_H_RES * 20 * sizeof(lv_color_t), &panel_handle));

    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);
    // esp_lcd_panel_invert_color(panel_handle, true);
    // the gap is LCD panel specific, even panels with the same driver IC, can have different gap value
    esp_lcd_panel_set_gap(panel_handle, 0, 0);

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(PIN_NUM_BK_LIGHT, LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    // create display
    ESP_LOGI(TAG, "Register display driver to LVGL");
    disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, disp_flush);
    lv_display_set_user_data(disp, panel_handle);

    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    LV_ATTRIBUTE_MEM_ALIGN
    static uint8_t buf_1[LCD_H_RES * 32 * 2];

    LV_ATTRIBUTE_MEM_ALIGN
    static uint8_t buf_2[LCD_H_RES * 32 * 2];
    lv_display_set_buffers(disp, buf_1, buf_2, sizeof(buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Display LVGL animation");
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_t * label1 = lv_label_create(scr);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_align(label1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_28, 0);
    lv_label_set_text(label1, "This is LVGL 9.2.2!");
    // lvgl_demo_ui(scr);

    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}
