/*
 NANODE SMA PV MONITOR
 Bluetooth.ino 
 */

#include "Arduino.h"
#include <time.h>
#include "BluetoothSerial.h"
#include <EEPROM.h>
#include <esp_bt_device.h> // ESP32 BLE
#include <esp_bt.h>
#include <logging.hpp>
#include "Utils_SMA.h"
#include "site_details.h"
#include "SMANetArduino.h"

using namespace esp32m;

//SoftwareSerial blueToothSerial(RxD,TxD);

BluetoothSerial SerialBT;
// uint8_t address[6] = {0x00, 0x80, 0x25, 0x27, 0xE3, 0xC5}; // Address of my SMA inverter.

uint8_t address[6] = {SMA_ADDRESS};
// const char pinbuf[] = {'0', '0', '0', '0'}; // BT pin, not the inverter login password. Always 0000.
// const char *pin = &pinbuf[0];
// const char *pin = "0000";
bool connected;
static long charTime = 0;

#define STATE_FRESH 0
#define STATE_SETUP 1
#define STATE_CONNECTED 2
uint8_t btstate = STATE_FRESH;

unsigned char smaBTInverterAddressArray[6] = {};

unsigned char myBTAddress[6] = {}; // BT address of ESP32.
volatile bool btTimedOut = false;



// Setup the Bluetooth connection, returns true on success, false otherwise
// On fail, should be called again.
bool BTStart()
{
  log_d("BTStart(%i)", btstate);

  static unsigned long lastAttempt = 0;
  const unsigned long retryIntervalMs = 10000; // 10s between connect attempts

  if (btstate == STATE_FRESH)
  {
    // Free BLE memory when using Classic BT only; prevents coexist aborts and saves RAM
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    // Tiny guard before enabling controller, gives WiFi/coex a moment to settle
    delay(50);

    SerialBT.begin("ESP32test", true); // "true" creates this device as a BT Master.
    SerialBT.setPin("0000");           // pin as in "PIN" This is the BT connection pin, not login pin. ALWAYS 0000, unchangable.
    updateMyDeviceAddress();
    const uint8_t *addr = esp_bt_dev_get_address();
    log_d("My BT Address: %s  SMA BT Address (reversed): %s ", getDeviceAddress(addr).c_str(), getDeviceAddress(smaBTInverterAddressArray).c_str()) ;
    log_d("The SM32 started in master mode. Now trying to connect to SMA inverter.");
    lastAttempt = 0; // force immediate attempt below
    btstate = STATE_SETUP;
  }

  // Attempt connection if not connected
  if (!SerialBT.connected(0))
  {
    if (millis() - lastAttempt > retryIntervalMs)
    {
      log_i("Attempting BT connect...");
      SerialBT.connect(address);
      lastAttempt = millis();
    }
  }

  if (SerialBT.connected(1))
  {
    btstate = STATE_CONNECTED;
    log_w("Bluetooth connected succesfully!");
    return true;
  }
  else
  {
    btstate = STATE_SETUP;
    // transient failure, keep retrying
    return false;
  }
}

bool BTCheckConnected()
{
  return SerialBT.connected(1);
}

String getDeviceAddress(const uint8_t *point) {
  String daString("00:00:00:00:00:00");
  for (int i = 0; i < 6; i++)
  {
    char str[3];
    sprintf(str, "%02X", (int)point[i]);
    daString.concat(str);

    if (i < 5)
    {
      daString.concat(":");
    }
  }
  return daString;
}

void updateMyDeviceAddress()
{
  const uint8_t *point = esp_bt_dev_get_address();
  for (int i = 0; i < 6; i++)
  {
    // Save address in reverse because ¯\_(ツ)_/¯
    myBTAddress[i] = (char)point[5 - i];
  }
}

void sendPacket(unsigned char *btbuffer)
{
  SerialBT.write(btbuffer, packetposition);
  //quickblink();
  // Serial.println();
  // Serial.println("Sending: ");
  // for (int i = 0; i < packetposition; i++)
  // {
  //   // SerialBT.write(*(btbuffer + i)); // Send message char-by-char to SMA via ESP32 bluetooth
  //   // SerialBT.write(btbuffer[i]);
  //   Serial.print(btbuffer[i], HEX); // Print out what we are sending, in hex, for inspection.
  //   Serial.print(' ');
  // }
  // Serial.println();
}

