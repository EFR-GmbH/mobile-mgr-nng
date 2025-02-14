#ifndef MBIM_NNG_DATABUF_H
#define MBIM_NNG_DATABUF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

enum data_type // 2 Bytes
{
    DT_RAW = 1,
    DT_UINT,
    DT_STRING
};

typedef struct databuf
{
    unsigned char *buf;
    size_t size;
    size_t len;
} Databuf;

bool databuf_init(Databuf *buf);
void databuf_free(Databuf *buf);
void databuf_set_buf(Databuf *buf, unsigned char *buffer, size_t size);
bool databuf_is_valid(Databuf *buf);

void databuf_add_string(Databuf *buf, unsigned int var, const char *value);
void databuf_add_uint(Databuf *buf, unsigned int var, unsigned int value);

char *databuf_get_next_string(Databuf *buf, unsigned int var, unsigned char *prev);
char *databuf_get_string(Databuf *buf, unsigned int var);
char *databuf_get_next_uint(Databuf *buf, unsigned int var, unsigned int *value, unsigned char *prev);
char *databuf_get_uint(Databuf *buf, unsigned int var, unsigned int *value);

#ifdef __cplusplus
}
#endif

#endif // MBIM_NNG_DATABUF_H
