#include "checks.h"
#include "../disc4-input-storage/t48_ports.c"

// Microseconds for 4,096 accesses on the Marty 2, plus or minus 2%
static const char* const k_names[PORTS][2] =
{
    { "READ 0200H", "" }, { "READ 0202H", "WRITE 0202H" }, { "READ 0204H", "WRITE 0204H" },
    { "READ 0206H", "WRITE 0206H" }, { "READ 0208H", "WRITE 0208H" }, { "", "WRITE 020CH" }, { "READ 020DH", "" },
    { "READ 020EH", "" }, { "READ 3000H (CMOS)", "" }, { "READ 0600H", "" }, { "READ 0604H", "WRITE 0604H" },
    { "READ 04D2H", "" }, { "READ 04D6H", "WRITE 04D6H" }, { "READ 0C32H", "" }, { "READ 0034H", "" },
    { "READ 00A1H", "" }, { "READ 00ABH", "" }, { "READ 0480H", "" }, { "", "WRITE 0000H" }, { "", "WRITE 0002H" },
    { "", "WRITE 0010H" }, { "", "WRITE 0012H" }, { "", "WRITE 0440H" }, { "", "WRITE 0448H" },
    { "", "WRITE 3000H (CMOS)" }, { "", "WRITE 04D5H" }, { "", "WRITE 04EAH" }
};

static void evaluate_ports(const void* data, uint32_t steps)
{
    const ports_t* result = (const ports_t*)data;
    (void)steps;

    for (int i = 0; i < PORTS; i++)
    {
        uint16_t port = k_ports[i].port;

        if ((k_ports[i].access & READ) != 0)
        {
            if (port == 0x3000)
                check_range(k_names[i][0], result->read_us[i], 3075, 3205);
            else
                check_range(k_names[i][0], result->read_us[i], 3570, 3720);
        }

        if ((k_ports[i].access & WRITE) != 0)
        {
            if (port == 0x3000)
                check_range(k_names[i][1], result->write_us[i], 2095, 2180);
            else if (port == 0x0448)
                check_range(k_names[i][1], result->write_us[i], 3540, 3685);
            else
                check_range(k_names[i][1], result->write_us[i], 2530, 2640);
        }
    }
}

static const test_t k_test = CHECK_TEST(g_test_ports);

const check_group_t g_check_port_times = { "FDC, PAD, CMOS PORTS (MARTY)", &k_test, 1, check_marty,
    evaluate_ports };
