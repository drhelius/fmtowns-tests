#include <stddef.h>
#include <stdint.h>

// GCC may emit calls to these even in freestanding code

void* memset(void* destination, int value, size_t size)
{
    uint8_t* d = (uint8_t*)destination;

    while (size-- > 0)
        *d++ = (uint8_t)value;

    return destination;
}

void* memcpy(void* destination, const void* source, size_t size)
{
    uint8_t* d = (uint8_t*)destination;
    const uint8_t* s = (const uint8_t*)source;

    while (size-- > 0)
        *d++ = *s++;

    return destination;
}

void* memmove(void* destination, const void* source, size_t size)
{
    uint8_t* d = (uint8_t*)destination;
    const uint8_t* s = (const uint8_t*)source;

    if (d < s)
    {
        while (size-- > 0)
            *d++ = *s++;
    }
    else
    {
        while (size-- > 0)
            d[size] = s[size];
    }

    return destination;
}

int memcmp(const void* a, const void* b, size_t size)
{
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;

    for (size_t i = 0; i < size; i++)
    {
        if (x[i] != y[i])
            return x[i] - y[i];
    }

    return 0;
}
