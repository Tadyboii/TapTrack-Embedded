/*
 * DS1302 RTC Driver - Baremetal Implementation
 * Combined Implementation
 */

#include "DS1302_RTC.h"

// =============================================================================
// NTP CONFIGURATION
// =============================================================================

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600; // GMT+8 Philippines
const int daylightOffset_sec = 0;

// Global RTC instance definition
DS1302_RTC rtc(26, 25, 27); // IO=26, SCLK=25, CE=27

// =============================================================================
// DS1302_RTC CLASS IMPLEMENTATION
// =============================================================================

/**
 * Constructor
 */
DS1302_RTC::DS1302_RTC(uint8_t ioPin, uint8_t sclkPin, uint8_t cePin)
    : _ioPin(ioPin), _sclkPin(sclkPin), _cePin(cePin) {
}

/**
 * Initialize the RTC
 */
void DS1302_RTC::begin() {
    // Set pins to default state (low, input)
    pinMode(_cePin, INPUT);
    pinMode(_sclkPin, INPUT);
    pinMode(_ioPin, INPUT);
    
    // Disable write protection to allow configuration
    setWriteProtect(false);
    
    // Make sure clock is running
    if (!isRunning()) {
        setRunning(true);
    }
}

/**
 * Set date and time
 */
void DS1302_RTC::setDateTime(const DateTime& dt) {
    // Disable write protection
    setWriteProtect(false);
    
    // Use burst mode to write all registers at once
    beginTransmission(DS1302_REG_BURST);
    
    // Write seconds (with CH bit cleared to ensure clock runs)
    writeByte(decToBcd(dt.second) & 0x7F);
    
    // Write minutes
    writeByte(decToBcd(dt.minute));
    
    // Write hours (24-hour mode)
    writeByte(decToBcd(dt.hour));
    
    // Write date
    writeByte(decToBcd(dt.day));
    
    // Write month
    writeByte(decToBcd(dt.month));
    
    // Write day of week (we'll calculate it: 1=Monday, 7=Sunday)
    // For simplicity, we'll just use 1
    writeByte(1);
    
    // Write year (only last 2 digits: 2024 -> 24)
    writeByte(decToBcd(dt.year - 2000));
    
    // Write protect byte (disabled)
    writeByte(0x00);
    
    endTransmission();
}

/**
 * Get current date and time
 */
DateTime DS1302_RTC::getDateTime() {
    DateTime dt;
    
    // Use burst mode to read all registers at once
    beginTransmission(DS1302_REG_BURST | DS1302_READ_FLAG);
    
    // Read seconds (mask out CH bit)
    dt.second = bcdToDec(readByte() & 0x7F);
    
    // Read minutes
    dt.minute = bcdToDec(readByte());
    
    // Read hours (mask to handle 24-hour format)
    dt.hour = bcdToDec(readByte() & 0x3F);
    
    // Read date
    dt.day = bcdToDec(readByte());
    
    // Read month
    dt.month = bcdToDec(readByte());
    
    // Read day of week (we'll ignore this)
    readByte();
    
    // Read year
    dt.year = bcdToDec(readByte()) + 2000;
    
    // Read write protect (ignore)
    readByte();
    
    endTransmission();
    
    return dt;
}

/**
 * Check if RTC is running
 */
bool DS1302_RTC::isRunning() {
    uint8_t seconds = readRegister(DS1302_REG_SECONDS);
    return !(seconds & DS1302_CH_BIT);
}

/**
 * Start or halt the RTC
 */
void DS1302_RTC::setRunning(bool running) {
    uint8_t seconds = readRegister(DS1302_REG_SECONDS);
    
    if (running) {
        seconds &= ~DS1302_CH_BIT;  // Clear CH bit to start
    } else {
        seconds |= DS1302_CH_BIT;   // Set CH bit to halt
    }
    
    writeRegister(DS1302_REG_SECONDS, seconds);
}

/**
 * Begin transmission
 */
void DS1302_RTC::beginTransmission(uint8_t address) {
    // Set CE low first
    digitalWrite(_cePin, LOW);
    pinMode(_cePin, OUTPUT);
    
    // Set SCLK low
    digitalWrite(_sclkPin, LOW);
    pinMode(_sclkPin, OUTPUT);
    
    // Set IO as output
    pinMode(_ioPin, OUTPUT);
    
    // Enable the chip (CE high)
    digitalWrite(_cePin, HIGH);
    delayMicroseconds(4);  // tCC = 4us
    
    // Send the address/command byte
    writeByte(address, (address & DS1302_READ_FLAG) == DS1302_READ_FLAG);
}

