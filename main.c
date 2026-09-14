#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "edid_parser.h"

#define EDID_MAX_SIZE    4096

/* Forward declarations */
static uint8_t *read_hex_text(const char *path, uint32_t *out_size);
static uint8_t *read_binary(const char *path, uint32_t *out_size);

/*
 * Read EDID bytes from a binary file.
 * The file is raw EDID bytes, same as the wire format.
 */
static uint8_t *read_binary(const char *path, uint32_t *out_size)
{
    FILE *fp;
    uint8_t *buf;
    long file_size;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", path);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || file_size > EDID_MAX_SIZE)
    {
        fprintf(stderr, "File size invalid: %ld bytes\n", file_size);
        fclose(fp);
        return NULL;
    }

    buf = (uint8_t *)malloc((size_t)file_size);
    if (buf == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        fclose(fp);
        return NULL;
    }

    if (fread(buf, 1, (size_t)file_size, fp) != (size_t)file_size)
    {
        fprintf(stderr, "Read error\n");
        free(buf);
        fclose(fp);
        return NULL;
    }

    fclose(fp);
    *out_size = (uint32_t)file_size;
    return buf;
}

/*
 * Auto-detect file format and load EDID data.
 *
 * Detection rules:
 *   1. Extension .bin/.dat → binary format
 *   2. First bytes are 0x00 0xFF → binary format (EDID header)
 *   3. Otherwise → hex text format
 */
static uint8_t *edid_load_file(const char *path, uint32_t *out_size)
{
    size_t path_len;

    /* Rule 1: check file extension */
    path_len = strlen(path);

    if (path_len >= 4)
    {
        const char *ext = path + path_len - 4;

        if (strcmp(ext, ".bin") == 0 ||
            strcmp(ext, ".dat") == 0)
        {
            return read_binary(path, out_size);
        }
    }

    /* Rule 2: try reading first 2 bytes as binary */
    {
        FILE *fp = fopen(path, "rb");

        if (fp != NULL)
        {
            uint8_t header[2];
            int is_binary = 0;

            if (fread(header, 1, 2, fp) == 2)
            {
                if (header[0] == 0x00 && header[1] == 0xFF)
                    is_binary = 1;
            }

            fclose(fp);

            if (is_binary)
                return read_binary(path, out_size);
        }
    }

    /* Rule 3: default to hex text format */
    return read_hex_text(path, out_size);
}

static int is_bin_file(const char *path)
{
    size_t len = strlen(path);

    if (len >= 4)
    {
        const char *ext = path + len - 4;

        if (strcmp(ext, ".bin") == 0 ||
            strcmp(ext, ".dat") == 0)
        {
            return 1;
        }
    }

    return 0;
}

/*
 * Read EDID bytes from a text file.
 * The file may contain hex values in any common format:
 *   0x00, 0xFF, ...    (comma-separated)
 *   { 0x00, 0xFF, ...} (C array style)
 * Non-hex characters (letters, spaces, commas, braces, etc.)
 * are treated as separators.
 */
static uint8_t *read_hex_text(const char *path, uint32_t *out_size)
{
    FILE *fp;
    uint8_t *buf;
    uint32_t count = 0;
    unsigned int val;
    int ch;
    int have_digit;

    fp = fopen(path, "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot open %s\n", path);
        return NULL;
    }

    buf = (uint8_t *)malloc(EDID_MAX_SIZE);
    if (buf == NULL)
    {
        fprintf(stderr, "Out of memory\n");
        fclose(fp);
        return NULL;
    }

    /*
     * State machine:
     *   waiting  - looking for '0' to start "0x" prefix
     *   prefix   - saw '0', looking for 'x' or 'X'
     *   hex      - reading hex digits
     */
    enum { ST_WAITING, ST_PREFIX, ST_HEX } state = ST_WAITING;
    have_digit = 0;
    val = 0;

    while ((ch = fgetc(fp)) != EOF)
    {
        if (state == ST_WAITING)
        {
            if (ch == '0')
            {
                state = ST_PREFIX;
            }
        }
        else if (state == ST_PREFIX)
        {
            if (ch == 'x' || ch == 'X')
            {
                state = ST_HEX;
                val = 0;
                have_digit = 0;
            }
            else
            {
                /* Not "0x", go back to waiting */
                state = ST_WAITING;
            }
        }
        else /* ST_HEX */
        {
            if (ch >= '0' && ch <= '9')
            {
                val = (val << 4) | (unsigned int)(ch - '0');
                have_digit = 1;
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                val = (val << 4) | (unsigned int)(ch - 'a' + 10);
                have_digit = 1;
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                val = (val << 4) | (unsigned int)(ch - 'A' + 10);
                have_digit = 1;
            }
            else
            {
                /* Non-hex char: end of current token */
                if (have_digit)
                {
                    if (count < EDID_MAX_SIZE)
                        buf[count++] = (uint8_t)(val & 0xFF);
                }
                state = ST_WAITING;
            }
        }
    }

    /* Flush last token if file ends with a hex digit */
    if (state == ST_HEX && have_digit)
    {
        if (count < EDID_MAX_SIZE)
            buf[count++] = (uint8_t)(val & 0xFF);
    }

    fclose(fp);

    if (count == 0)
    {
        fprintf(stderr, "No hex data found in %s\n", path);
        free(buf);
        return NULL;
    }

    *out_size = count;
    return buf;
}

