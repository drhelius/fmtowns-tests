#include "fmt.h"

static int put(char* output, int size, int position, char c);

int fmt_vformat(char* output, int size, const char* format, va_list arguments)
{
    static const char k_lower[] = "0123456789abcdef";
    static const char k_upper[] = "0123456789ABCDEF";
    int position = 0;

    while (*format != 0)
    {
        char c = *format++;

        if (c != '%')
        {
            position = put(output, size, position, c);
            continue;
        }

        int left = 0;
        char padding = ' ';
        int width = 0;

        for (;;)
        {
            if (*format == '-')
                left = 1;
            else if (*format == '0')
                padding = '0';
            else
                break;

            format++;
        }

        while (*format >= '0' && *format <= '9')
            width = width * 10 + (*format++ - '0');

        char spec = *format++;
        char digits[12];
        const char* text = digits;
        int length = 0;
        int negative = 0;

        switch (spec)
        {
            case 's':
                text = va_arg(arguments, const char*);

                if (text == 0)
                    text = "";

                length = fmt_length(text);
                break;
            case 'c':
                digits[0] = (char)va_arg(arguments, int);
                length = 1;
                break;
            case 'd':
            case 'u':
            case 'x':
            case 'X':
            {
                uint32_t value = va_arg(arguments, uint32_t);
                uint32_t base = (spec == 'x' || spec == 'X') ? 16 : 10;
                const char* table = spec == 'x' ? k_lower : k_upper;
                int i = 12;

                if (spec == 'd' && (int32_t)value < 0)
                {
                    negative = 1;
                    value = (uint32_t)(-(int32_t)value);
                }

                do
                {
                    digits[--i] = table[value % base];
                    value /= base;
                } while (value != 0);

                text = &digits[i];
                length = 12 - i;
                break;
            }
            case '%':
                digits[0] = '%';
                length = 1;
                break;
            default:
                continue;
        }

        int total = length + negative;

        if (negative && padding == '0')
            position = put(output, size, position, '-');

        if (!left)
        {
            for (int i = total; i < width; i++)
                position = put(output, size, position, padding);
        }

        if (negative && padding != '0')
            position = put(output, size, position, '-');

        for (int i = 0; i < length; i++)
            position = put(output, size, position, text[i]);

        if (left)
        {
            for (int i = total; i < width; i++)
                position = put(output, size, position, ' ');
        }
    }

    if (size > 0)
        output[position < size ? position : size - 1] = 0;

    return position;
}

int fmt_format(char* output, int size, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int length = fmt_vformat(output, size, format, arguments);
    va_end(arguments);
    return length;
}

int fmt_length(const char* text)
{
    int length = 0;

    while (text[length] != 0)
        length++;

    return length;
}

void fmt_copy(char* output, const char* text, int size)
{
    int i = 0;

    for (; i < size - 1 && text[i] != 0; i++)
        output[i] = text[i];

    if (size > 0)
        output[i] = 0;
}

static int put(char* output, int size, int position, char c)
{
    if (position < size - 1)
        output[position] = c;

    return position + 1;
}
