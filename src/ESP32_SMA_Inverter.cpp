#include "ESP32_SMA_Inverter.h"

#include "bluetooth.h"
#include "SMANetArduino.h"
#include "LocalUtil.h"
#include "mainstate.h"

#include <ets-appender.hpp>
#include <udp-appender.hpp>

#include "site_details.h"

bool ESP32_SMA_Inverter::initialiseSMAConnection()
{
  logD("initialiseSMAConnection(%i) ", innerstate);


  unsigned char netid;
  switch (innerstate)
  {
  case 0:
    //Wait for announcement/broadcast message from PV inverter
    if (getPacket(0x0002))
      innerstate++;
    break;

  case 1:
    //Extract data from the 0002 packet
    netid = level1packet[4];

    // Now create a response and send it.
    for (int i = 0; i < sizeof(level1packet); i++)
      level1packet[i] = 0x00;

    writePacketHeader(level1packet, 0x02, 0x00, smaBTInverterAddressArray);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet99, sizeof_smanet2packet99 );
    writeSMANET2SingleByte(level1packet, netid);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros );
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet0x01000000, sizeof(smanet2packet0x01000000));
    writePacketLength(level1packet);
    sendPacket(level1packet);
    innerstate++;
    break;

  case 2:
    // The SMA inverter will respond with a packet carrying the command '0x000A'.
    // It will return with cmdcode set to 0x000A.
    if (getPacket(0x000a))
      innerstate++;
    break;

  case 3:
    // The SMA inverter will now send two packets, one carrying the '0x000C' command, then the '0x0005' command.
    // Sit in the following loop until you get one of these two packets.
    cmdcode = readLevel1PacketFromBluetoothStream(0);
    if ((cmdcode == 0x000c) || (cmdcode == 0x0005))
      innerstate++;
    break;

  case 4:
    // If the most recent packet was command code = 0x0005 skip this next line, otherwise, wait for 0x0005 packet.
    // Since the first SMA packet after a 0x000A packet will be a 0x000C packet, you'll probably sit here waiting at least once.
    if (cmdcode == 0x0005)
    {
      innerstate++;
    }
    else
    {
      if (getPacket(0x0005))
        innerstate++;
    }
    break;

  case 5:
    //First SMANET2 packet
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof_smanet2packetx80x00x02x00);
    writeSMANET2SingleByte(level1packet, 0x00);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);

    if (getPacket(0x0001) && validateChecksum())
    {
      innerstate++;
      packet_send_counter++;
    }
    break;

  case 6:
    //Second SMANET2 packet
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x08, 0xa0, packet_send_counter, 0x00, 0x03, 0x03);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet2, sizeof_smanet2packet2);

    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);
    packet_send_counter++;

    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}




bool ESP32_SMA_Inverter::logonSMAInverter()
{
  logD("logonSMAInverter(%i)" , innerstate);

  //Third SMANET2 packet
  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet, sixff);
    writeSMANET2PlusPacket(level1packet, 0x0e, 0xa0, packet_send_counter, 0x00, 0x01, 0x01);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet_logon, sizeof(smanet2packet_logon));
    writeSMANET2ArrayFromProgmem(level1packet, fourzeros, sizeof_fourzeros);

    //INVERTER PASSWORD
    for (int passcodeloop = 0; passcodeloop < sizeof(SMAInverterPasscode); passcodeloop++)
    {
      unsigned char v = pgm_read_byte(SMAInverterPasscode + passcodeloop);
      writeSMANET2SingleByte(level1packet, (v + 0x88) % 0xff);
    }

    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);
    sendPacket(level1packet);

    innerstate++;
    break;

  case 1:
    if (getPacket(0x0001) && validateChecksum())
    {
      innerstate++;
      packet_send_counter++;
    }
    break;

  default:
    return true;
  }

  return false;
}



