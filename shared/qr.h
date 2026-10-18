#ifndef QR_H
#define QR_H

#include <stdint.h>

// QR codes in alphanumeric mode with error correction level M, versions 1-21, with the mask the caller picks

#define QR_MAX_VERSION 21
#define QR_MAX_SIZE (QR_MAX_VERSION * 4 + 17)
#define QR_QUIET_ZONE 4

typedef struct
{
    int version;
    int size;
    int mask;
    uint8_t modules[QR_MAX_SIZE][QR_MAX_SIZE];
} qr_code_t;

int qr_capacity(int version);
int qr_largest_version(int pixels, int scale);
int qr_encode(qr_code_t* qr, const char* text, int length, int max_version, int mask);

#endif /* QR_H */
