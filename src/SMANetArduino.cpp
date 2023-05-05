/*
 NANODE SMA PV MONITOR
 SMANetArduino.ino
 */
#include "Arduino.h"
#include <logging.hpp>
#include "SMANetArduino.h"
#include "bluetooth.h"
#include "Utils_SMA.h"
using namespace esp32m;

unsigned int packetlength = 0;
unsigned int cmdcode = 0;

//Should be moved into PROGMEM
unsigned char sixzeros[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
//Should be moved into PROGMEM
unsigned char sixff[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

unsigned int packetposition = 0;

unsigned int FCSChecksum = 0xffff;

unsigned char Level1SrcAdd[6];
unsigned char Level1DestAdd[6];

//char invertername[15]={0};
unsigned char packet_send_counter = 0;
unsigned char lastpacketindex = 0;

unsigned char level1packet[maxlevel1packetsize];

bool escapenextbyte = false;

PROGMEM prog_uint16_t fcstab[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf, 0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7, 0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e, 0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd, 0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5, 0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c, 0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb, 0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3, 0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a, 0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9, 0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1, 0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738, 0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7, 0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff, 0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036, 0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5, 0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd, 0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134, 0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3, 0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb, 0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232, 0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1, 0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9, 0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330, 0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78};


prog_uchar PROGMEM SMANET2header[] = {0xFF, 0x03, 0x60, 0x65};
size_t sizeof_SMANET2header = sizeof(SMANET2header);

prog_uchar PROGMEM InverterCodeArray[] = {0x5c, 0xaf, 0xf0, 0x1d, 0x50, 0x00}; // Fake address on the SMA NETWORK
size_t sizeof_InverterCodeArray = sizeof(InverterCodeArray);

prog_uchar PROGMEM fourzeros[] = {0, 0, 0, 0};
size_t sizeof_fourzeros = sizeof(fourzeros);




unsigned int readLevel1PacketFromBluetoothStream(int index)
{
  unsigned int retcmdcode;
  bool errorCodePCS;
  bool errorCodeVSA;
  bool errorCodePIFM;
  //HowMuchMemory();

  //Serial.print(F("#Read cmd="));
  //Serial.print("READ=");  Serial.println(index,HEX);
  retcmdcode = 0;
  errorCodePCS = false;
  errorCodeVSA = false;
  errorCodePIFM = false;
  //level1packet={0};

  //Wait for a start packet byte
  while (getByte() != '\x7e')
  {
    delay(1);
  }

  byte len1 = getByte();
  byte len2 = getByte();
  byte packetchecksum = getByte();

  if ((0x7e ^ len1 ^ len2) == packetchecksum)
    errorCodePCS = false;
  else
  {
    log_i("*** Invalid header chksum.");
    errorCodePCS = true;
  }

  for (int i = 0; i < 6; i++)
    Level1SrcAdd[i] = getByte();
  for (int i = 0; i < 6; i++)
    Level1DestAdd[i] = getByte();

  retcmdcode = getByte() + (getByte() * 256);
  packetlength = len1 + (len2 * 256);
  //Serial.println(" ");
  //Serial.print("packetlength = ");
  //Serial.println(packetlength);

  if (ValidateSenderAddress())
  {
    //Serial.println("*** Message is from SMA.");
    errorCodeVSA = false;
  }
  else
  {
    log_w("P wrng dest");
    errorCodeVSA = true;
  }
  if (IsPacketForMe())
  {
    //Serial.println("*** Message is to ESP32.");
    errorCodePIFM = false;
  }
  else
  {
    log_w("P wrng snder");
    errorCodePIFM = true;
  }

  //If retcmdcode==8 then this is part of a multi packet response which we need to buffer up
  //Read the whole packet into buffer
  //=0;  //Seperate index as we exclude bytes as we move through the buffer

  //Serial.println(" ");
  //Serial.print( "packetlength-level1headerlength = " );
  //Serial.println( packetlength-level1headerlength );
  for (int i = 0; i < (packetlength - level1headerlength); i++)
  {
    //Serial.print( " index = " );
    //Serial.print( index );
    //Serial.print( " " );
    level1packet[index] = getByte();

    //Keep 1st byte raw unescaped 0x7e
    //if (i>0) {
    if (escapenextbyte == true)
    {
      level1packet[index] = level1packet[index] ^ 0x20;
      escapenextbyte = false;
      index++;
    }
    else
    {
      if ((level1packet[index] == 0x7d))
      {
        //Throw away the 0x7d byte
        escapenextbyte = true;
      }
      else
        index++;
    }
  } // End of for loop that reads the ENTIRE packet into level1packet[]

  //Reset packetlength based on true length of packet
  packetlength = index + level1headerlength; // Length of the entire packet received. Which is 18 greater
  //                                               than the amount of data actually added to level1packet[]
  packetposition = index; // Index into level1packet[] of the first byte after the Level 1 header stuff.
  //                        The byte at level1packet[index] will be 0x7E for a level 2 packet. NOT 0x7E for a level 1 packet.

  if (errorCodePCS || errorCodeVSA || errorCodePIFM)
  {
    //Serial.println(" ");
    //Serial.println("readLevel1PacketFromBluetoothStream now returning with return code = 0xFFFF.");
    return 0xFFFF;
  }
  else
  {
    //Serial.println(" ");
    //Serial.print("readLevel1PacketFromBluetoothStream now returning with return code = ");
    //Serial.println(retcmdcode);
    return retcmdcode;
  }
}
//-----------------------------------------------------------
void prepareToReceive()
{
  FCSChecksum = 0xFFFF;
  packetposition = 0;
  escapenextbyte = false;
}
//------------------------------------------------------------
bool containsLevel2Packet()
{
  //Serial.println(" ");
  //Serial.print("packetlength = ");
  //Serial.println(packetlength);
  //Serial.print("level1headerlength = ");
  //Serial.println(level1headerlength);
  if ((packetlength - level1headerlength) < 5)
    return false;

  return (level1packet[0] == 0x7e &&
          level1packet[1] == 0xff &&
          level1packet[2] == 0x03 &&
          level1packet[3] == 0x60 &&
          level1packet[4] == 0x65);
}
//---------------------------------------------------------------
// This function will read BT packets from the inverter until it finds one that is addressed to me, and
//   has the same command code as the one I am waiting for = cmdcodetowait.
// It is called when you know a Level1 message is coming.
// NOTE: NEVER call this function with cmdcodetowait = 0xFFFF. That value is returned
//   if the packet has a checksum error, or it's not addressed to me, etc...
// This function just discards the packet if there is an error, returns true on success
// false otherwise, at which point it should be called again

unsigned int lastGetPacket = -1;
bool getPacket(unsigned int cmdcodetowait)
{
  if (lastGetPacket != cmdcodetowait)
  {
    prepareToReceive();
    lastGetPacket = cmdcodetowait;
  }
  cmdcode = readLevel1PacketFromBluetoothStream(0);
  // debugMsg("Command code: ");
  // debugMsgLn(String(cmdcode));
  if (cmdcode == cmdcodetowait)
  {
    return true;
    lastGetPacket = -1;
  }
  return false;
}

void waitForPacket(unsigned int xxx)
{
  // pass
}

//------------------------------------------------------------------
// This code will also look for the proper command code.
// The difference to the function above is that it will search deeply through the level1packet[] array.
// Level 2 messages (PPP messages) follow the Level 1 packet header part. Each Level 2 packet starts with
//   7E FF 03 60 65.
unsigned int lastMulticmdcode = -1;
bool waitForMultiPacket(unsigned int cmdcodetowait)
{

  //prepareToReceive();
  //Serial.println(" ");
  prepareToReceive();
  //Serial.println("*** In waitForMultiPacket. About to call readLevel1PacketFromBluetoothStream(0) ***");
  //cmdcode = readLevel1PacketFromBluetoothStream(packetposition);
  lastMulticmdcode = readLevel1PacketFromBluetoothStream(0);
  //Serial.println(" ");
  //Serial.print("*** Done reading packet. cmdcode = ");
  //Serial.println(cmdcode);
  if (lastMulticmdcode == cmdcodetowait)
    return true;

  //Serial.println("Now returning from 'waitForMultiPacket'.");
  return false;
}

void writeSMANET2Long(unsigned char *btbuffer, unsigned long v)
{
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 0) & 0XFF));
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 8) & 0XFF));
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 16) & 0xFF));
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 24) & 0xFF));
}

