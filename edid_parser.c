#include "edid_parser.h"

#include <stdio.h>
#include <string.h>

typedef struct
{
    uint8_t vic;
    uint16_t width;
    uint16_t height;
    uint32_t refresh_rate;
} edid_vic_t;

/*
 * Common CTA VICs.
 * Extend this table if your product needs the complete CTA-861 VIC set.
 */
static const edid_vic_t g_vic_table[] =
{
    {   1,  640,  480, 60 },
    {   2,  720,  480, 60 },
    {   3,  720,  480, 60 },
    {   4, 1280,  720, 60 },
    {   5, 1920, 1080, 60 },
    {   6,  720,  480, 60 },
    {   7,  720,  480, 60 },
    {   8,  720,  240, 60 },
    {   9,  720,  240, 60 },
    {  10, 2880,  480, 60 },
    {  11, 2880,  480, 60 },
    {  12, 2880,  240, 60 },
    {  13, 2880,  240, 60 },
    {  14, 1440,  480, 60 },
    {  15, 1440, 480, 60 },
    {  16, 1920, 1080, 60 },
    {  17,  720,  576, 50 },
    {  18,  720,  576, 50 },
    {  19, 1280, 720, 50 },
    {  20, 1920, 1080, 50 },
    {  31, 1920, 1080, 50 },
    {  32, 1920, 1080, 24 },
    {  33, 1920, 1080, 25 },
    {  34, 1920, 1080, 30 },
    {  93, 3840, 2160, 24 },
    {  94, 3840, 2160, 25 },
    {  95, 3840, 2160, 30 },
    {  96, 3840, 2160, 50 },
    {  97, 3840, 2160, 60 },
    {  98, 4096, 2160, 24 },
    {  99, 4096, 2160, 25 },
    { 100, 4096, 2160, 30 },
    { 101, 4096, 2160, 50 },
    { 102, 4096, 2160, 60 },
};

static int edid_checksum_ok(const uint8_t *block)
{
    uint8_t sum = 0;
    int i;

    for (i = 0; i < EDID_BLOCK_SIZE; i++)
        sum += block[i];

    return sum == 0;
}

static const edid_vic_t *edid_find_vic(uint8_t vic)
{
    unsigned int i;

    for (i = 0; i < sizeof(g_vic_table) / sizeof(g_vic_table[0]); i++)
    {
        if (g_vic_table[i].vic == vic)
            return &g_vic_table[i];
    }

    return NULL;
}

static void edid_add_mode(edid_info_t *info,
                          const edid_video_mode_t *mode)
{
    int i;

    if (info->mode_count >= EDID_MAX_VIDEO_MODES)
        return;

    for (i = 0; i < info->mode_count; i++)
    {
        edid_video_mode_t *old = &info->modes[i];

        if (old->width == mode->width &&
            old->height == mode->height &&
            old->refresh_rate == mode->refresh_rate &&
            old->interlaced == mode->interlaced)
        {
            if (mode->native)
                old->native = 1;

            /*
             * Keep the more detailed timing if the old entry
             * did not have a pixel clock.
             */
            if (old->pixel_clock_khz == 0 &&
                mode->pixel_clock_khz != 0)
            {
                *old = *mode;
            }

            return;
        }
    }

    info->modes[info->mode_count++] = *mode;
}

static void edid_parse_dtd(const uint8_t *dtd,
                           edid_video_mode_t *mode)
{
    uint16_t pixel_clock_10khz;

    memset(mode, 0, sizeof(*mode));

    pixel_clock_10khz =
        (uint16_t)dtd[0] |
        ((uint16_t)dtd[1] << 8);

    mode->pixel_clock_khz =
        (uint32_t)pixel_clock_10khz * 10;

    mode->h_active =
        (uint16_t)dtd[2] |
        ((uint16_t)(dtd[4] >> 4) << 8);

    mode->h_blank =
        (uint16_t)dtd[3] |
        ((uint16_t)(dtd[4] & 0x0F) << 8);

    mode->v_active =
        (uint16_t)dtd[5] |
        ((uint16_t)(dtd[7] >> 4) << 8);

    mode->v_blank =
        (uint16_t)dtd[6] |
        ((uint16_t)(dtd[7] & 0x0F) << 8);

    mode->h_front_porch =
        (uint16_t)dtd[8] |
        ((uint16_t)((dtd[11] >> 6) & 0x03) << 8);

    mode->h_sync =
        (uint16_t)dtd[9] |
        ((uint16_t)((dtd[11] >> 4) & 0x03) << 8);

    mode->v_front_porch =
        (uint16_t)((dtd[10] >> 4) & 0x0F) |
        ((uint16_t)((dtd[11] >> 2) & 0x03) << 4);

    mode->v_sync =
        (uint16_t)(dtd[10] & 0x0F) |
        ((uint16_t)(dtd[11] & 0x03) << 4);

    mode->h_total =
        mode->h_active + mode->h_blank;

    mode->v_total =
        mode->v_active + mode->v_blank;

    mode->h_back_porch =
        mode->h_blank - mode->h_front_porch - mode->h_sync;

    mode->v_back_porch =
        mode->v_blank - mode->v_front_porch - mode->v_sync;

    mode->width = mode->h_active;
    mode->height = mode->v_active;

    if (mode->h_total != 0 && mode->v_total != 0)
    {
        uint64_t pixel_clock_hz =
            (uint64_t)mode->pixel_clock_khz * 1000ULL;

        mode->refresh_rate =
            (uint32_t)(pixel_clock_hz /
                       ((uint32_t)mode->h_total *
                        (uint32_t)mode->v_total));
    }

    mode->interlaced = !!(dtd[17] & 0x80);
}

