#include "RTC.h"

// NTP settings
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600; // GMT+8 Philippines
const int daylightOffset_sec = 0;

// RTC instance
ThreeWire myWire(26, 25, 27); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

void setupAndSyncRTC() {
    Rtc.Begin();
    // Rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
    // printDateTime(Rtc.GetDateTime());
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) { // wait max 10 sec
        RtcDateTime ntpTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        );
        Rtc.Begin();
        Rtc.SetDateTime(ntpTime);
        Serial.printf("✅ RTC synced: %04u-%02u-%02u %02u:%02u:%02u\n",
                        ntpTime.Year(), ntpTime.Month(), ntpTime.Day(),
                        ntpTime.Hour(), ntpTime.Minute(), ntpTime.Second());
    } else {
        Serial.println("⚠️ Failed to get time from NTP");
    }
}

void printDateTime(const RtcDateTime& dt) {
    char buf[26];
    snprintf(buf, sizeof(buf), "%02u/%02u/%04u %02u:%02u:%02u",
            dt.Month(), dt.Day(), dt.Year(),
            dt.Hour(), dt.Minute(), dt.Second());
    Serial.print(buf);
}

RtcDateTime getCurrentTime() {
    return Rtc.GetDateTime();
}