bool ESP32_SMA_Inverter::getDailyYield()
{
  //We expect a multi packet reply to this question...
  //We ask the inverter for its DAILY yield (generation)
  //once this is returned we can extract the current date/time from the inverter and set our internal clock
  logD("getDailyYield(%i)" , innerstate);

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packet6, sizeof(smanet2packet6));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);

    sendPacket(level1packet);

    innerstate++;
    break;

  case 1:
    if (getPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    //Returns packet looking like this...
    //    7E FF 03 60 65 0D 90 5C AF F0 1D 50 00 00 A0 83
    //    00 1E 6C 5D 7E 00 00 00 00 00 00 03
    //    80 01 02 00
    //    54 01 00 00 00 01 00 00 00 01
    //    22 26  //command code 0x2622 daily yield
    //    00     //unknown
    //    D6 A6 99 4F  //Unix time stamp (backwards!) = 1335469782 = Thu, 26 Apr 2012 19:49:42 GMT
    //    D9 26 00     //Daily generation 9.945 kwh
    //    00
    //    00 00 00 00
    //    18 61    //checksum
    //    7E       //packet trailer

    // Does this packet contain the British Summer time flag?
    //dumpPacket('Y');

    valuetype = level1packet[40 + 1 + 1] + level1packet[40 + 2 + 1] * 256;

    //Serial.println(valuetype,HEX);
    //Make sure this is the right message type
    if (valuetype == 0x2622)
    {
      memcpy(&value64, &level1packet[40 + 8 + 1], 8);
      //0x2622=Day Yield Wh
      // memcpy(&datetime, &level1packet[40 + 4 + 1], 4);
      datetime = LocalUtil::get_long(level1packet + 40 + 1 + 4);
      // debugMsg("Current Time (epoch): ");
      // debugMsgLn(String(datetime));

      //setTime(datetime);
      currentvalue = value64;
      log_i("Day Yield: %f" , (double)value64 / 1000);
      _client.publish(MQTT_BASE_TOPIC "generation_today", LocalUtil::uint64ToString(currentvalue), true);


      if (1==0) {
        if (!_client.isConnected() ){
            log_i("_client not connected here ");
        } else {
            log_i("_client is connected here ");
        }
      }

    }
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}


bool ESP32_SMA_Inverter::getInstantACPower()
{
  logD("getInstantACPower(%i)" , innerstate);

  int32_t thisvalue;
  //Get spot value for instant AC wattage
  // debugMsg("getInstantACPower stage: ");
  // debugMsgLn(String(innerstate));

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet); //writePacketHeader(pcktBuf, 0x01, addr_unknown);
    //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xA1, packet_send_counter, 0, 0, 0); // writePacket(pcktBuf, 0x09, 0xA0, 0, device->SUSyID, device->Serial);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2acspotvalues, sizeof(smanet2acspotvalues));
    writeSMANET2PlusPacketTrailer(level1packet); //writePacketTrailer(pcktBuf);
    writePacketLength(level1packet); //writePacketLength(pcktBuf);

    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    //value will contain instant/spot AC power generation along with date/time of reading...
    // memcpy(&datetime, &level1packet[40 + 1 + 4], 4);
    datetime = LocalUtil::get_long(level1packet + 40 + 1 + 4);
    thisvalue = LocalUtil::get_long(level1packet + 40 + 1 + 8);
    // memcpy(&thisvalue, &level1packet[40 + 1 + 8], 4);
    
    currentvalue = thisvalue;
    log_i("AC Pwr= %li " , thisvalue);
    _client.publish(MQTT_BASE_TOPIC "instant_ac", LocalUtil::uint64ToString(currentvalue), true);

    spotpowerac = thisvalue;

    //displaySpotValues(28);
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}