static void cta_parse_video_block(const uint8_t *data,
                                  uint8_t len,
                                  edid_info_t *info)
{
    uint8_t i;

    for (i = 0; i < len; i++)
    {
        uint8_t svd = data[i];
        uint8_t native = !!(svd & 0x80);
        uint8_t vic = svd & 0x7F;
        const edid_vic_t *v = edid_find_vic(vic);
        edid_video_mode_t mode;

        if (v == NULL)
        {
            printf("    VIC %u: unknown\n", vic);
            continue;
        }

        memset(&mode, 0, sizeof(mode));

        mode.vic = vic;
        mode.native = native;
        mode.width = v->width;
        mode.height = v->height;
        mode.refresh_rate = v->refresh_rate;

        printf("    VIC %u: %ux%u @ %uHz%s\n",
               vic,
               mode.width,
               mode.height,
               mode.refresh_rate,
               native ? " Native" : "");

        edid_add_mode(info, &mode);
    }
}

static void cta_parse_audio_block(const uint8_t *data,
                                  uint8_t len)
{
    uint8_t pos;

    for (pos = 0; pos + 2 < len; pos += 3)
    {
        uint8_t b0 = data[pos];
        uint8_t b1 = data[pos + 1];
        uint8_t b2 = data[pos + 2];

        uint8_t audio_format = (b0 >> 3) & 0x0F;
        uint8_t channels = (b0 & 0x07) + 1;

        printf("    Audio: format=%u channels=%u "
               "rates=0x%02X byte2=0x%02X\n",
               audio_format,
               channels,
               b1,
               b2);
    }
}

static void cta_parse_vsdb(const uint8_t *data,
                           uint8_t len)
{
    uint32_t oui;

    if (len < 3)
        return;

    /*
     * EDID OUI is stored LSB first.
     */
    oui =
        ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16);

    printf("    VSDB OUI: 0x%06X\n",
           (unsigned int)oui);

    if (oui == 0x000C03)
        printf("    HDMI VSDB\n");
}

static void cta_parse_extended_block(const uint8_t *data,
                                     uint8_t len)
{
    uint8_t ext_tag;

    if (len == 0)
        return;

    ext_tag = data[0];

    printf("    Extended Tag: 0x%02X\n", ext_tag);

    switch (ext_tag)
    {
    case 0x00:
        printf("    Video Capability Data Block\n");
        break;

    case 0x05:
        printf("    Colorimetry Data Block\n");
        break;

    case 0x06:
        printf("    HDR Static Metadata Data Block\n");
        break;

    default:
        printf("    Unknown Extended Data Block\n");
        break;
    }
}

static void cta_parse_data_blocks(const uint8_t *block,
                                  edid_info_t *info)
{
    uint8_t dtd_offset;
    uint8_t pos;

    dtd_offset = block[2];

    if (dtd_offset == 0)
    {
        printf("  CTA: no DTD\n");
        return;
    }

    if (dtd_offset < 4 || dtd_offset > 127)
    {
        printf("  CTA: invalid DTD offset: %u\n",
               dtd_offset);
        return;
    }

    pos = 4;

    while (pos < dtd_offset)
    {
        uint8_t header = block[pos];
        uint8_t tag = (header >> 5) & 0x07;
        uint8_t len = header & 0x1F;
        const uint8_t *data;

        if ((uint16_t)pos + 1U + len > dtd_offset)
        {
            printf("  CTA: Data Block overflow\n");
            break;
        }

        data = &block[pos + 1];

        printf("  CTA Data Block: tag=%u length=%u\n",
               tag, len);

        switch (tag)
        {
        case CTA_TAG_AUDIO:
            printf("    Type: Audio\n");
            cta_parse_audio_block(data, len);
            break;

        case CTA_TAG_VIDEO:
            printf("    Type: Video\n");
            cta_parse_video_block(data, len, info);
            break;

        case CTA_TAG_VENDOR:
            printf("    Type: Vendor Specific\n");
            cta_parse_vsdb(data, len);
            break;

        case CTA_TAG_SPEAKER:
            printf("    Type: Speaker Allocation\n");
            break;

        case CTA_TAG_EXTENDED:
            printf("    Type: Extended\n");
            cta_parse_extended_block(data, len);
            break;

        default:
            printf("    Type: Unknown\n");
            break;
        }

        pos += (uint8_t)(1 + len);
    }
}

static void cta_parse_dtds(const uint8_t *block,
                            edid_info_t *info)
{
    uint8_t dtd_offset;
    uint8_t pos;

    dtd_offset = block[2];

    if (dtd_offset == 0 || dtd_offset >= 127)
        return;

    pos = dtd_offset;

    while ((uint16_t)pos + 18U <= 127U)
    {
        const uint8_t *dtd = &block[pos];
        edid_video_mode_t mode;

        if (dtd[0] == 0x00 && dtd[1] == 0x00)
            break;

        edid_parse_dtd(dtd, &mode);

        printf("  CTA Detailed Timing:\n");
        printf("    Resolution : %ux%u\n",
               mode.h_active,
               mode.v_active);
        printf("    Pixel Clock: %u kHz\n",
               mode.pixel_clock_khz);
        printf("    HTotal     : %u\n",
               mode.h_total);
        printf("    VTotal     : %u\n",
               mode.v_total);
        printf("    Refresh    : %u Hz\n",
               mode.refresh_rate);
        printf("    Interlace  : %u\n",
               mode.interlaced);

        edid_add_mode(info, &mode);

        pos += 18;
    }
}