void writeSMANET2uint(unsigned char *btbuffer, unsigned int v)
{
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 0) & 0XFF));
  writeSMANET2SingleByte(btbuffer, (unsigned char)((v >> 8) & 0XFF));
  //writeSMANET2SingleByte(btbuffer,(unsigned char)((v >> 16) & 0xFF)) ;
  //writeSMANET2SingleByte(btbuffer,(unsigned char)((v >> 24) & 0xFF)) ;
}

/*
void displaySpotValues(int gap) {
  unsigned long value = 0;
  unsigned int valuetype=0;
  for(int i=40+1;i<packetposition-3;i+=gap){
    valuetype = level1packet[i+1]+level1packet[i+2]*256;
    memcpy(&value,&level1packet[i+8],3);
    //valuetype 
    //0x2601=Total Yield Wh
    //0x2622=Day Yield Wh
    //0x462f=Feed in time (hours) /60*60
    //0x462e=Operating time (hours) /60*60
    //0x451f=DC Voltage  /100
    //0x4521=DC Current  /1000
    Serial.print(valuetype,HEX);
    Serial.print("=");
    memcpy(&datetime,&level1packet[i+4],4);
    digitalClockDisplay(datetime);
    Serial.print("=");Serial.println(value);
  }
}
*/

void writeSMANET2SingleByte(unsigned char *btbuffer, unsigned char v)
{
  //Keep a rolling checksum over the payload
  FCSChecksum = (FCSChecksum >> 8) ^ pgm_read_word_near(&fcstab[(FCSChecksum ^ v) & 0xff]);

  if (v == 0x7d || v == 0x7e || v == 0x11 || v == 0x12 || v == 0x13)
  {
    btbuffer[packetposition++] = 0x7d;
    btbuffer[packetposition++] = v ^ 0x20;
  }
  else
  {
    btbuffer[packetposition++] = v;
  }
}

