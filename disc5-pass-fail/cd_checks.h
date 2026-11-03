#ifndef CD_CHECKS_H
#define CD_CHECKS_H

// The packets disc 3's tests keep: the first two bytes of the packet at that index
static inline int keep_is(const cd_keep_t* keep, int index, uint8_t first, uint8_t second)
{
    return index < keep->count && index < CD_KEEP && keep->packets[index].bytes[0] == first &&
        keep->packets[index].bytes[1] == second;
}

// The index of the first packet with that first byte, or -1
static inline int keep_find(const cd_keep_t* keep, uint8_t first)
{
    for (int i = 0; i < keep->count && i < CD_KEEP; i++)
    {
        if (keep->packets[i].bytes[0] == first)
            return i;
    }

    return -1;
}

#endif /* CD_CHECKS_H */