static void edid_parse_base_dtds(const uint8_t *edid,
                                 edid_info_t *info)
{
    int i;

    for (i = 0; i < 4; i++)
    {
        const uint8_t *dtd =
            &edid[0x36 + i * 18];

        edid_video_mode_t mode;

        if (dtd[0] == 0x00 && dtd[1] == 0x00)
        {
            uint8_t type = dtd[3];
            const uint8_t *data = &dtd[5];

            switch (type)
            {
            case 0xFF:
            {
                /* Monitor Serial Number (ASCII) */
                char str[14];
                int j;

                printf("Descriptor %d: Monitor Serial Number\n", i);
                for (j = 0; j < 13; j++)
                    str[j] = (data[j] >= 0x20 &&
                              data[j] <= 0x7E) ? data[j] : 0;
                str[13] = '\0';
                printf("  S/N: %s\n", str);
                break;
            }

            case 0xFE:
            {
                /* ASCII String */
                char str[14];
                int j;

                printf("Descriptor %d: ASCII String\n", i);
                for (j = 0; j < 13; j++)
                    str[j] = (data[j] >= 0x20 &&
                              data[j] <= 0x7E) ? data[j] : 0;
                str[13] = '\0';
                printf("  String: %s\n", str);
                break;
            }

            case 0xFC:
            {
                /* Monitor Name (ASCII) */
                char str[14];
                int j;

                printf("Descriptor %d: Monitor Name: ", i);
                for (j = 0; j < 13; j++)
                    str[j] = (data[j] >= 0x20 &&
                              data[j] <= 0x7E) ? data[j] : 0;
                str[13] = '\0';
                printf("%s\n", str);
                break;
            }

            case 0xFD:
            {
                /* Monitor Range Limits */
                uint8_t min_v = data[0];
                uint8_t max_v = data[1];
                uint8_t min_h = data[2];
                uint8_t max_h = data[3];
                uint8_t max_clock_m10 = data[4];
                uint8_t gtf_curve = data[5];

                printf("Descriptor %d: Monitor Range Limits\n", i);
                printf("  V Freq   : %u - %u Hz\n",
                       min_v, max_v);
                printf("  H Freq   : %u - %u kHz\n",
                       min_h, max_h);
                printf("  Max Clock: %u MHz\n",
                       (unsigned int)max_clock_m10 * 10);

                if (gtf_curve == 0x00)
                {
                    printf("  GTF      : None\n");
                }
                else if (gtf_curve == 0x02)
                {
                    uint8_t start_h = data[6];
                    uint8_t c_x2 = data[7];
                    uint16_t m = (uint16_t)data[8] |
                                ((uint16_t)data[9] << 8);
                    uint8_t k = data[10];
                    uint8_t j_x2 = data[11];

                    printf("  GTF      : Secondary\n");
                    printf("    Start  : %u kHz\n", start_h * 2);
                    printf("    C=%u.%u M=%u K=%u J=%u.%u\n",
                           c_x2 / 2, (c_x2 & 1) ? 5 : 0,
                           m, k,
                           j_x2 / 2, (j_x2 & 1) ? 5 : 0);
                }
                else
                {
                    printf("  GTF      : Unknown (0x%02X)\n",
                           gtf_curve);
                }
                break;
            }

            case 0xFB:
            {
                /* Color Point Data */
                int idx1 = data[0];
                int idx2 = data[6];

                printf("Descriptor %d: Color Point\n", i);

                if (idx1 != 0)
                {
                    uint16_t wx1 = ((uint16_t)data[2] << 2) |
                                   ((uint16_t)(data[1] >> 2) & 0x03);
                    uint16_t wy1 = ((uint16_t)data[3] << 2) |
                                   ((uint16_t)(data[1] >> 0) & 0x03);
                    uint8_t gamma1 = data[4];

                    printf("  WP[%d]: x=%u.%03u y=%u.%03u gamma=%u.%02u\n",
                           idx1,
                           wx1 / 1024, (wx1 * 1000 / 1024) % 1000,
                           wy1 / 1024, (wy1 * 1000 / 1024) % 1000,
                           (gamma1 + 100) / 100,
                           ((gamma1 + 100) * 100 / 100) % 100);
                }

                if (idx2 != 0)
                {
                    uint16_t wx2 = ((uint16_t)data[8] << 2) |
                                   ((uint16_t)(data[7] >> 2) & 0x03);
                    uint16_t wy2 = ((uint16_t)data[9] << 2) |
                                   ((uint16_t)(data[7] >> 0) & 0x03);
                    uint8_t gamma2 = data[10];

                    printf("  WP[%d]: x=%u.%03u y=%u.%03u gamma=%u.%02u\n",
                           idx2,
                           wx2 / 1024, (wx2 * 1000 / 1024) % 1000,
                           wy2 / 1024, (wy2 * 1000 / 1024) % 1000,
                           (gamma2 + 100) / 100,
                           ((gamma2 + 100) * 100 / 100) % 100);
                }
                break;
            }

            case 0xFA:
            {
                /* Standard Timing Identifiers (9-14) */
                int j;

                printf("Descriptor %d: Standard Timings\n", i);
                for (j = 0; j < 6; j++)
                {
                    uint8_t b0 = data[j * 2];
                    uint8_t b1 = data[j * 2 + 1];
                    uint16_t h_px;
                    uint16_t v_px;
                    uint8_t ar;
                    uint8_t rr;
                    const char *ar_s;

                    if (b0 == 0x00 && b1 == 0x00)
                        continue;

                    h_px = (uint16_t)((b0 + 31) * 8);
                    ar = (b1 >> 6) & 0x03;
                    rr = (b1 & 0x3F) + 60;

                    switch (ar)
                    {
                    case 0: ar_s = "16:10"; v_px = h_px * 10 / 16; break;
                    case 1: ar_s = "4:3";   v_px = h_px *  3 /  4; break;
                    case 2: ar_s = "5:4";   v_px = h_px *  4 /  5; break;
                    case 3: ar_s = "16:9";  v_px = h_px *  9 / 16; break;
                    default: ar_s = "?";    v_px = 0;               break;
                    }

                    printf("  [%d] %ux%u @ %uHz (AR %s)\n",
                           9 + j, h_px, v_px, rr, ar_s);
                }
                break;
            }

            default:
                if (type >= 0x00 && type <= 0x0F)
                {
                    printf("Base Descriptor %d: Manufacturer (0x%02X)\n",
                           i, type);
                    printf("  Data: %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X "
                           "%02X %02X %02X\n",
                           data[0], data[1], data[2], data[3], data[4],
                           data[5], data[6], data[7], data[8], data[9],
                           data[10], data[11], data[12]);
                }
                else
                {
                    printf("Base Descriptor %d: Type 0x%02X\n",
                           i, type);
                }
                break;
            }

            continue;
        }

        edid_parse_dtd(dtd, &mode);

        printf("Base Detailed Timing %d:\n", i);
        printf("  Resolution : %ux%u\n",
               mode.h_active,
               mode.v_active);
        printf("  Pixel Clock: %u kHz\n",
               mode.pixel_clock_khz);
        printf("  HTotal     : %u\n",
               mode.h_total);
        printf("  VTotal     : %u\n",
               mode.v_total);
        printf("  Refresh    : %u Hz\n",
               mode.refresh_rate);
        printf("  Interlace  : %u\n",
               mode.interlaced);

        edid_add_mode(info, &mode);
    }
}

