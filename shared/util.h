#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

#define MAX_VALID_VALUES 8

typedef struct
{
    uint8_t count;
    uint32_t values[MAX_VALID_VALUES];
} expected_result_t;

typedef struct
{
    uint32_t minimum;
    uint32_t maximum;
} expected_range_t;

#define VA_ARGS_COUNT(...) VA_ARGS_COUNT_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1)
#define VA_ARGS_COUNT_IMPL(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define EXPECT(...) { VA_ARGS_COUNT(__VA_ARGS__), { __VA_ARGS__ } }
#define EXPECT_RANGE(minimum, maximum) { minimum, maximum }

static inline int is_valid_result(uint32_t actual, const expected_result_t* expected)
{
    for (int i = 0; i < expected->count; i++)
    {
        if (actual == expected->values[i])
            return 1;
    }

    return 0;
}

static inline int is_valid_range(uint32_t actual, const expected_range_t* expected)
{
    return actual >= expected->minimum && actual <= expected->maximum;
}

#endif /* UTIL_H */
