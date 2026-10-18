#ifndef RUNTIME_H
#define RUNTIME_H

#include <stddef.h>

void* memset(void* destination, int value, size_t size);
void* memcpy(void* destination, const void* source, size_t size);
void* memmove(void* destination, const void* source, size_t size);
int memcmp(const void* a, const void* b, size_t size);

#endif /* RUNTIME_H */