static void edid_parse_block_map(const uint8_t *block,
                                 uint8_t block_index,
                                 uint8_t extension_count)
{
    uint16_t i;

    printf("  Extension Block Map\n");

    /*
     * Byte 1 maps to block 2, byte 2 maps to block 3, ...
     */
    for (i = 1; i < 127; i++)
    {
        uint16_t target_block =
            (uint16_t)block_index + i;

        if (target_block > extension_count)
            break;

        if (block[i] == 0)
            continue;

        printf("    Block %u -> Tag 0x%02X\n",
               target_block,
               block[i]);
    }
}

static void edid_parse_video_input(uint8_t vi,
                                   uint8_t revision)
{
    /*
     * EDID 1.3 & 1.4 — Offset 0x14: Video Input Definition
     *
     * Bit 7: 0 = analog, 1 = digital
     *
     * Analog (bit 7 = 0) — same for 1.3 and 1.4:
     *   Bits 6-5: Signal Level Standard
     *     00 = 0.700/0.300/1.000 V p-p
     *     01 = 0.714/0.286/1.000 V p-p
     *     10 = 1.000/0.400/1.400 V p-p
     *     11 = 0.700/0.000/0.700 V p-p
     *   Bit 4: Video Setup
     *     0 = blank level = black level
     *     1 = blank-to-black setup or pedestal
     *   Bit 3: Separate Sync H&V supported
     *   Bit 2: Composite Sync on Horizontal supported
     *   Bit 1: Composite Sync on Green Video supported
     *   Bit 0: Serrations on Vertical Sync supported
     *
     * Digital (bit 7 = 1), EDID 1.3:
     *   Bits 6-1: Reserved (must be 0)
     *   Bit 0: DFP 1.x — VESA DFP 1.x TMDS CRGB
     *
     * Digital (bit 7 = 1), EDID 1.4:
     *   Bits 6-4: Color Bit Depth
     *     8=undefined, 9=6bpc, 10=8bpc, 11=10bpc,
     *     12=12bpc, 13=14bpc, 14=16bpc, 15=reserved
     *   Bits 3-0: Digital Video Interface Standard
     *     0=undefined, 1=DVI, 2=HDMI-a, 3=HDMI-b,
     *     4=MDDI, 5=DisplayPort
     */
    int is_digital = !!(vi & 0x80);

    printf("Video Input  : 0x%02X (", vi);

    if (is_digital)
    {
        if (revision >= 4)
        {
            /* EDID 1.4 digital */
            uint8_t depth_raw = (vi >> 4) & 0x07;
            uint8_t iface = vi & 0x0F;
            const char *depth_str;
            const char *iface_str;

            switch (depth_raw)
            {
            case 0: depth_str = "undefined";     break;
            case 1: depth_str = "6 bpc";         break;
            case 2: depth_str = "8 bpc";         break;
            case 3: depth_str = "10 bpc";        break;
            case 4: depth_str = "12 bpc";        break;
            case 5: depth_str = "14 bpc";        break;
            case 6: depth_str = "16 bpc";        break;
            default: depth_str = "reserved";     break;
            }

            switch (iface)
            {
            case 0:  iface_str = "undefined";    break;
            case 1:  iface_str = "DVI";          break;
            case 2:  iface_str = "HDMI-a";       break;
            case 3:  iface_str = "HDMI-b";       break;
            case 4:  iface_str = "MDDI";         break;
            case 5:  iface_str = "DisplayPort";  break;
            default: iface_str = "reserved";     break;
            }

            printf("Digital, depth=%s, iface=%s)",
                   depth_str, iface_str);
        }
        else
        {
            /* EDID 1.3 digital */
            printf("Digital, DFP1.x=%d)",
                   !!(vi & 0x01));
        }
    }
    else
    {
        uint8_t level = (vi >> 5) & 0x03;
        const char *level_str;

        switch (level)
        {
        case 0: level_str = "0.700/0.300/1.000V";   break;
        case 1: level_str = "0.714/0.286/1.000V";   break;
        case 2: level_str = "1.000/0.400/1.400V";   break;
        case 3: level_str = "0.700/0.000/0.700V";   break;
        default: level_str = "unknown";             break;
        }

        printf("Analog, level=%s, "
               "setup=%d, sep_HV=%d comp_H=%d sync_G=%d serration=%d)",
               level_str,
               !!(vi & 0x10),
               !!(vi & 0x08),
               !!(vi & 0x04),
               !!(vi & 0x02),
               !!(vi & 0x01));
    }

    printf("\n");
}

