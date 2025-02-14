/**
 * @file
 * @brief NNG Mbim Manager helpers
 * @ccmod{MBIM_X_MMG}
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "databuf.h"

#define CHUNK_SIZE 512

typedef struct data_var
{
    uint32_t type;
    uint32_t size;
} Data_var;

/**
 * Reallocate memory for the data buffer
 *
 * @param buf Pointer to the data buffer structure
 * @param size Desired new size of the buffer
 *
 * @return True on success, otherwise false
 */
static bool databuf_realloc(Databuf *buf, size_t size)
{
    unsigned char *new_buf;
    size_t new_size = buf->size;

    while (size > new_size)
        new_size += CHUNK_SIZE;

    new_buf = (unsigned char *) realloc(buf->buf, new_size);
    if (!new_buf)
        return false;

    buf->buf = new_buf;
    buf->size = new_size;

    return true;
}

/**
 * Add data to the data buffer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param value Pointer to the data to be added
 * @param len Length of the data
 *
 * @return Number of bytes added, or 0 on failure
 */
static size_t databuf_add(Databuf *buf, unsigned int var, const void *value, size_t len)
{
    Data_var *header;
    Data_var datavar;
    size_t size;

    if (!buf || !buf->buf)
        return 0;

    size = buf->len + len + sizeof(datavar);
    if (size > buf->size && !databuf_realloc(buf, size))
        return 0;

    datavar.type = var;
    datavar.size = len;

    memcpy(buf->buf + buf->len, &datavar, sizeof(datavar));
    buf->len += sizeof(datavar);

    if (value && len > 0)
    {
        memcpy(buf->buf + buf->len, value, len);
        buf->len += len;
    }

    // Update packet length
    header = (Data_var *) buf->buf;
    header->size += len + sizeof(datavar);

    return len;
}

/**
 * Retrieve data from the data buffer based on variable identifier and previous value pointer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param prev Pointer to the previous value used for searching (NULL if starting from beginning)
 *
 * @return Pointer to the retrieved data, or NULL if not found
 */
static unsigned char *databuf_get(Databuf *buf, unsigned int var, unsigned char *prev)
{
    int offset;
    Data_var data = {0};
    int data_len = sizeof(data);
    unsigned char *value;

    if (!buf || !buf->buf)
        return NULL;

    offset = data_len;
    while (offset < buf->len)
    {
        memcpy(&data, buf->buf + offset, data_len);
        if (var == data.type)
        {
            value = buf->buf + offset + data_len;
            if (!prev || prev < value)
                return value;
        }

        offset += data.size + data_len;
    }

    return NULL;
}

/**
 * Initialize a new data buffer with initial size and header
 *
 * @param buf Pointer to the data buffer structure
 *
 * @return True on success, otherwise false
 */
bool databuf_init(Databuf *buf)
{
    Data_var *header;
    buf->buf = (unsigned char *) calloc(1, CHUNK_SIZE);
    if (!buf->buf)
        return false;

    buf->size = CHUNK_SIZE;
    buf->len = sizeof(Data_var);

    header = (Data_var *) buf->buf;
    header->type = DT_RAW;
    header->size = 0;

    return true;
}

/**
 * Set the buffer for the data buffer structure
 *
 * @param buf Pointer to the data buffer structure
 * @param buffer Pointer to the new buffer
 * @param size Size of the new buffer
 */
void databuf_set_buf(Databuf *buf, unsigned char *buffer, size_t size)
{
    databuf_free(buf);

    buf->buf = buffer;
    buf->size = size;
    buf->len = size;
}

/**
 * Add a string to the data buffer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param value Pointer to the string to be added
 */
void databuf_add_string(Databuf *buf, unsigned int var, const char *value)
{
    int len = 0;

    if ((var & 0xff) != DT_STRING)
    {
        printf("Error: databuf var %u is not of type string\n", var);
        return;
    }

    if (value)
        len = strlen(value) + 1;

    databuf_add(buf, var, value, len);
}