bool ESP32_SMA_Inverter::getTotalPowerGeneration()
{

  //Gets the total kWh the SMA inverter has generated in its lifetime...
  // debugMsg("getTotalPowerGeneration stage: ");
  // debugMsgLn(String(innerstate));
  logD("getTotalPowerGeneration(%i)" , innerstate);

  switch (innerstate)
  {
  case 0:
    writePacketHeader(level1packet);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xa0, packet_send_counter, 0, 0, 0);
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2totalyieldWh, sizeof(smanet2totalyieldWh));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);

    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    //displaySpotValues(16);
    memcpy(&datetime, &level1packet[40 + 1 + 4], 4);
    memcpy(&value64, &level1packet[40 + 1 + 8], 8);
    //digitalClockDisplay(datetime);
    currentvalue = value64;
    log_i("Total Power: %f ", (double)value64 / 1000);
    //reportValue(MQTT_BASE_TOPIC "generation_total", LocalUtil::uint64ToString(currentvalue), true);
    _client.publish(MQTT_BASE_TOPIC "generation_total", LocalUtil::uint64ToString(currentvalue), true);
    innerstate++;
    break;

  default:
    return true;
  }

  return false;
}

bool ESP32_SMA_Inverter::getInstantDCPower()
{
  // 2W - This appears broken...
#ifndef FETCH_DC_INSTANT_POWER
  return true;
#else
  logD("getInstantDCPower(%i)", innerstate);
  //DC
  //We expect a multi packet reply to this question...

  switch (innerstate)
  {
  case 0:

    writePacketHeader(level1packet);
    //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
    writeSMANET2PlusPacket(level1packet, 0x09, 0xE0, packet_send_counter, 0, 0, 0);
    // writeSMANET2ArrayFromProgmem(level1packet, smanet2packetx80x00x02x00, sizeof(smanet2packetx80x00x02x00));
    writeSMANET2ArrayFromProgmem(level1packet, smanet2packetdcpower, sizeof(smanet2packetdcpower));
    writeSMANET2PlusPacketTrailer(level1packet);
    writePacketLength(level1packet);

    sendPacket(level1packet);
    innerstate++;
    break;

  case 1:
    if (waitForMultiPacket(0x0001))
    {
      if (validateChecksum())
      {
        packet_send_counter++;
        innerstate++;
      }
      else
        innerstate = 0;
    }
    break;

  case 2:
    //displaySpotValues(28);
    for (int i = 40 + 1; i < packetposition - 3; i += 28)
    {
      valuetype = level1packet[i + 1] + level1packet[i + 2] * 256;
      memcpy(&value, &level1packet[i + 8], 4);

      //valuetype
      log_d("getInstantDCPower(): valuetype: %li value: %lu ", valuetype, value);

      //0x451f=DC Voltage  /100
      //0x4521=DC Current  /1000
      //0x251e=DC Power /1
      if (valuetype==0x451f) spotvoltdc=(float)value/(float)100.0;

      if (valuetype==0x4521) spotampdc=(float)value/(float)1000.0;

      if (valuetype == 0x251e) {
        if (value > MAX_SPOTDC) {
          spotpowerdc = spotvoltdc * spotampdc; 
        } else {
          spotpowerdc = value;
        }  
      } 

      memcpy(&datetime, &level1packet[i + 4], 4);
    }

    //spotpowerdc=volts*amps;
    log_i("DC Pwr=%lu Volt=%f Amp=%f " , spotpowerdc, spotvoltdc, spotampdc);

    _client.publish(MQTT_BASE_TOPIC "instant_dc", LocalUtil::uint64ToString(spotpowerdc), true);
    _client.publish(MQTT_BASE_TOPIC "instant_vdc", LocalUtil::uint64ToString(spotvoltdc), true);
    _client.publish(MQTT_BASE_TOPIC "instant_adc", LocalUtil::uint64ToString(spotampdc), true);

    innerstate++;
    break;

  default:
    return true;
  }

  return false;
#endif
}