static void edid_parse_image_size(uint8_t h_cm,
                                  uint8_t v_cm,
                                  uint8_t revision)
{
    printf("Image Size   : ");

    if (h_cm == 0 && v_cm == 0)
    {
        printf("Unknown\n");
        return;
    }

    if (revision >= 4)
    {
        /*
         * EDID 1.4, Table 3.12 — Screen Size or Aspect Ratio
         *
         * byte 16h ≠ 0 → byte 15h = H size (cm), byte 16h = V size (cm)
         * byte 16h = 0, byte 15h ≠ 0 → byte 15h = Landscape AR
         * byte 15h = 0, byte 16h ≠ 0 → byte 16h = Portrait AR
         * both = 0 → undefined
         */
        if (h_cm != 0 && v_cm != 0)
        {
            /* Screen size in cm */
            printf("%u cm x %u cm", h_cm, v_cm);
        }
        else if (v_cm == 0 && h_cm != 0)
        {
            /* Landscape aspect ratio: AR = (stored + 99) / 100 */
            uint32_t ar_100 = (uint32_t)h_cm + 99;

            printf("Aspect Ratio (Landscape) %u.%02u : 1",
                   (unsigned int)(ar_100 / 100),
                   (unsigned int)(ar_100 % 100));
        }
        else if (h_cm == 0 && v_cm != 0)
        {
            /*
             * Portrait aspect ratio: AR = 100 / (stored + 99)
             * Display as "1 : X" where X = 1/AR = (stored+99)/100
             */
            uint32_t x_100 = (uint32_t)v_cm + 99;

            printf("Aspect Ratio (Portrait) 1 : %u.%02u",
                   (unsigned int)(x_100 / 100),
                   (unsigned int)(x_100 % 100));
        }
        else
        {
            /* Both zero: undefined */
            printf("Unknown");
        }
    }
    else
    {
        /*
         * EDID 1.3: bytes 15h/16h are always screen size in cm.
         */
        printf("%u cm x %u cm", h_cm, v_cm);
    }

    printf("\n");
}

static void edid_parse_gamma(uint8_t gamma_raw)
{
    /*
     * EDID 1.3/1.4, Offset 0x17: Display Transfer Characteristic
     *
     * 01h → FEh: Gamma = (value + 100) / 100.0
     *             Range: 1.00 → 3.54
     * FFh:        Gamma not defined here; stored in extension block
     */
    printf("Gamma        : ");

    if (gamma_raw == 0xFF)
    {
        printf("Not defined (see extension block)\n");
    }
    else
    {
        uint32_t gamma_100 = (uint32_t)gamma_raw + 100;

        printf("%u.%02u\n",
               (unsigned int)(gamma_100 / 100),
               (unsigned int)(gamma_100 % 100));
    }
}

static void edid_parse_feature_support(uint8_t feat,
                                       uint8_t revision,
                                       uint8_t video_input)
{
    /*
     * EDID 1.3, Table 3.11 — Offset 0x18: Feature Support
     *
     * Bit 7: Standby (DPMS)
     * Bit 6: Suspend (DPMS)
     * Bit 5: Active Off / Very Low Power
     * Bits 4-3: Display Type
     *   00 = Monochrome / Grayscale
     *   01 = RGB Color
     *   10 = Non-RGB Multicolor
     *   11 = Undefined
     * Bit 2: sRGB standard default color space
     * Bit 1: Preferred Timing Mode (required in EDID 1.3+)
     * Bit 0: Default GTF supported
     *
     * EDID 1.4 — Offset 0x18: Feature Support
     *
     * Bits 7-5: DPMS (same as 1.3)
     *   Bit 7 = Standby, Bit 6 = Suspend, Bit 5 = Active-Off
     *
     * Bits 4-3: Depends on bit 7 at address 14h (video_input):
     *   If analog  (bit7=0): Display Color Type
     *     00 = Monochrome/Grayscale, 01 = RGB color
     *     10 = Non-RGB color,        11 = Undefined
     *   If digital (bit7=1): Supported Color Encoding Format
     *     00 = RGB 4:4:4
     *     01 = RGB 4:4:4 + YCrCb 4:4:4
     *     10 = RGB 4:4:4 + YCrCb 4:2:2
     *     11 = RGB 4:4:4 + YCrCb 4:4:4 + YCrCb 4:2:2
     *
     * Bit 2: sRGB is default color space
     * Bit 1: Preferred Timing Mode includes native pixel format
     * Bit 0: Display is continuous frequency (0 = multi-mode)
     */
    int is_digital;
    const char *type_str;

    printf("Feature      : 0x%02X\n", feat);

    /* Bits 7-5: DPMS — same for both 1.3 and 1.4 */
    printf("  DPMS       :%s%s%s\n",
           !!(feat & 0x80) ? " Standby" : "",
           !!(feat & 0x40) ? " Suspend" : "",
           !!(feat & 0x20) ? " Active-Off" : "");

    is_digital = !!(video_input & 0x80);

    if (revision >= 4)
    {
        /* EDID 1.4 */
        uint8_t bits_43 = (feat >> 3) & 0x03;

        if (is_digital)
        {
            /* Bits 4-3: Supported Color Encoding Format */
            switch (bits_43)
            {
            case 0: type_str = "RGB 4:4:4";                            break;
            case 1: type_str = "RGB 4:4:4 + YCrCb 4:4:4";            break;
            case 2: type_str = "RGB 4:4:4 + YCrCb 4:2:2";            break;
            default: type_str = "RGB 4:4:4 + YCrCb 4:4:4 + 4:2:2";  break;
            }
            printf("  Color Enc. : %s\n", type_str);
        }
        else
        {
            /* Bits 4-3: Display Color Type */
            switch (bits_43)
            {
            case 0: type_str = "Monochrome/Grayscale"; break;
            case 1: type_str = "RGB color";            break;
            case 2: type_str = "Non-RGB color";        break;
            default: type_str = "Undefined";           break;
            }
            printf("  Color Type : %s\n", type_str);
        }

        printf("  sRGB       : %s\n",
               !!(feat & 0x04) ? "Default color space" : "Not default");
        printf("  Pref Timing: %s\n",
               !!(feat & 0x02) ? "Includes native pixel format" : "No preferred");
        printf("  Frequency  : %s\n",
               !!(feat & 0x01) ? "Continuous" : "Non-continuous (multi-mode)");
    }
    else
    {
        /* EDID 1.3 */
        uint8_t display_type = (feat >> 3) & 0x03;

        switch (display_type)
        {
        case 0: type_str = "Monochrome/Grayscale"; break;
        case 1: type_str = "RGB color";            break;
        case 2: type_str = "Non-RGB multicolor";   break;
        default: type_str = "Undefined";           break;
        }

        printf("  Display Type: %s\n", type_str);
        printf("  sRGB       : %s\n",
               !!(feat & 0x04) ? "Standard default color space" : "Not sRGB");
        printf("  Pref Timing: %s\n",
               !!(feat & 0x02) ? "Preferred timing mode" : "No preferred");
        printf("  Default GTF: %s\n",
               !!(feat & 0x01) ? "Supported" : "Not supported");
    }
}