/**
 * Add an unsigned integer to the data buffer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param value The unsigned integer to be added
 */
void databuf_add_uint(Databuf *buf, unsigned int var, unsigned int value)
{
    if ((var & 0xff) != DT_UINT)
    {
        printf("Error: databuf var %u is not of type uint\n", var);
        return;
    }

    databuf_add(buf, var, &value, sizeof(unsigned int));
}

/**
 * Check if the data buffer is valid
 *
 * @param buf Pointer to the data buffer structure
 *
 * @return True if the buffer is valid, otherwise false
 */
bool databuf_is_valid(Databuf *buf)
{
    int offset = 0;
    Data_var data = {0};
    int data_len = sizeof(data);

    if (!buf || !buf->buf)
        return false;

    if (buf->len < data_len)
        return false;

    memcpy(&data, buf->buf, data_len);
    offset += data_len;

    if (data.size != (buf->len - data_len) || (data.type & 0xff) != DT_RAW)
    {
        printf("Error : databuf wrong message type/length. Expected %u but got %zu\n", data.size, (buf->len - data_len));
        return false;
    }

    while (offset < buf->len)
    {
        memcpy(&data, buf->buf + offset, data_len);

        if ((data.size + offset + data_len) > buf->len)
        {
            printf("Error: databuf wrong data size for %04x (offset %d)\n", data.type, offset);
            return false;
        }

        offset += data_len + data.size;
    }

    return true;
}

/**
 * Retrieve the next string from the data buffer based on variable identifier and previous value pointer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param prev Pointer to the previous value used for searching (NULL if starting from beginning)
 *
 * @return Pointer to the retrieved string, or NULL if not found
 */
char *databuf_get_next_string(Databuf *buf, unsigned int var, unsigned char *prev)
{
    if ((var & 0xff) != DT_STRING)
    {
        printf("Error: databuf var %u is not of type string\n", var);
        return NULL;
    }

    return (char *) databuf_get(buf, var, prev);
}

/**
 * Retrieve a string from the data buffer based on variable identifier
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 *
 * @return Pointer to the retrieved string
 */
char *databuf_get_string(Databuf *buf, unsigned int var)
{
    return databuf_get_next_string(buf, var, NULL);
}

/**
 * Retrieve the next unsigned integer from the data buffer based on variable identifier and previous value pointer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param value Pointer to store the retrieved unsigned integer
 * @param prev Pointer to the previous value used for searching (NULL if starting from beginning)
 *
 * @return Pointer to the retrieved unsigned integer, or NULL if not found
 */
char *databuf_get_next_uint(Databuf *buf, unsigned int var, unsigned int *value, unsigned char *prev)
{
    unsigned char *buffer;

    if ((var & 0xff) != DT_UINT)
    {
        printf("Error: databuf var %u is not of type unsigned int\n", var);
        return NULL;
    }

    buffer = databuf_get(buf, var, prev);
    if (!buffer)
        return NULL;

    memcpy(value, buffer, sizeof(unsigned int));

    return (char *) buffer;
}

/**
 * Retrieve an unsigned integer from the data buffer based on variable identifier and previous value pointer
 *
 * @param buf Pointer to the data buffer structure
 * @param var Variable identifier for the data type
 * @param value Pointer to store the retrieved unsigned integer
 *
 * @return Pointer to the retrieved unsigned integer
 */
char *databuf_get_uint(Databuf *buf, unsigned int var, unsigned int *value)
{
    return databuf_get_next_uint(buf, var, value, NULL);
}

/**
 * Free the memory allocated for the data buffer
 *
 * @param buf Pointer to the data buffer structure
 */
void databuf_free(Databuf *buf)
{
    if (!buf)
        return;

    free(buf->buf);
    buf->buf = NULL;
    buf->len = 0;
    buf->size = 0;
}
