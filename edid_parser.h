#ifndef EDID_PARSER_H
#define EDID_PARSER_H

#include <stdint.h>

#define EDID_BLOCK_SIZE             128
#define EDID_MAX_VIDEO_MODES        64

#define EDID_EXT_CTA                0x02
#define EDID_EXT_DISPLAYID          0x70
#define EDID_EXT_BLOCK_MAP          0xF0

#define CTA_TAG_AUDIO               1
#define CTA_TAG_VIDEO               2
#define CTA_TAG_VENDOR              3
#define CTA_TAG_SPEAKER             4
#define CTA_TAG_EXTENDED            7

typedef struct
{
    uint16_t width;
    uint16_t height;

    uint32_t pixel_clock_khz;

    uint16_t h_active;
    uint16_t h_blank;
    uint16_t h_front_porch;
    uint16_t h_sync;
    uint16_t h_back_porch;

    uint16_t v_active;
    uint16_t v_blank;
    uint16_t v_front_porch;
    uint16_t v_sync;
    uint16_t v_back_porch;

    uint16_t h_total;
    uint16_t v_total;

    uint32_t refresh_rate;

    uint8_t vic;
    uint8_t native;
    uint8_t interlaced;
} edid_video_mode_t;

/*
 * EDID Color Characteristics (offsets 19h-22h)
 * Each CIE coordinate is a 10-bit value: high 8 bits + 2 low bits.
 * Stored as 10-bit integers (0-1023), divide by 1024.0 for float.
 */
typedef struct
{
    uint16_t red_x;
    uint16_t red_y;
    uint16_t green_x;
    uint16_t green_y;
    uint16_t blue_x;
    uint16_t blue_y;
    uint16_t white_x;
    uint16_t white_y;
} edid_color_info_t;

typedef struct
{
    uint8_t version;
    uint8_t revision;

    uint16_t manufacturer_id;
    uint16_t product_code;
    uint32_t serial_number;

    uint8_t week;
    uint8_t year;

    uint8_t video_input;
    uint8_t horizontal_size_cm;
    uint8_t vertical_size_cm;
    uint8_t gamma;
    uint8_t feature_support;

    edid_color_info_t color_info;

    uint8_t extension_count;

    edid_video_mode_t modes[EDID_MAX_VIDEO_MODES];
    uint8_t mode_count;
} edid_info_t;

int edid_parse(const uint8_t *edid,
               uint32_t edid_size,
               edid_info_t *info);

const edid_video_mode_t *
edid_get_native_mode(const edid_info_t *info);

#endif /* EDID_PARSER_H */