static void edid_decode_manufacturer_id(uint16_t id,
                                       char *out3)
{
    /*
     * EDID Manufacturer ID (offsets 08h-09h):
     * 15 bits, 3 groups of 5 bits, each: 0=unused, 1=A ... 26=Z
     * Bits 14-10: char1, Bits 9-5: char2, Bits 4-0: char3
     */
    out3[0] = (id >> 10) & 0x1F;
    out3[1] = (id >> 5) & 0x1F;
    out3[2] = id & 0x1F;

    out3[0] = out3[0] ? 'A' + out3[0] - 1 : ' ';
    out3[1] = out3[1] ? 'A' + out3[1] - 1 : ' ';
    out3[2] = out3[2] ? 'A' + out3[2] - 1 : ' ';
    out3[3] = '\0';
}

static void edid_parse_color_characteristics(const uint8_t *edid,
                                           edid_color_info_t *ci)
{
    /*
     * EDID 1.3/1.4, Offsets 19h-22h: Color Characteristics
     *
     * Byte 19h: Rx[1:0] Ry[1:0] Gx[1:0] Gy[1:0]  (low 2 bits each)
     * Byte 1Ah: Bx[1:0] By[1:0] Wx[1:0] Wy[1:0]  (low 2 bits each)
     * Bytes 1Bh-22h: High 8 bits of each coordinate
     *
     * Each coordinate = (high_8 << 2) | low_2  (10-bit, 0-1023)
     * CIE value = coordinate / 1024.0
     */
    ci->red_x =
        ((uint16_t)edid[0x1B] << 2) |
        ((uint16_t)(edid[0x19] >> 6) & 0x03);
    ci->red_y =
        ((uint16_t)edid[0x1C] << 2) |
        ((uint16_t)(edid[0x19] >> 4) & 0x03);
    ci->green_x =
        ((uint16_t)edid[0x1D] << 2) |
        ((uint16_t)(edid[0x19] >> 2) & 0x03);
    ci->green_y =
        ((uint16_t)edid[0x1E] << 2) |
        ((uint16_t)(edid[0x19] >> 0) & 0x03);
    ci->blue_x =
        ((uint16_t)edid[0x1F] << 2) |
        ((uint16_t)(edid[0x1A] >> 6) & 0x03);
    ci->blue_y =
        ((uint16_t)edid[0x20] << 2) |
        ((uint16_t)(edid[0x1A] >> 4) & 0x03);
    ci->white_x =
        ((uint16_t)edid[0x21] << 2) |
        ((uint16_t)(edid[0x1A] >> 2) & 0x03);
    ci->white_y =
        ((uint16_t)edid[0x22] << 2) |
        ((uint16_t)(edid[0x1A] >> 0) & 0x03);

    printf("Color Chars  : ");
    printf("R(%u,%u) G(%u,%u) B(%u,%u) W(%u,%u)\n",
           ci->red_x, ci->red_y,
           ci->green_x, ci->green_y,
           ci->blue_x, ci->blue_y,
           ci->white_x, ci->white_y);

    printf("  Red        : x=%u.%03u y=%u.%03u\n",
           ci->red_x / 1024, (ci->red_x * 1000 / 1024) % 1000,
           ci->red_y / 1024, (ci->red_y * 1000 / 1024) % 1000);
    printf("  Green      : x=%u.%03u y=%u.%03u\n",
           ci->green_x / 1024, (ci->green_x * 1000 / 1024) % 1000,
           ci->green_y / 1024, (ci->green_y * 1000 / 1024) % 1000);
    printf("  Blue       : x=%u.%03u y=%u.%03u\n",
           ci->blue_x / 1024, (ci->blue_x * 1000 / 1024) % 1000,
           ci->blue_y / 1024, (ci->blue_y * 1000 / 1024) % 1000);
    printf("  White      : x=%u.%03u y=%u.%03u\n",
           ci->white_x / 1024, (ci->white_x * 1000 / 1024) % 1000,
           ci->white_y / 1024, (ci->white_y * 1000 / 1024) % 1000);
}

