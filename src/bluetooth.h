#ifndef BLUETOOTH_H
#define BLUETOOTH_H


#define RED_LED 6
#define INVERTERSCAN_PIN 14 //Analogue pin 1 - next to VIN on connectors
#define BT_KEY 15           //Forces BT BOARD/CHIP into AT command mode
#define RxD 16
#define TxD 17
#define BLUETOOTH_POWER_PIN 5 //pin 5

//Location in EEPROM where the 2 arrays are written
#define ADDRESS_MY_BTADDRESS 0
#define ADDRESS_SMAINVERTER_BTADDRESS 10

// This is set via a build time constant in setup()
extern unsigned char smaBTInverterAddressArray[6] ;

extern unsigned char myBTAddress[6] ; // BT address of ESP32.


bool BTStart();
bool BTCheckConnected();
// Forcibly tear down the BT stack so the next BTStart() goes through
// SerialBT.begin() again. Used by the loop's stale-link watchdog.
void btReset();
// Wall-clock millis() of the last successfully-received BT byte. Updated by
// getByte() on success. Used by the loop watchdog to detect dead links.
extern volatile unsigned long gLastBTSuccessMs;

String getDeviceAddress(const uint8_t *point);
void updateMyDeviceAddress();
void sendPacket(unsigned char *btbuffer);
void writeArrayIntoEEPROM(unsigned char readbuffer[], int length, int EEPROMoffset);
bool readArrayFromEEPROM(unsigned char readbuffer[], int length, int EEPROMoffset);
unsigned char getByte();
extern volatile bool btTimedOut;
void convertBTADDRStringToArray(char *tempbuf, unsigned char *outarray, char match);
int hex2bin(const char *s);

#endif