/* by DRH. Not ready to blink LEDs yet.
void quickblink() {
  digitalWrite( RED_LED, LOW);
  delay(30);
  digitalWrite( RED_LED, HIGH);
}
*/

void writeArrayIntoEEPROM(unsigned char readbuffer[], int length, int EEPROMoffset)
{
  //Writes an array into EEPROM and calculates a simple XOR checksum
  byte checksum = 0;
  for (int i = 0; i < length; i++)
  {
    EEPROM.write(EEPROMoffset + i, readbuffer[i]);
    //Serial.print(EEPROMoffset+i); Serial.print("="); Serial.println(readbuffer[i],HEX);
    checksum ^= readbuffer[i];
  }

  //Serial.print(EEPROMoffset+length); Serial.print("="); Serial.println(checksum,HEX);
  EEPROM.write(EEPROMoffset + length, checksum);
}

bool readArrayFromEEPROM(unsigned char readbuffer[], int length, int EEPROMoffset)
{
  //Writes an array into EEPROM and calculates a simple XOR checksum
  byte checksum = 0;
  for (int i = 0; i < length; i++)
  {
    readbuffer[i] = EEPROM.read(EEPROMoffset + i);
    //Serial.print(EEPROMoffset+i); Serial.print("="); Serial.println(readbuffer[i],HEX);
    checksum ^= readbuffer[i];
  }
  //Serial.print(EEPROMoffset+length); Serial.print("="); Serial.println(checksum,HEX);
  return (checksum == EEPROM.read(EEPROMoffset + length));
}

unsigned char getByte()
{
  // Returns a single byte from the bluetooth stream (with error timeout)
  // Do NOT restart the ESP on timeout; return 0xFF as a sentinel so callers can abort gracefully.
  const unsigned long maxWaitMs = 5000; // 5s per byte should be plenty; keeps main loop responsive
  const unsigned long start = millis();
  int inInt = -1; // ESP32 SerialBT.read() returns an int

  while (!SerialBT.available())
  {
    delay(2); // small wait for BT byte to arrive
    if ((millis() - start) > maxWaitMs)
    {
      log_w("BT getByte timeout");
      btTimedOut = true;
      return 0; // value ignored by caller when btTimedOut is set
    }
  }

  inInt = SerialBT.read();
  charTime = millis();

  if (inInt == -1)
  {
    log_w("BT read returned -1");
    btTimedOut = true;
    return 0;
  }
  btTimedOut = false;
  return (unsigned char)inInt;
}

void convertBTADDRStringToArray(char *tempbuf, unsigned char *outarray, char match)
{
  //Convert BT Address into a more useful byte array, the BT address is in a really awful format to parse!
  //Must be a better way of doing this function!

  int l = strlen(tempbuf);
  char *firstcolon = strchr(tempbuf, match) + 1;
  char *lastcolon = strrchr(tempbuf, match) + 1;

  //Could use a shared buffer here to save RAM
  char output[13] = {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', 0};

  //Deliberately avoided sprintf as it adds over 1600bytes to the program size at compile time.
  int partlen = (firstcolon - tempbuf) - 1;
  strncpy(output + (4 - partlen), tempbuf, partlen);

  partlen = (lastcolon - firstcolon) - 1;
  strncpy(output + 4 + (2 - partlen), firstcolon, partlen);

  partlen = l - (lastcolon - tempbuf);
  strncpy(output + 6 + (6 - partlen), lastcolon, partlen);

  //Finally convert the string (11AABB44FF66) into a real byte array
  //written backwards in the same format that the SMA packets expect it in
  int i2 = 5;
  for (int i = 0; i < 12; i += 2)
  {
    outarray[i2--] = hex2bin(&output[i]);
  }

  /*
Serial.print("BT StringToArray=");  
   for(int i=0; i<6; i+=1)    debugPrintHexByte(outarray[i]);   
   Serial.println("");
   */
}

int hex2bin(const char *s)
{
  int ret = 0;
  int i;
  for (i = 0; i < 2; i++)
  {
    char c = *s++;
    int n = 0;
    if ('0' <= c && c <= '9')
      n = c - '0';
    else if ('a' <= c && c <= 'f')
      n = 10 + c - 'a';
    else if ('A' <= c && c <= 'F')
      n = 10 + c - 'A';
    ret = n + ret * 16;
  }
  return ret;
}