static void edid_parse_established_timings(const uint8_t *edid)
{
    /*
     * EDID 1.3/1.4, Offsets 23h-25h: Established Timings
     *
     * Byte 23h (Established Timing I):
     *   Bit 7: 720x400 @ 70Hz   (IBM VGA)
     *   Bit 6: 720x400 @ 88Hz   (IBM XGA2)
     *   Bit 5: 640x480 @ 60Hz   (IBM VGA)
     *   Bit 4: 640x480 @ 67Hz   (Apple Mac II)
     *   Bit 3: 640x480 @ 72Hz   (VESA)
     *   Bit 2: 640x480 @ 75Hz   (VESA)
     *   Bit 1: 800x600 @ 56Hz   (VESA)
     *   Bit 0: 800x600 @ 60Hz   (VESA)
     *
     * Byte 24h (Established Timing II):
     *   Bit 7: 800x600 @ 72Hz   (VESA)
     *   Bit 6: 800x600 @ 75Hz   (VESA)
     *   Bit 5: 832x624 @ 75Hz   (Apple Mac II)
     *   Bit 4: 1024x768 @ 87Hz(I) (IBM)
     *   Bit 3: 1024x768 @ 60Hz  (VESA)
     *   Bit 2: 1024x768 @ 70Hz  (VESA)
     *   Bit 1: 1024x768 @ 75Hz  (VESA)
     *   Bit 0: 1280x1024 @ 75Hz (VESA)
     *
     * Byte 25h (Manufacturer's Timings):
     *   Bit 7: 1152x870 @ 75Hz  (Apple Mac II)
     *   Bits 6-0: Reserved
     */
    uint8_t b0 = edid[0x23];
    uint8_t b1 = edid[0x24];
    uint8_t b2 = edid[0x25];
    int count = 0;

    printf("Established Timing: 0x%02X%02X%02X\n", b0, b1, b2);

    if (b0 == 0 && b1 == 0 && b2 == 0)
    {
        printf("  (none)\n");
        return;
    }

    printf("  ");
    if (b0 & 0x80) { printf("720x400@70 "); count++; }
    if (b0 & 0x40) { printf("720x400@88 "); count++; }
    if (b0 & 0x20) { printf("640x480@60 "); count++; }
    if (b0 & 0x10) { printf("640x480@67 "); count++; }
    if (b0 & 0x08) { printf("640x480@72 "); count++; }
    if (b0 & 0x04) { printf("640x480@75 "); count++; }
    if (b0 & 0x02) { printf("800x600@56 "); count++; }
    if (b0 & 0x01) { printf("800x600@60 "); count++; }

    if (b1 & 0x80) { printf("800x600@72 "); count++; }
    if (b1 & 0x40) { printf("800x600@75 "); count++; }
    if (b1 & 0x20) { printf("832x624@75 "); count++; }
    if (b1 & 0x10) { printf("1024x768@87i "); count++; }
    if (b1 & 0x08) { printf("1024x768@60 "); count++; }
    if (b1 & 0x04) { printf("1024x768@70 "); count++; }
    if (b1 & 0x02) { printf("1024x768@75 "); count++; }
    if (b1 & 0x01) { printf("1280x1024@75 "); count++; }

    if (b2 & 0x80) { printf("1152x870@75 "); count++; }

    printf("\n");
    printf("  Total: %d timings\n", count);
}

static void edid_parse_standard_timings(const uint8_t *edid)
{
    /*
     * EDID 1.3/1.4, Offsets 26h-35h: Standard Timings
     *
     * 8 entries, each 2 bytes:
     *
     * Byte 0: (Horizontal active pixels / 8) - 31
     *   horizontal_pixels = (byte0 + 31) * 8
     *   Range: 256 → 2288 pixels (increments of 8)
     *
     * Byte 1:
     *   Bits 7-6: Aspect Ratio
     *     00 = 16:10 (pre-EDID 1.3: 1:1)
     *     01 = 4:3
     *     10 = 5:4
     *     11 = 16:9
     *   Bits 5-0: Refresh Rate - 60
     *     Range: 60 → 123 Hz
     *
     * If both bytes are 0x00, the entry is unused.
     */
    int i;
    int count = 0;

    printf("Standard Timings:\n");

    for (i = 0; i < 8; i++)
    {
        uint8_t b0 = edid[0x26 + i * 2];
        uint8_t b1 = edid[0x26 + i * 2 + 1];
        uint16_t h_pixels;
        uint16_t v_pixels;
        uint8_t aspect;
        uint8_t refresh;
        const char *ar_str;

        if (b0 == 0x00 && b1 == 0x00)
            continue;

        h_pixels = (uint16_t)((b0 + 31) * 8);
        aspect = (b1 >> 6) & 0x03;
        refresh = (b1 & 0x3F) + 60;

        /*
         * Calculate vertical resolution from horizontal pixels
         * and aspect ratio.  Assumes square pixels (1:1 PAR).
         *
         * 16:10 → v = h * 10 / 16
         *  4:3  → v = h *  3 /  4
         *  5:4  → v = h *  4 /  5
         * 16:9  → v = h *  9 / 16
         */
        switch (aspect)
        {
        case 0: ar_str = "16:10"; v_pixels = h_pixels * 10 / 16; break;
        case 1: ar_str = "4:3";   v_pixels = h_pixels *  3 /  4; break;
        case 2: ar_str = "5:4";   v_pixels = h_pixels *  4 /  5; break;
        case 3: ar_str = "16:9";  v_pixels = h_pixels *  9 / 16; break;
        default: ar_str = "?";    v_pixels = 0;                   break;
        }

        printf("  [%d] %ux%u @ %uHz (AR %s)\n",
               i, h_pixels, v_pixels, refresh, ar_str);
        count++;
    }

    if (count == 0)
        printf("  (none)\n");
}

