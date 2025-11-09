#ifndef RTC_H
#define RTC_H

#include <Arduino.h>
#include <RtcDS1302.h>

// RTC instance
extern ThreeWire myWire;
extern RtcDS1302<ThreeWire> Rtc;

// Function declarations
void setupAndSyncRTC();
void printDateTime(const RtcDateTime& dt);
RtcDateTime getCurrentTime();

#endif