/*
//Inverter name
 prog_uchar PROGMEM smanet2packetinvertername[]={   0x80, 0x00, 0x02, 0x00, 0x58, 0x00, 0x1e, 0x82, 0x00, 0xFF, 0x1e, 0x82, 0x00};  
 
 void getInverterName() {
 
 do {
 //INVERTERNAME
 debugMsgln("InvName"));
 writePacketHeader(level1packet,sixff);
 //writePacketHeader(level1packet,0x01,0x00,sixff);
 writeSMANET2PlusPacket(level1packet,0x09, 0xa0, packet_send_counter, 0, 0, 0);
 writeSMANET2ArrayFromProgmem(level1packet,smanet2packetinvertername);
 writeSMANET2PlusPacketTrailer(level1packet);
 writePacketLength(level1packet);
 sendPacket(level1packet);
 
 waitForMultiPacket(0x0001);
 } 
 while (!validateChecksum());
 packet_send_counter++;
 
 valuetype = level1packet[40+1+1]+level1packet[40+2+1]*256;
 
 if (valuetype==0x821e) {
 memcpy(invertername,&level1packet[48+1],14);
 Serial.println(invertername);
 memcpy(&datetime,&level1packet[40+4+1],4);  //Returns date/time unit switched PV off for today (or current time if its still on)
 }
 }
 
 void HistoricData() {
 
 time_t currenttime=now();
 digitalClockDisplay(currenttime);
 
 debugMsgln("Historic data...."));
 tmElements_t tm;
 if( year(currenttime) > 99)
 tm.Year = year(currenttime)- 1970;
 else
 tm.Year = year(currenttime)+ 30;  
 
 tm.Month = month(currenttime);
 tm.Day = day(currenttime);
 tm.Hour = 10;      //Start each day at 5am (might need to change this if you're lucky enough to live somewhere hot and sunny!!
 tm.Minute = 0;
 tm.Second = 0;
 time_t startTime=makeTime(tm);  //Midnight
 
 
 //Read historic data for today (SMA inverter saves 5 minute averaged data)
 //We read 30 minutes at a time to save RAM on Arduino
 
 //for (int hourloop=1;hourloop<24*2;hourloop++)
 while (startTime < now()) 
 {
 //HowMuchMemory();
 
 time_t endTime=startTime+(25*60);  //25 minutes on
 
 //digitalClockDisplay(startTime);
 //digitalClockDisplay(endTime);
 //debugMsgln(" ");
 
 do {
 writePacketHeader(level1packet);
 //writePacketHeader(level1packet,0x01,0x00,smaBTInverterAddressArray);
 writeSMANET2PlusPacket(level1packet,0x09, 0xE0, packet_send_counter, 0, 0, 0);
 
 writeSMANET2SingleByte(level1packet,0x80);
 writeSMANET2SingleByte(level1packet,0x00);
 writeSMANET2SingleByte(level1packet,0x02);
 writeSMANET2SingleByte(level1packet,0x00);
 writeSMANET2SingleByte(level1packet,0x70);
 // convert from an unsigned long int to a 4-byte array
 writeSMANET2Long(level1packet,startTime);
 writeSMANET2Long(level1packet,endTime);
 writeSMANET2PlusPacketTrailer(level1packet);
 writePacketLength(level1packet);
 sendPacket(level1packet);
 
 waitForMultiPacket(0x0001);
 }
 while (!validateChecksum());
 //debugMsg("packetlength=");    Serial.println(packetlength);
 
 packet_send_counter++;
 
 //Loop through values
 for(int x=40+1;x<(packetposition-3);x+=12){
 memcpy(&value,&level1packet[x+4],4);
 
 if (value > currentvalue) {
 memcpy(&datetime,&level1packet[x],4);
 digitalClockDisplay(datetime);
 debugMsg("=");
 Serial.println(value);
 currentvalue=value;         
 
 //uploadValueToSolarStats(currentvalue,datetime);          
 }
 }
 
 startTime=endTime+(5*60);
 delay(750);  //Slow down the requests to the SMA inverter
 }
 }
 */

//-------------------------------------------------------------------------------------------
bool ESP32_SMA_Inverter::checkIfNeedToSetInverterTime()
{
  //digitalClockDisplay(now());Serial.println("");
  //digitalClockDisplay(datetime);Serial.println("");
  logI("checkIfNeedToSetInverterTime()");

  unsigned long timediff;

  timediff = abs((long) (datetime - esp32rtc.getEpoch()));
  logI("Time diff: %lu, datetime: %lu, epoch: %lu " , timediff, datetime, esp32rtc.getEpoch());

  if (timediff > 60)
  {
    //If inverter clock is out by more than 1 minute, set it to the time from NTP, saves filling up the
    //inverters event log with hundred of "change time" lines...
    setInverterTime(); //Set inverter time to now()
  }

  return true;
}