int main(int argc, char *argv[])
{
    const char *path;
    uint32_t edid_size;
    uint8_t *edid;
    edid_info_t info;
    int ret;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <edid_file>\n", argv[0]);
        fprintf(stderr, "  Supported formats: .txt (hex text), .bin/.dat (binary)\n");
        return 1;
    }

    path = argv[1];

    printf("Reading EDID from: %s\n\n", path);

    edid = edid_load_file(path, &edid_size);
    if (edid == NULL)
        return 1;

    printf("File size: %u bytes\n\n", edid_size);

    /* If input is .bin/.dat, export corresponding .txt file */
    if (is_bin_file(path))
    {
        size_t path_len = strlen(path);
        char *txt_path = (char *)malloc(path_len + 1); /* +1 for '\0' */

        if (txt_path != NULL)
        {
            FILE *fp;
            uint32_t i;
            int first = 1;

            strcpy(txt_path, path);

            /* Replace .bin/.dat extension with .txt */
            txt_path[path_len - 3] = 't';
            txt_path[path_len - 2] = 'x';
            txt_path[path_len - 1] = 't';

            fp = fopen(txt_path, "w");
            if (fp != NULL)
            {
                for (i = 0; i < edid_size; i++)
                {
                    if (!first)
                        fprintf(fp, ", ");

                    if (i % 16 == 0)
                        fprintf(fp, "\n    ");

                    fprintf(fp, "0x%02X", edid[i]);
                    first = 0;
                }
                fprintf(fp, "\n");

                fclose(fp);
                printf("Exported: %s (%u bytes)\n\n", txt_path, edid_size);
            }
            else
            {
                fprintf(stderr, "Cannot create %s\n", txt_path);
            }

            free(txt_path);
        }
    }
    /* If input is .txt, export corresponding .bin file */
    else
    {
        size_t path_len = strlen(path);
        char *bin_path = (char *)malloc(path_len + 5); /* +5 for ".bin\0" */
        FILE *fp;

        if (bin_path != NULL)
        {
            strcpy(bin_path, path);

            /* Replace .txt extension with .bin */
            bin_path[path_len - 4] = '\0';
            strcat(bin_path, ".bin");

            fp = fopen(bin_path, "wb");
            if (fp != NULL)
            {
                fwrite(edid, 1, edid_size, fp);
                fclose(fp);
                printf("Exported: %s (%u bytes)\n\n", bin_path, edid_size);
            }
            else
            {
                fprintf(stderr, "Cannot create %s\n", bin_path);
            }

            free(bin_path);
        }
    }

    ret = edid_parse(edid, edid_size, &info);

    printf("\n========== Summary ==========\n");
    printf("Parse result : %d\n", ret);

    if (ret == 0)
    {
        const edid_video_mode_t *native;

        printf("Total modes  : %u\n", info.mode_count);

        native = edid_get_native_mode(&info);
        if (native != NULL)
        {
            printf("Native mode  : %ux%u @ %uHz",
                   native->width,
                   native->height,
                   native->refresh_rate);

            if (native->vic != 0)
                printf(" VIC=%u", native->vic);

            printf("\n");
        }
    }

    free(edid);
    return 0;
}
