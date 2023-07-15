#ifndef LOCALUTIL_H
#define LOCALUTIL_H

#include <Arduino.h>

#define NaN_S32 (int32_t)0x80000000  // "Not a Number" representation for LONG (converted to 0 by SBFspot)
#define NaN_U32 (uint32_t)0xFFFFFFFF // "Not a Number" representation for ULONG (converted to 0 by SBFspot)


class LocalUtil {
    public:
    /// Convert a uint64_t (unsigned long long) to a string.
/// Arduino String/toInt/Serial.print() can't handle printing 64 bit values.
/// @param[in] input The value to print
/// @param[in] base The output base.
/// @returns A String representation of the integer.
/// @note Based on Arduino's Print::printNumber()
static String uint64ToString(uint64_t input, uint8_t base = 10);


static int32_t get_long(unsigned char *buf);



};

#endif
