#include <stdint.h>
#include "runtime.h"

// GCC may emit calls to these even in freestanding code
// String instructions keep it from turning the loops back into calls to themselves

void* memset(void* destination, int value, size_t size)
{
    void* d = destination;
    __asm__ volatile ("cld\n\trep stosb" : "+D"(d), "+c"(size) : "a"(value) : "memory");
    return destination;
}

void* memcpy(void* destination, const void* source, size_t size)
{
    void* d = destination;
    __asm__ volatile ("cld\n\trep movsb" : "+D"(d), "+S"(source), "+c"(size) : : "memory");
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
