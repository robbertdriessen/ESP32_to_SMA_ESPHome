
#ifndef SMANETARDUINO_H_
#define SMANETARDUINO_H_

/*
 NANODE SMA PV MONITOR
 SMANetArduino.h
 */

// Holds a fully reassembled L2 frame. Responses with more than ~2 records are
// split by the inverter's BT module into several L1 fragments (cmd 0x0008
// followed by a final 0x0001), so this must be larger than one L1 packet.
#define maxlevel1packetsize 512
#define level1headerlength 18
//const byte maxlevel1packetsize = 120;
//const byte level1headerlength = 18;


extern unsigned int packetlength ;
extern unsigned int cmdcode ;

//Should be moved into PROGMEM
extern unsigned char sixzeros[];
//Should be moved into PROGMEM
extern unsigned char sixff[];

extern unsigned int packetposition;

extern unsigned int FCSChecksum;

extern unsigned char Level1SrcAdd[];
extern unsigned char Level1DestAdd[];

//char invertername[15]={0};
extern unsigned char packet_send_counter;
extern unsigned char lastpacketindex;
extern unsigned char level1packet[maxlevel1packetsize];
extern bool escapenextbyte;
// Error code from the L2 header (bytes 23-24) of the last frame that passed
// validateChecksum(). 0 = OK, 0x0100 = invalid password on logon, anything
// else usually means the requested LRI is not supported.
extern unsigned int l2ErrorCode;

extern prog_uchar PROGMEM SMANET2header[];
extern prog_uchar PROGMEM InverterCodeArray[];
extern prog_uchar PROGMEM fourzeros[];
extern size_t sizeof_fourzeros;

unsigned int readLevel1PacketFromBluetoothStream(int index);
void prepareToReceive();
bool containsLevel2Packet();
bool getPacket(unsigned int cmdcodetowait);
void waitForPacket(unsigned int xxx);
bool waitForMultiPacket(unsigned int cmdcodetowait);
void writeSMANET2Long(unsigned char *btbuffer, unsigned long v);
void writeSMANET2uint(unsigned char *btbuffer, unsigned int v);
void writeSMANET2SingleByte(unsigned char *btbuffer, unsigned char v);
void writeSMANET2Array(unsigned char *btbuffer, unsigned char bytes[], byte loopcount);
void writeSMANET2ArrayFromProgmem(unsigned char *btbuffer, prog_uchar ProgMemArray[], byte loopcount);
void writeSMANET2PlusPacket(unsigned char *btbuffer, unsigned char ctrl1, unsigned char ctrl2, unsigned char packetcount, unsigned char a, unsigned char b, unsigned char c);
void writeSMANET2PlusPacketTrailer(unsigned char *btbuffer);
void writePacketHeader(unsigned char *btbuffer);
void writePacketHeader(unsigned char *btbuffer, unsigned char *destaddress);
void writePacketHeader(unsigned char *btbuffer, unsigned char cmd1, unsigned char cmd2, unsigned char *destaddress);
void writePacketLength(unsigned char *btbuffer);
void wrongPacket();
void dumpPacket(char letter);
void debugPrintHexByte(unsigned char b);
bool validateChecksum();
String getMAC(unsigned char *addr);
bool ValidateSenderAddress();
bool IsPacketForMe();
bool ValidateDestAddress(unsigned char *address);

// Reset SMA-protocol packet counters between cycles (after a logoff +
// BT disconnect). The inverter restarts its own counter on a fresh login,
// so leaving stale values here causes the post-reconnect handshake to
// report unexpected packet indices.
void resetSMANetCounters();


#endif