/**
 * End transmission
 */
void DS1302_RTC::endTransmission() {
    // Disable the chip (CE low)
    digitalWrite(_cePin, LOW);
    delayMicroseconds(4);  // tCWH = 4us
    
    // Reset pins to input (low power state)
    pinMode(_cePin, INPUT);
    pinMode(_sclkPin, INPUT);
    pinMode(_ioPin, INPUT);
}

/**
 * Write a byte to DS1302
 */
void DS1302_RTC::writeByte(uint8_t data, bool isRead) {
    for (uint8_t i = 0; i < 8; i++) {
        // Set data bit
        digitalWrite(_ioPin, data & 0x01);
        delayMicroseconds(1);  // tDC = 200ns
        
        // Clock high (DS1302 reads on rising edge)
        digitalWrite(_sclkPin, HIGH);
        delayMicroseconds(1);  // tCH = 1000ns
        
        // If this is the last bit before a read, switch IO to input
        if (i == 7 && isRead) {
            pinMode(_ioPin, INPUT);
        }
        
        // Clock low
        digitalWrite(_sclkPin, LOW);
        delayMicroseconds(1);  // tCL = 1000ns
        
        // Shift to next bit
        data >>= 1;
    }
}

/**
 * Read a byte from DS1302
 */
uint8_t DS1302_RTC::readByte() {
    uint8_t data = 0;
    
    for (uint8_t i = 0; i < 8; i++) {
        // Read bit (LSB first)
        data |= (digitalRead(_ioPin) << i);
        
        // Clock high
        digitalWrite(_sclkPin, HIGH);
        delayMicroseconds(1);
        
        // Clock low (data is ready after this)
        digitalWrite(_sclkPin, LOW);
        delayMicroseconds(1);  // tCL = 1000ns, tCDD = 800ns
    }
    
    return data;
}

/**
 * Read a single register
 */
uint8_t DS1302_RTC::readRegister(uint8_t address) {
    beginTransmission(address | DS1302_READ_FLAG);
    uint8_t data = readByte();
    endTransmission();
    return data;
}

/**
 * Write a single register
 */
void DS1302_RTC::writeRegister(uint8_t address, uint8_t data) {
    setWriteProtect(false);  // Ensure write protection is disabled
    beginTransmission(address);
    writeByte(data);
    endTransmission();
}

/**
 * Enable/disable write protection
 */
void DS1302_RTC::setWriteProtect(bool enable) {
    beginTransmission(DS1302_REG_WP);
    writeByte(enable ? DS1302_WP_BIT : 0x00);
    endTransmission();
}

/**
 * Convert decimal to BCD
 */
uint8_t DS1302_RTC::decToBcd(uint8_t val) {
    return ((val / 10) << 4) | (val % 10);
}

/**
 * Convert BCD to decimal
 */
uint8_t DS1302_RTC::bcdToDec(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

// =============================================================================
// NTP SYNC AND UTILITY FUNCTIONS
// =============================================================================

/**
 * Initialize and sync RTC with NTP server
 */
void setupAndSyncRTC() {
    rtc.begin();
    
    // Configure NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) { // wait max 10 sec
        DateTime ntpTime(
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec
        );
        
        rtc.setDateTime(ntpTime);
        Serial.printf("✅ RTC synced: %04u-%02u-%02u %02u:%02u:%02u\n",
                        ntpTime.year, ntpTime.month, ntpTime.day,
                        ntpTime.hour, ntpTime.minute, ntpTime.second);
    } else {
        Serial.println("⚠️ Failed to get time from NTP");
    }
}

/**
 * Print DateTime in human-readable format
 */
void printDateTime(const DateTime& dt) {
    char buf[26];
    snprintf(buf, sizeof(buf), "%02u/%02u/%04u %02u:%02u:%02u",
            dt.month, dt.day, dt.year,
            dt.hour, dt.minute, dt.second);
    Serial.print(buf);
}

/**
 * Get current time from RTC
 */
DateTime getCurrentTime() {
    return rtc.getDateTime();
}