void writeSMANET2Array(unsigned char *btbuffer, unsigned char bytes[], byte loopcount)
{
  //Escape the packet if needed....
  for (int i = 0; i < loopcount; i++)
  {
    writeSMANET2SingleByte(btbuffer, bytes[i]);
  } //end for
}

void writeSMANET2ArrayFromProgmem(unsigned char *btbuffer, prog_uchar ProgMemArray[], byte loopcount)
{
  //debugMsg("writeSMANET2ArrayFromProgmem=");
  //Serial.println(loopcount);
  //Escape the packet if needed....
  for (int i = 0; i < loopcount; i++)
  {
    writeSMANET2SingleByte(btbuffer, pgm_read_byte_near(ProgMemArray + i));
  } //end for
}

void writeSMANET2PlusPacket(unsigned char *btbuffer, unsigned char ctrl1, unsigned char ctrl2, unsigned char packetcount, unsigned char a, unsigned char b, unsigned char c)
{

  //This is the header for a SMANET2+ (no idea if this is the correct name!!) packet - 28 bytes in length
  lastpacketindex = packetcount;

  //Start packet
  btbuffer[packetposition++] = 0x7e; //Not included in checksum

  //Header bytes 0xFF036065
  writeSMANET2ArrayFromProgmem(btbuffer, SMANET2header, 4);

  writeSMANET2SingleByte(btbuffer, ctrl1);
  writeSMANET2SingleByte(btbuffer, ctrl2);

  writeSMANET2Array(btbuffer, sixff, 6);

  writeSMANET2SingleByte(btbuffer, a); //ArchCd
  writeSMANET2SingleByte(btbuffer, b); //Zero

  writeSMANET2ArrayFromProgmem(btbuffer, InverterCodeArray, 6);

  writeSMANET2SingleByte(btbuffer, 0x00);
  writeSMANET2SingleByte(btbuffer, c);

  writeSMANET2ArrayFromProgmem(btbuffer, fourzeros, 4);

  writeSMANET2SingleByte(btbuffer, packetcount);
}

