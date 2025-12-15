/*
 * DS1302 RTC Driver - Baremetal Implementation
 * Combined Header and Implementation
 * For ESP32 Attendance System
 * 
 * Pin Configuration:
 * - IO (Data): Bidirectional data line
 * - SCLK (Clock): Serial clock
 * - CE (Chip Enable): Active high chip select
 */

#ifndef DS1302_RTC_H
#define DS1302_RTC_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

// DS1302 Register Addresses
#define DS1302_REG_SECONDS      0x80
#define DS1302_REG_MINUTES      0x82
#define DS1302_REG_HOURS        0x84
#define DS1302_REG_DATE         0x86
#define DS1302_REG_MONTH        0x88
#define DS1302_REG_DAY          0x8A
#define DS1302_REG_YEAR         0x8C
#define DS1302_REG_WP           0x8E  // Write Protect
#define DS1302_REG_BURST        0xBE  // Burst mode (read/write all)

// Read/Write flag
#define DS1302_READ_FLAG        0x01

// Clock Halt bit (in seconds register)
#define DS1302_CH_BIT           0x80

// Write Protect bit
#define DS1302_WP_BIT           0x80

/**
 * Simple DateTime structure
 */
struct DateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    
    // Constructor
    DateTime(uint16_t y = 2000, uint8_t mon = 1, uint8_t d = 1, 
             uint8_t h = 0, uint8_t min = 0, uint8_t s = 0)
        : year(y), month(mon), day(d), hour(h), minute(min), second(s) {}
};

/**
 * DS1302 RTC Driver Class
 */
class DS1302_RTC {
public:
    /**
     * Constructor
     * @param ioPin - Data pin (bidirectional)
     * @param sclkPin - Serial clock pin
     * @param cePin - Chip enable pin
     */
    DS1302_RTC(uint8_t ioPin, uint8_t sclkPin, uint8_t cePin);
    
    /**
     * Initialize the RTC module
     * Must be called in setup()
     */
    void begin();
    
    /**
     * Set the current date and time
     * @param dt - DateTime structure with the time to set
     */
    void setDateTime(const DateTime& dt);
    
    /**
     * Get the current date and time
     * @return DateTime structure with current time
     */
    DateTime getDateTime();
    
    /**
     * Check if the RTC is running
     * @return true if running, false if halted
     */
    bool isRunning();
    
    /**
     * Start or stop the RTC clock
     * @param running - true to start, false to halt
     */
    void setRunning(bool running);
    
private:
    uint8_t _ioPin;
    uint8_t _sclkPin;
    uint8_t _cePin;
    
    /**
     * Start a transmission to the DS1302
     * @param address - Register address with R/W flag
     */
    void beginTransmission(uint8_t address);
    
    /**
     * End transmission
     */
    void endTransmission();
    
    /**
     * Write a byte to the DS1302
     * @param data - Byte to write
     * @param isRead - true if this is preparing for a read operation
     */
    void writeByte(uint8_t data, bool isRead = false);
    
    /**
     * Read a byte from the DS1302
     * @return Byte read from the device
     */
    uint8_t readByte();
    
    /**
     * Read a single register
     * @param address - Register address (without read flag)
     * @return Register value
     */
    uint8_t readRegister(uint8_t address);
    
    /**
     * Write a single register
     * @param address - Register address
     * @param data - Data to write
     */
    void writeRegister(uint8_t address, uint8_t data);
    
    /**
     * Enable or disable write protection
     * @param enable - true to enable write protection
     */
    void setWriteProtect(bool enable);
    
    /**
     * Convert decimal to BCD (Binary Coded Decimal)
     * @param val - Decimal value (0-99)
     * @return BCD encoded value
     */
    uint8_t decToBcd(uint8_t val);
    
    /**
     * Convert BCD to decimal
     * @param val - BCD encoded value
     * @return Decimal value
     */
    uint8_t bcdToDec(uint8_t val);
};

// =============================================================================
// NTP SYNC AND UTILITY FUNCTIONS
// =============================================================================

// NTP settings
extern const char* ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;

// Global RTC instance
extern DS1302_RTC rtc;

/**
 * Initialize and sync RTC with NTP server
 */
void setupAndSyncRTC();

/**
 * Print DateTime in human-readable format
 */
void printDateTime(const DateTime& dt);

/**
 * Get current time from RTC
 * @return DateTime object with current time
 */
DateTime getCurrentTime();

#endif // DS1302_RTC_H