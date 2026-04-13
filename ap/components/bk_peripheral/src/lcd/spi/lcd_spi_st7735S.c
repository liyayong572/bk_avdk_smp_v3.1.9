// Copyright 2020-2021 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <common/bk_include.h>
#include <driver/lcd_types.h>


static const lcd_qspi_init_cmd_t st7735s_init_cmds[] =
{
    {0x11, {0x00}, 0},
    {0xB1, {0x05, 0x3A, 0x3A}, 3},
    {0xB2, {0x05, 0x3A, 0x3A}, 3},
    {0xB3, {0x05, 0x3A, 0x3A,0x05, 0x3A, 0x3A}, 6},
    {0xB4, {0x03}, 1},
	{0xC0, {0xA4,0x04,0x84}, 3},
	{0xC1, {0xC5}, 1},
	{0xC2, {0x0D,0x00}, 2},
	{0xC3, {0x8D,0x2A}, 2},
	{0xC4, {0x8D,0xEE}, 2},
	{0xC5, {0x09}, 1},
	//{0x36, {0xC8}, 1},
	//{0x36, {0x6C}, 1},
	{0x36, {0x6C}, 1},

	//{0x2A, {0x00, 0x00, 0x00, 0xA0}, 4},
	//{0x2B, {0x00, 0x00, 0x00, 0x78}, 4},
	{0xE0, {0x17, 0x1F, 0x04, 0x07, 0x14, 0x11, 0x0E, 0x17, 0x1D, 0x25, 0x36, 0x3F, 0x10, 0x15,0x02,0x00}, 16},
    {0xE1, {0x15, 0x1C, 0x05, 0x04, 0x11, 0x0F, 0x0C, 0x15, 0x1B, 0x22, 0x2F, 0x3F, 0x10, 0x12,0x00,0x00}, 16},
	{0x3A, {0x05}, 1},

	{0x2A, {0x00, 0x00, 0x00, 0x9f}, 4},
	{0x2B, {0x00, 0x00, 0x00, 0x7f}, 4},
	
	{0x29, {0x00}, 0},

};

static const lcd_spi_t lcd_spi_st7735s_config =
{
    .clk = LCD_QSPI_64M,
    .init_cmd = st7735s_init_cmds,
    .device_init_cmd_len = sizeof(st7735s_init_cmds) / sizeof (lcd_qspi_init_cmd_t),
    .frame_len = 128 * 160 * CONFIG_LCD_SPI_COLOR_DEPTH_BYTE,
};

const lcd_device_t lcd_device_st7735S =
{
    .id = LCD_DEVICE_ST7735S,
    .name = "st7735s",
    .type = LCD_TYPE_SPI,
    .width =  160,
    .height = 128,
    .spi = &lcd_spi_st7735s_config,
    .out_fmt = PIXEL_FMT_RGB565,
    .init = NULL,
    .off = NULL,
};