void writeSMANET2PlusPacketTrailer(unsigned char *btbuffer)
{
  FCSChecksum = FCSChecksum ^ 0xffff;

  btbuffer[packetposition++] = FCSChecksum & 0x00ff;
  btbuffer[packetposition++] = (FCSChecksum >> 8) & 0x00ff;
  btbuffer[packetposition++] = 0x7e; //Trailing byte

  //Serial.print("Send Checksum=");Serial.println(FCSChecksum,HEX);
}

void writePacketHeader(unsigned char *btbuffer)
{
  writePacketHeader(btbuffer, 0x01, 0x00, smaBTInverterAddressArray);
}
void writePacketHeader(unsigned char *btbuffer, unsigned char *destaddress)
{
  writePacketHeader(btbuffer, 0x01, 0x00, destaddress);
}

void writePacketHeader(unsigned char *btbuffer, unsigned char cmd1, unsigned char cmd2, unsigned char *destaddress)
{

  if (packet_send_counter > 75)
    packet_send_counter = 1;

  FCSChecksum = 0xffff;

  packetposition = 0;

  btbuffer[packetposition++] = 0x7e;
  btbuffer[packetposition++] = 0; //len1
  btbuffer[packetposition++] = 0; //len2
  btbuffer[packetposition++] = 0; //checksum

  for (int i = 0; i < 6; i++)
  {
    btbuffer[packetposition++] = myBTAddress[i];
  }

  for (int i = 0; i < 6; i++)
  {
    btbuffer[packetposition++] = destaddress[i];
  }

  btbuffer[packetposition++] = cmd1; //cmd byte 1
  btbuffer[packetposition++] = cmd2; //cmd byte 2

  //cmdcode=cmd1+(cmd2*256);
  cmdcode = 0xffff; //Just set to dummy value

  //packetposition should now = 18
}

void writePacketLength(unsigned char *btbuffer)
{
  //  unsigned char len1=;  //TODO: Need to populate both bytes for large packets over 256 bytes...
  //  unsigned char len2=0;  //Were only doing a single byte for now...
  btbuffer[1] = packetposition;
  btbuffer[2] = 0;
  btbuffer[3] = btbuffer[0] ^ btbuffer[1] ^ btbuffer[2]; //checksum
}

void wrongPacket()
{
  Serial.println(F("*WRONG PACKET*"));
  error();
}

void dumpPacket(char letter)
{
  for (int i = 0; i < packetposition; i++)
  {

    if (i % 16 == 0)
    {
      Serial.println("");
      Serial.print(letter);
      Serial.print(i, HEX);
      Serial.print(":");
    }

    debugPrintHexByte(level1packet[i]);
  }
  Serial.println("");
}

