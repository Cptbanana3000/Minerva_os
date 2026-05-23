#include <stdint.h>
#include "rtc.h"
#include "io.h"

#define CMOS_INDEX 0x70u
#define CMOS_DATA  0x71u
#define RTC_SECONDS 0x00u
#define RTC_MINUTES 0x02u
#define RTC_HOURS   0x04u
#define RTC_DAY     0x07u
#define RTC_MONTH   0x08u
#define RTC_YEAR    0x09u
#define RTC_STATUS_A 0x0Au
#define RTC_STATUS_B 0x0Bu

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, (uint8_t)(reg | 0x80u));
    return inb(CMOS_DATA);
}

static int rtc_update_in_progress(void) {
    return (cmos_read(RTC_STATUS_A) & 0x80u) != 0;
}

static uint8_t bcd_to_bin(uint8_t value) {
    return (uint8_t)((value & 0x0Fu) + ((value >> 4) * 10u));
}

static void rtc_read_raw(rtc_time_t *time) {
    time->second = cmos_read(RTC_SECONDS);
    time->minute = cmos_read(RTC_MINUTES);
    time->hour = cmos_read(RTC_HOURS);
    time->day = cmos_read(RTC_DAY);
    time->month = cmos_read(RTC_MONTH);
    time->year = cmos_read(RTC_YEAR);
}

int rtc_read_time(rtc_time_t *time) {
    if (!time) return 0;

    for (uint32_t wait = 0; wait < 1000000 && rtc_update_in_progress(); wait++) {}

    rtc_time_t a;
    rtc_time_t b;
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        rtc_read_raw(&a);
        rtc_read_raw(&b);
        if (a.second == b.second &&
            a.minute == b.minute &&
            a.hour == b.hour &&
            a.day == b.day &&
            a.month == b.month &&
            a.year == b.year) {
            break;
        }
    }

    uint8_t status_b = cmos_read(RTC_STATUS_B);
    if ((status_b & 0x04u) == 0) {
        a.second = bcd_to_bin(a.second);
        a.minute = bcd_to_bin(a.minute);
        a.hour = (uint8_t)((a.hour & 0x80u) | bcd_to_bin((uint8_t)(a.hour & 0x7Fu)));
        a.day = bcd_to_bin(a.day);
        a.month = bcd_to_bin(a.month);
        a.year = bcd_to_bin((uint8_t)a.year);
    }

    if ((status_b & 0x02u) == 0 && (a.hour & 0x80u)) {
        a.hour = (uint8_t)(((a.hour & 0x7Fu) + 12u) % 24u);
    }

    uint16_t year = (uint16_t)a.year;
    year = (uint16_t)(year >= 70 ? 1900u + year : 2000u + year);

    if (a.month < 1 || a.month > 12 ||
        a.day < 1 || a.day > 31 ||
        a.hour > 23 || a.minute > 59 || a.second > 59) {
        time->valid = 0;
        return 0;
    }

    time->year = year;
    time->month = a.month;
    time->day = a.day;
    time->hour = a.hour;
    time->minute = a.minute;
    time->second = a.second;
    time->yyyymmdd = (uint32_t)year * 10000u +
                     (uint32_t)a.month * 100u +
                     a.day;
    time->valid = 1;
    return 1;
}