static void edid_parse_basic_display_params(const uint8_t *edid,
                                           edid_info_t *info)
{
    info->video_input = edid[0x14];
    info->horizontal_size_cm = edid[0x15];
    info->vertical_size_cm = edid[0x16];
    info->gamma = edid[0x17];
    info->feature_support = edid[0x18];

    edid_parse_video_input(info->video_input, info->revision);
    edid_parse_image_size(info->horizontal_size_cm,
                          info->vertical_size_cm,
                          info->revision);
    edid_parse_gamma(info->gamma);
    edid_parse_feature_support(info->feature_support,
                               info->revision,
                               info->video_input);
}

int edid_parse(const uint8_t *edid,
               uint32_t edid_size,
               edid_info_t *info)
{
    static const uint8_t header[8] =
    {
        0x00, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0x00
    };

    uint32_t required_size;
    uint8_t i;

    if (edid == NULL || info == NULL)
        return -1;

    if (edid_size < EDID_BLOCK_SIZE)
        return -2;

    memset(info, 0, sizeof(*info));

    if (memcmp(edid, header, sizeof(header)) != 0)
    {
        printf("EDID header error\n");
        return -3;
    }

    if (!edid_checksum_ok(edid))
    {
        printf("EDID base checksum error\n");
        return -4;
    }

    info->version = edid[0x12];
    info->revision = edid[0x13];

    info->manufacturer_id =
        ((uint16_t)edid[0x08] << 8) |
        edid[0x09];

    info->product_code =
        (uint16_t)edid[0x0A] |
        ((uint16_t)edid[0x0B] << 8);

    info->serial_number =
        (uint32_t)edid[0x0C] |
        ((uint32_t)edid[0x0D] << 8) |
        ((uint32_t)edid[0x0E] << 16) |
        ((uint32_t)edid[0x0F] << 24);

    info->week = edid[0x10];
    info->year = edid[0x11];

    info->extension_count = edid[0x7E];

    required_size =
        ((uint32_t)info->extension_count + 1U) *
        EDID_BLOCK_SIZE;

    printf("\n========== EDID ==========\n");
    {
        char mfr[4];

        edid_decode_manufacturer_id(info->manufacturer_id, mfr);

        printf("Version      : %u.%u\n",
               info->version,
               info->revision);
        printf("Manufacturer     : %s (0x%04X)\n",
               mfr, info->manufacturer_id);
        printf("Product Code     : 0x%04X\n",
               info->product_code);
        printf("Serial Number    : 0x%08X\n",
               info->serial_number);
        printf("Week of Manufacture: %u\n",
               info->week);
        printf("Year of Manufacture: %u\n",
               1990 + info->year);
        printf("Extension blocks     : %u\n",
               info->extension_count);
    }
    printf("Total Size   : %u bytes\n",
           (unsigned int)required_size);

    if (edid_size < required_size)
    {
        printf("EDID incomplete: need %u bytes, got %u\n",
               (unsigned int)required_size,
               (unsigned int)edid_size);
        return -5;
    }

    edid_parse_basic_display_params(edid, info);
    edid_parse_color_characteristics(edid, &info->color_info);
    edid_parse_established_timings(edid);
    edid_parse_standard_timings(edid);
    edid_parse_base_dtds(edid, info);

    for (i = 0; i < info->extension_count; i++)
    {
        uint8_t block_index = (uint8_t)(i + 1);
        const uint8_t *block =
            &edid[(uint32_t)block_index * EDID_BLOCK_SIZE];

        uint8_t tag = block[0];

        printf("\n========== Block %u ==========\n",
               block_index);
        printf("Tag      : 0x%02X\n", tag);
        printf("Revision : 0x%02X\n", block[1]);

        if (!edid_checksum_ok(block))
        {
            printf("Checksum : ERROR\n");
            continue;
        }

        printf("Checksum : OK\n");

        switch (tag)
        {
        case EDID_EXT_CTA:
            printf("Type     : CTA-861\n");
            cta_parse_data_blocks(block, info);
            cta_parse_dtds(block, info);
            break;

        case EDID_EXT_BLOCK_MAP:
            printf("Type     : Extension Block Map\n");
            edid_parse_block_map(block,
                                 block_index,
                                 info->extension_count);
            break;

        case EDID_EXT_DISPLAYID:
            printf("Type     : DisplayID\n");
            printf("DisplayID parser not implemented yet.\n");
            break;

        default:
            printf("Type     : Unknown Extension\n");
            break;
        }
    }

    printf("\n========== Video Modes ==========\n");

    for (i = 0; i < info->mode_count; i++)
    {
        const edid_video_mode_t *m = &info->modes[i];

        printf("[%02u] %ux%u @ %uHz",
               i,
               m->width,
               m->height,
               m->refresh_rate);

        if (m->pixel_clock_khz != 0)
            printf(" PixelClock=%u kHz",
                   m->pixel_clock_khz);

        if (m->vic != 0)
            printf(" VIC=%u", m->vic);

        if (m->native)
            printf(" Native");

        printf("\n");
    }

    return 0;
}

const edid_video_mode_t *
edid_get_native_mode(const edid_info_t *info)
{
    uint8_t i;

    if (info == NULL || info->mode_count == 0)
        return NULL;

    for (i = 0; i < info->mode_count; i++)
    {
        if (info->modes[i].native)
            return &info->modes[i];
    }

    /*
     * No native flag:
     * the first parsed mode is used as fallback.
     */
    return &info->modes[0];
}