void debugPrintHexByte(unsigned char b)
{
  if (b < 16)
    Serial.print("0"); //leading zero if needed
  Serial.print(b, HEX);
  Serial.print(" ");
}
//-------------------------------------------------------------------------
bool validateChecksum()
{
  //Checks the validity of a LEVEL2 type SMA packet

  if (!containsLevel2Packet())
  {
    //Wrong packet index received
    //Serial.println("*** in validateChecksum(). containsLevel2Packet() returned FALSE.");
    dumpPacket('R');

    Serial.print(F("Wrng L2 hdr"));
    return false;
  }
  //Serial.println("*** in validateChecksum(). containsLevel2Packet() returned TRUE.");
  //We should probably do this loop in the receive packet code

  if (level1packet[28 - 1] != lastpacketindex)
  {
    //Wrong packet index received
    dumpPacket('R');

    Serial.print(F("Wrng Pkg count="));
    Serial.println(level1packet[28 - 1], HEX);
    Serial.println(lastpacketindex, HEX);
    return false;
  }

  FCSChecksum = 0xffff;
  //Skip over 0x7e and 0x7e at start and end of packet
  for (int i = 1; i <= packetposition - 4; i++)
  {
    FCSChecksum = (FCSChecksum >> 8) ^ pgm_read_word_near(&fcstab[(FCSChecksum ^ level1packet[i]) & 0xff]);
  }

  FCSChecksum = FCSChecksum ^ 0xffff;

  //if (1==1)
  if ((level1packet[packetposition - 3] == (FCSChecksum & 0x00ff)) && (level1packet[packetposition - 2] == ((FCSChecksum >> 8) & 0x00ff)))
  {
    if ((level1packet[23 + 1] != 0) || (level1packet[24 + 1] != 0))
    {
      log_w("chksum err");
      error();
    }
    return true;
  }
  else
  {
    log_w("Invalid chk= ");
    Serial.print(F("Invalid chk="));
    Serial.println(FCSChecksum, HEX);
    dumpPacket('R');
    delay(10000);
    return false;
  }
}
//------------------------------------------------------------------------------------
/*
void InquireBlueToothSignalStrength() {
  writePacketHeader(level1packet,0x03,0x00,smaBTInverterAddressArray);
  //unsigned char a[2]= {0x05,0x00};
  writeSMANET2SingleByte(level1packet,0x05);
  writeSMANET2SingleByte(level1packet,0x00);  
  //  writePacketPayload(level1packet,a,sizeof(a));
  writePacketLength(level1packet);
  sendPacket(level1packet);
  waitForPacket(0x0004); 
  float bluetoothSignalStrength = (level1packet[4] * 100.0) / 0xff;
  Serial.print(F("BT Sig="));
  Serial.print(bluetoothSignalStrength);
  Serial.println("%");
}
*/
String getMAC(unsigned char *addr) {
  String macRet = String("00:00:00:00:00:00");

  macRet.clear();
  for (int i = 0; i < 6; i++)
  {
    char str[3];
    sprintf(str, "%02X", (int)addr[i]);
    macRet.concat(str);
    if (i < 5)
    {
      macRet.concat(":");
    }
  }
  return macRet;
}

bool ValidateSenderAddress()
{
  // Compares the SMA inverter address to the "from" address contained in the message.
  // Debug prints "P wrng dest" if there is no match.
  log_d("Lvel1: %s SMABT: %s ", getMAC(Level1SrcAdd).c_str(), getMAC(smaBTInverterAddressArray).c_str());

  bool ret = (Level1SrcAdd[5] == smaBTInverterAddressArray[5] &&
              Level1SrcAdd[4] == smaBTInverterAddressArray[4] &&
              Level1SrcAdd[3] == smaBTInverterAddressArray[3] &&
              Level1SrcAdd[2] == smaBTInverterAddressArray[2] &&
              Level1SrcAdd[1] == smaBTInverterAddressArray[1] &&
              Level1SrcAdd[0] == smaBTInverterAddressArray[0]);
  log_d("Sending back: %d", ret);
  return ret;
}

bool IsPacketForMe()
{
  // Compares the ESP32 address to the received "to" address in the message.
  // Debug prints "P wrng snder" if it does not match.
  // debugMsg("My BT address: ");
  // printMAC(myBTAddress);
  // debugMsgLn("");
  // debugMsg("Level1dstaddr: ");
  // printMAC(Level1DestAdd);
  // debugMsgLn("");

  return (ValidateDestAddress(sixzeros) || ValidateDestAddress(myBTAddress) || ValidateDestAddress(sixff));
}

bool ValidateDestAddress(unsigned char *address)
{
  return (Level1DestAdd[0] == address[0] &&
          Level1DestAdd[1] == address[1] &&
          Level1DestAdd[2] == address[2] &&
          Level1DestAdd[3] == address[3] &&
          Level1DestAdd[4] == address[4] &&
          Level1DestAdd[5] == address[5]);
}
