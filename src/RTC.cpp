#include "RTC.h"

// RTC instance
ThreeWire myWire(26, 25, 27); // IO, SCLK, CE
RtcDS1302<ThreeWire> Rtc(myWire);

void setupRTC() {
    Rtc.Begin();
    Rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
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