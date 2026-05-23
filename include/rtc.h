#ifndef MINERVA_RTC_H
#define MINERVA_RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t yyyymmdd;
    uint8_t valid;
} rtc_time_t;

int rtc_read_time(rtc_time_t *time);

#endif