void ESP32_SMA_Inverter::setInverterTime()
{
  //Sets inverter time for those SMA inverters which don't have a realtime clock (Tripower 8000 models for instance)
  logD("setInverterTime(%i) " , innerstate);

  //Payload...

  //** 8C 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   9F AE 99 4F   ==Thu, 26 Apr 2012 20:22:55 GMT  (now)
  //   01 00         ==Timezone +1 hour for BST ?
  //   00 00
  //   A1 A5 99 4F   ==Thu, 26 Apr 2012 19:44:33 GMT  (strange date!)
  //   01 00 00 00
  //   F3 D9         ==Checksum
  //   7E            ==Trailer

  //Set time to Feb

  //2A 20 63 00 5F 00 B1 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 01
  //** 8D 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //14 02 2B 4F ==Thu, 02 Feb 2012 21:37:24 GMT
  //14 02 2B 4F ==Thu, 02 Feb 2012 21:37:24 GMT
  //14 02 2B 4F  ==Thu, 02 Feb 2012 21:37:24 GMT
  //00 00        ==No time zone/BST not applicable for Feb..
  //00 00
  //AD B1 99 4F  ==Thu, 26 Apr 2012 20:35:57 GMT
  //01 00 00 00
  //F6 87        ==Checksum
  //7E

  //2A 20 63 00 5F 00 B1 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 1C
  //** 8D 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //F5 B3 99 4F
  //F5 B3 99 4F
  //F5 B3 99 4F 01 00 00 00 28 B3 99 4F 01 00 00 00
  //F3 C7 7E

  //2B 20 63 00 5F 00 DD 00 0B FF B5 01
  //7E 5A 00 24 A3 0B 50 DD 09 00 FF FF FF FF FF FF 01 00
  //7E FF 03 60 65 10 A0 FF FF FF FF FF FF 00 00 78 00 6E 21 96 37 00 00 00 00 00 00 08
  //** 80 0A 02 00 F0 00 6D 23 00 00 6D 23 00 00 6D 23 00
  //64 76 99 4F ==Thu, 26 Apr 2012 16:23:00 GMT
  //64 76 99 4F ==Thu, 26 Apr 2012 16:23:00 GMT
  //64 76 99 4F  ==Thu, 26 Apr 2012 16:23:00 GMT
  //58 4D   ==19800 seconds = 5.5 hours
  //00 00
  //62 B5 99 4F
  //01 00 00 00
  //C3 27 7E

  
  time_t currenttime = esp32rtc.getEpoch(); // Returns the ESP32 RTC in number of seconds since the epoch (normally 01/01/1970)
  //digitalClockDisplay(currenttime);
  writePacketHeader(level1packet);
  writeSMANET2PlusPacket(level1packet, 0x09, 0x00, packet_send_counter, 0, 0, 0);
  writeSMANET2ArrayFromProgmem(level1packet, smanet2settime, sizeof(smanet2settime));
  writeSMANET2Long(level1packet, currenttime);
  writeSMANET2Long(level1packet, currenttime);
  writeSMANET2Long(level1packet, currenttime);
  // writeSMANET2Long(level1packet, timeZoneOffset);
  writeSMANET2uint(level1packet, timeZoneOffset);
  writeSMANET2uint(level1packet, 0);
  writeSMANET2Long(level1packet, currenttime); //No idea what this is for...
  writeSMANET2ArrayFromProgmem(level1packet, smanet2packet0x01000000, sizeof(smanet2packet0x01000000));
  writeSMANET2PlusPacketTrailer(level1packet);
  writePacketLength(level1packet);
  sendPacket(level1packet);
  packet_send_counter++;
  //debugMsgln(" done");
}

