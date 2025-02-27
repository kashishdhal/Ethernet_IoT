// ETH0 Library
// Jason Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// ENC28J60 Ethernet controller on SPI0
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS (SW controlled) on PA3
//   WOL on PB3
//   INT on PC6

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <eth0.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "tm4c123gh6pm.h"
#include "wait.h"
#include "gpio.h"
#include "spi0.h"
#include "shell.h"

// Pins
#define CS PORTA,3
#define WOL PORTB,3
#define INT PORTC,6

// Ether registers
#define ERDPTL      0x00
#define ERDPTH      0x01
#define EWRPTL      0x02
#define EWRPTH      0x03
#define ETXSTL      0x04
#define ETXSTH      0x05
#define ETXNDL      0x06
#define ETXNDH      0x07
#define ERXSTL      0x08
#define ERXSTH      0x09
#define ERXNDL      0x0A
#define ERXNDH      0x0B
#define ERXRDPTL    0x0C
#define ERXRDPTH    0x0D
#define ERXWRPTL    0x0E
#define ERXWRPTH    0x0F
#define EIE         0x1B
#define EIR         0x1C
#define RXERIF  0x01
#define TXERIF  0x02
#define TXIF    0x08
#define PKTIF   0x40
#define ESTAT       0x1D
#define CLKRDY  0x01
#define TXABORT 0x02
#define ECON2       0x1E
#define PKTDEC  0x40
#define ECON1       0x1F
#define RXEN    0x04
#define TXRTS   0x08
#define ERXFCON     0x38
#define EPKTCNT     0x39
#define MACON1      0x40
#define MARXEN  0x01
#define RXPAUS  0x04
#define TXPAUS  0x08
#define MACON2      0x41
#define MARST   0x80
#define MACON3      0x42
#define FULDPX  0x01
#define FRMLNEN 0x02
#define TXCRCEN 0x10
#define PAD60   0x20
#define MACON4      0x43
#define MABBIPG     0x44
#define MAIPGL      0x46
#define MAIPGH      0x47
#define MACLCON1    0x48
#define MACLCON2    0x49
#define MAMXFLL     0x4A
#define MAMXFLH     0x4B
#define MICMD       0x52
#define MIIRD   0x01
#define MIREGADR    0x54
#define MIWRL       0x56
#define MIWRH       0x57
#define MIRDL       0x58
#define MIRDH       0x59
#define MAADR1      0x60
#define MAADR0      0x61
#define MAADR3      0x62
#define MAADR2      0x63
#define MAADR5      0x64
#define MAADR4      0x65
#define MISTAT      0x6A
#define MIBUSY  0x01
#define ECOCON      0x75

// Ether phy registers
#define PHCON1      0x00
#define PDPXMD 0x0100
#define PHSTAT1     0x01
#define LSTAT  0x0400
#define PHCON2      0x10
#define HDLDIS 0x0100
#define PHLCON      0x14

// Packets
#define IP_ADD_LENGTH 4
#define HW_ADD_LENGTH 6

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t nextPacketLsb = 0x00;
uint8_t nextPacketMsb = 0x00;
uint8_t sequenceId = 1;
uint32_t sum;
uint8_t macAddress[HW_ADD_LENGTH] = {2,3,4,5,6,7};
uint8_t ipAddress[IP_ADD_LENGTH] = {0,0,0,0};
uint8_t ipSubnetMask[IP_ADD_LENGTH] = {255,255,255,0};
uint8_t ipGwAddress[IP_ADD_LENGTH] = {192,168,10,1};
bool    dhcpEnabled = true;
uint8_t payload = 1;
uint16_t checksum;
uint16_t portNum;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

// This M4F is little endian (TI hardwired it this way)
// Network byte order is big endian
// Must interpret uint16_t in reverse order

typedef struct _enc28j60Frame // 4-bytes
{
  uint16_t size;
  uint16_t status;
  uint8_t data;
} enc28j60Frame;

typedef struct _etherFrame // 14-bytes
{
  uint8_t destAddress[6];
  uint8_t sourceAddress[6];
  uint16_t frameType;
  uint8_t data;
} etherFrame;

typedef struct _loopbackEtherFrame
{
  uint16_t frameType;
  uint8_t data;
} loopbackEtherFrame;

typedef struct _ipFrame // minimum 20 bytes
{
  uint8_t revSize;
  uint8_t typeOfService;
  uint16_t length;
  uint16_t id;
  uint16_t flagsAndOffset;
  uint8_t ttl;
  uint8_t protocol;
  uint16_t headerChecksum;
  uint8_t sourceIp[4];
  uint8_t destIp[4];
} ipFrame;

typedef struct _icmpFrame
{
  uint8_t type;
  uint8_t code;
  uint16_t check;
  uint16_t id;
  uint16_t seq_no;
  uint8_t data;
} icmpFrame;

typedef struct _arpFrame
{
  uint16_t hardwareType;
  uint16_t protocolType;
  uint8_t hardwareSize;
  uint8_t protocolSize;
  uint16_t op;
  uint8_t sourceAddress[6];
  uint8_t sourceIp[4];
  uint8_t destAddress[6];
  uint8_t destIp[4];
} arpFrame;

typedef struct _udpFrame // 8 bytes
{
  uint16_t sourcePort;
  uint16_t destPort;
  uint16_t length;
  uint16_t check;
  uint8_t  data;
} udpFrame;

typedef struct _tcpFrame // 20 bytes + options
{
  uint16_t sourcePort;
  uint16_t destPort;
  uint32_t seqNum;
  uint32_t ackNum;
  uint16_t dataResFlags;
  uint16_t winSize;
  uint16_t check;
  uint16_t urgPointer;
  uint8_t  data;
  uint8_t options[4];
} tcpFrame;

typedef struct _mqttFrame
{
   uint8_t control;
   uint8_t msgLength;
   uint16_t nameLength;
   uint8_t name[4];
   uint8_t version;
   uint8_t connectFlag;
   uint16_t ttl;
   uint16_t clientIdLength;
   uint8_t clientId[23];

} mqttFrame;

typedef struct _mqttPublishFrame
{
   uint8_t control;
   uint8_t msgLength;
   uint16_t topicLength;
   uint8_t topicNameAndMessage[100];
} mqttPublishFrame;

typedef struct _mqttSubReqFrame
{
   uint8_t control;
   uint8_t msgLength;
   uint16_t msgID;
   uint16_t topicLength;
   uint8_t topicName[100];
   uint8_t QoS;
} mqttSubReqFrame;

typedef struct _mqttUnSubReqFrame
{
   uint8_t control;
   uint8_t msgLength;
   uint16_t msgID;
   uint16_t topicLength;
   uint8_t topicName[100];
} mqttUnSubReqFrame;


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Buffer is configured as follows
// Receive buffer starts at 0x0000 (bottom 6666 bytes of 8K space)
// Transmit buffer at 01A0A (top 1526 bytes of 8K space)

void etherCsOn()
{
    setPinValue(CS, 0);
    __asm (" NOP");                    // allow line to settle
    __asm (" NOP");
    __asm (" NOP");
    __asm (" NOP");
}

void etherCsOff()
{
    setPinValue(CS, 1);
}

void etherWriteReg(uint8_t reg, uint8_t data)
{
    etherCsOn();
    writeSpi0Data(0x40 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(data);
    readSpi0Data();
    etherCsOff();
}

uint8_t etherReadReg(uint8_t reg)
{
    uint8_t data;
    etherCsOn();
    writeSpi0Data(0x00 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(0);
    data = readSpi0Data();
    etherCsOff();
    return data;
}

void etherSetReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0x80 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherClearReg(uint8_t reg, uint8_t mask)
{
    etherCsOn();
    writeSpi0Data(0xA0 | (reg & 0x1F));
    readSpi0Data();
    writeSpi0Data(mask);
    readSpi0Data();
    etherCsOff();
}

void etherSetBank(uint8_t reg)
{
    etherClearReg(ECON1, 0x03);
    etherSetReg(ECON1, reg >> 5);
}

void etherWritePhy(uint8_t reg, uint16_t data)
{
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MIWRL, data & 0xFF);
    etherWriteReg(MIWRH, (data >> 8) & 0xFF);
}

uint16_t etherReadPhy(uint8_t reg)
{
    uint16_t data, dataH;
    etherSetBank(MIREGADR);
    etherWriteReg(MIREGADR, reg);
    etherWriteReg(MICMD, MIIRD);
    waitMicrosecond(11);
    etherSetBank(MISTAT);
    while ((etherReadReg(MISTAT) & MIBUSY) != 0);
    etherSetBank(MICMD);
    etherWriteReg(MICMD, 0);
    data = etherReadReg(MIRDL);
    dataH = etherReadReg(MIRDH);
    data |= (dataH << 8);
    return data;
}

void etherWriteMemStart()
{
    etherCsOn();
    writeSpi0Data(0x7A);
    readSpi0Data();
}

void etherWriteMem(uint8_t data)
{
    writeSpi0Data(data);
    readSpi0Data();
}

void etherWriteMemStop()
{
    etherCsOff();
}

void etherReadMemStart()
{
    etherCsOn();
    writeSpi0Data(0x3A);
    readSpi0Data();
}

uint8_t etherReadMem()
{
    writeSpi0Data(0);
    return readSpi0Data();
}

void etherReadMemStop()
{
    etherCsOff();
}

// Initializes ethernet device
// Uses order suggested in Chapter 6 of datasheet except 6.4 OST which is first here
void etherInit(uint16_t mode)
{
    // Initialize SPI0
    initSpi0(USE_SSI0_RX);
    setSpi0BaudRate(4e6, 40e6);
    setSpi0Mode(0, 0);

    // Enable clocks
    enablePort(PORTA);
    enablePort(PORTB);
    enablePort(PORTC);

    // Configure pins for ethernet module
    selectPinPushPullOutput(CS);
    selectPinDigitalInput(WOL);
    selectPinDigitalInput(INT);

    // make sure that oscillator start-up timer has expired
    while ((etherReadReg(ESTAT) & CLKRDY) == 0) {}

    // disable transmission and reception of packets
    etherClearReg(ECON1, RXEN);
    etherClearReg(ECON1, TXRTS);

    // initialize receive buffer space
    etherSetBank(ERXSTL);
    etherWriteReg(ERXSTL, LOBYTE(0x0000));
    etherWriteReg(ERXSTH, HIBYTE(0x0000));
    etherWriteReg(ERXNDL, LOBYTE(0x1A09));
    etherWriteReg(ERXNDH, HIBYTE(0x1A09));
   
    // initialize receiver write and read ptrs
    // at startup, will write from 0 to 1A08 only and will not overwrite rd ptr
    etherWriteReg(ERXWRPTL, LOBYTE(0x0000));
    etherWriteReg(ERXWRPTH, HIBYTE(0x0000));
    etherWriteReg(ERXRDPTL, LOBYTE(0x1A09));
    etherWriteReg(ERXRDPTH, HIBYTE(0x1A09));
    etherWriteReg(ERDPTL, LOBYTE(0x0000));
    etherWriteReg(ERDPTH, HIBYTE(0x0000));

    // setup receive filter
    // always check CRC, use OR mode
    etherSetBank(ERXFCON);
    etherWriteReg(ERXFCON, (mode | ETHER_CHECKCRC) & 0xFF);

    // bring mac out of reset
    etherSetBank(MACON2);
    etherWriteReg(MACON2, 0);
  
    // enable mac rx, enable pause control for full duplex
    etherWriteReg(MACON1, TXPAUS | RXPAUS | MARXEN);

    // enable padding to 60 bytes (no runt packets)
    // add crc to tx packets, set full or half duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MACON3, FULDPX | FRMLNEN | TXCRCEN | PAD60);
    else
        etherWriteReg(MACON3, FRMLNEN | TXCRCEN | PAD60);

    // leave MACON4 as reset

    // set maximum rx packet size
    etherWriteReg(MAMXFLL, LOBYTE(1518));
    etherWriteReg(MAMXFLH, HIBYTE(1518));

    // set back-to-back inter-packet gap to 9.6us
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWriteReg(MABBIPG, 0x15);
    else
        etherWriteReg(MABBIPG, 0x12);

    // set non-back-to-back inter-packet gap registers
    etherWriteReg(MAIPGL, 0x12);
    etherWriteReg(MAIPGH, 0x0C);

    // leave collision window MACLCON2 as reset

    // setup mac address
    etherSetBank(MAADR0);
    etherWriteReg(MAADR5, macAddress[0]);
    etherWriteReg(MAADR4, macAddress[1]);
    etherWriteReg(MAADR3, macAddress[2]);
    etherWriteReg(MAADR2, macAddress[3]);
    etherWriteReg(MAADR1, macAddress[4]);
    etherWriteReg(MAADR0, macAddress[5]);

    // initialize phy duplex
    if ((mode & ETHER_FULLDUPLEX) != 0)
        etherWritePhy(PHCON1, PDPXMD);
    else
        etherWritePhy(PHCON1, 0);

    // disable phy loopback if in half-duplex mode
    etherWritePhy(PHCON2, HDLDIS);

    // Flash LEDA and LEDB
    etherWritePhy(PHLCON, 0x0880);
    waitMicrosecond(100000);

    // set LEDA (link status) and LEDB (tx/rx activity)
    // stretch LED on to 40ms (default)
    etherWritePhy(PHLCON, 0x0472);

    // enable reception
    etherSetReg(ECON1, RXEN);
}

// Returns true if link is up
bool etherIsLinkUp()
{
    return (etherReadPhy(PHSTAT1) & LSTAT) != 0;
}

// Returns TRUE if packet received
bool etherIsDataAvailable()
{
    return ((etherReadReg(EIR) & PKTIF) != 0);
}

// Returns true if rx buffer overflowed after correcting the problem
bool etherIsOverflow()
{
    bool err;
    err = (etherReadReg(EIR) & RXERIF) != 0;
    if (err)
        etherClearReg(EIR, RXERIF);
    return err;
}

// Returns up to max_size characters in data buffer
// Returns number of bytes copied to buffer
// Contents written are 16-bit size, 16-bit status, payload excl crc
uint16_t etherGetPacket(uint8_t packet[], uint16_t maxSize)
{
    uint16_t i = 0, size, tmp16, status;

    // enable read from FIFO buffers
    etherReadMemStart();

    // get next packet information
    nextPacketLsb = etherReadMem();
    nextPacketMsb = etherReadMem();

    // calc size
    // don't return crc, instead return size + status, so size is correct
    size = etherReadMem();
    tmp16 = etherReadMem();
    size |= (tmp16 << 8);

    // get status (currently unused)
    status = etherReadMem();
    tmp16 = etherReadMem();
    status |= (tmp16 << 8);

    // copy data
    if (size > maxSize)
        size = maxSize;
    while (i < size)
        packet[i++] = etherReadMem();

    // end read from FIFO buffers
    etherReadMemStop();

    // advance read pointer
    etherSetBank(ERXRDPTL);
    etherWriteReg(ERXRDPTL, nextPacketLsb); // hw ptr
    etherWriteReg(ERXRDPTH, nextPacketMsb);
    etherWriteReg(ERDPTL, nextPacketLsb);   // dma rd ptr
    etherWriteReg(ERDPTH, nextPacketMsb);

    // decrement packet counter so that PKTIF is maintained correctly
    etherSetReg(ECON2, PKTDEC);

    return size;
}

// Writes a packet
bool etherPutPacket(uint8_t packet[], uint16_t size)
{
    uint16_t i;

    // clear out any tx errors
    if ((etherReadReg(EIR) & TXERIF) != 0)
    {
        etherClearReg(EIR, TXERIF);
        etherSetReg(ECON1, TXRTS);
        etherClearReg(ECON1, TXRTS);
    }

    // set DMA start address
    etherSetBank(EWRPTL);
    etherWriteReg(EWRPTL, LOBYTE(0x1A0A));
    etherWriteReg(EWRPTH, HIBYTE(0x1A0A));

    // start FIFO buffer write
    etherWriteMemStart();

    // write control byte
    etherWriteMem(0);

    // write data
    for (i = 0; i < size; i++)
        etherWriteMem(packet[i]);

    // stop write
    etherWriteMemStop();
  
    // request transmit
    etherWriteReg(ETXSTL, LOBYTE(0x1A0A));
    etherWriteReg(ETXSTH, HIBYTE(0x1A0A));
    etherWriteReg(ETXNDL, LOBYTE(0x1A0A+size));
    etherWriteReg(ETXNDH, HIBYTE(0x1A0A+size));
    etherClearReg(EIR, TXIF);
    etherSetReg(ECON1, TXRTS);

    // wait for completion
    while ((etherReadReg(ECON1) & TXRTS) != 0);

    // determine success
    return ((etherReadReg(ESTAT) & TXABORT) == 0);
}

// Calculate sum of words
// Must use getEtherChecksum to complete 1's compliment addition
void etherSumWords(void* data, uint16_t sizeInBytes)
{
	uint8_t* pData = (uint8_t*)data;
    uint16_t i;
    uint8_t phase = 0;
    uint16_t data_temp;
    for (i = 0; i < sizeInBytes; i++)
    {
        if (phase)
        {
            data_temp = *pData;
            sum += data_temp << 8;
        }
        else
          sum += *pData;
        phase = 1 - phase;
        pData++;
    }
}

// Completes 1's compliment addition by folding carries back into field
uint16_t getEtherChecksum()
{
    uint16_t result;
    // this is based on rfc1071
    while ((sum >> 16) > 0)
      sum = (sum & 0xFFFF) + (sum >> 16);
    result = sum & 0xFFFF;
    return ~result;
}

void etherCalcIpChecksum(ipFrame* ip)
{
    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();
}

// Converts from host to network order and vice versa
uint16_t htons(uint16_t value)
{
    return ((value & 0xFF00) >> 8) + ((value & 0x00FF) << 8);
}
#define ntohs htons

uint32_t htons32(uint32_t value)
{
    return ( ( (value & 0xFF000000) >> 24 ) + ( (value & 0xFF0000) >> 8) + ((value & 0xFF00) << 8) +  ((value & 0xFF) << 24));
}

#define ntohs32 htons32

// Determines whether packet is IP datagram
bool etherIsIp(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    bool ok;
    ok = (ether->frameType == htons(0x0800));
    if (ok)
    {
        sum = 0;
        etherSumWords(&ip->revSize, (ip->revSize & 0xF) * 4);
        ok = (getEtherChecksum() == 0);
    }
    return ok;
}

// Determines whether packet is IP datagram
bool isEtherConnectACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    bool ok;
    ok = ( mqtt->control == 0x20);
    if(ok){payload = 4;}

    return ok;
}

bool isEtherSYNACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint16_t a = (tcp->dataResFlags)&0xFF00;
    uint16_t b = htons(0x12);
    ok = ( a == b);
    return ok;
}

bool isEtherACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint16_t a = (tcp->dataResFlags)&0xFF00;
    uint16_t b = htons(0x10);
    ok = ( a == b);
    return ok;
}

bool isEtherSubACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    bool ok;
    ok = ( mqtt->control == 0x90);
    if(ok){payload=5;}
    return ok;
}

bool isEtherMqttPublish(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    bool ok;
    ok = ( mqtt->control == 0x30);
    return ok;
}

bool isEtherPushACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint16_t a = (tcp->dataResFlags)&0xFF00;
    uint16_t b = htons(0x18);
    ok = ( a == b);
    return ok;
}

bool isEtherFINACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));

    bool ok;
    uint16_t a = (tcp->dataResFlags)&0xFF00;
    uint16_t b = htons(0x11);
    ok = ( a == b);
    return ok;
}

// Determines whether packet is unicast to this ip
// Must be an IP packet
bool etherIsIpUnicast(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    uint8_t i = 0;
    bool ok = true;
    while (ok && (i < IP_ADD_LENGTH))
    {
        ok = (ip->destIp[i] == ipAddress[i]);
        i++;
    }
    return ok;
}

// Determines whether packet is ping request
// Must be an IP packet
bool etherIsPingRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    icmpFrame* icmp = (icmpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    return (ip->protocol == 0x01 && icmp->type == 8);
}

// Sends a ping response given the request data
void etherSendPingResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    icmpFrame* icmp = (icmpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    uint8_t i, tmp;
    uint16_t icmp_size;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = ip->destIp[i];
        ip->destIp[i] = ip ->sourceIp[i];
        ip->sourceIp[i] = tmp;
    }
    // this is a response
    icmp->type = 0;
    // calc icmp checksum
    sum = 0;
    etherSumWords(&icmp->type, 2);
    icmp_size = ntohs(ip->length);
    icmp_size -= 24; // sub ip header and icmp code, type, and check
    etherSumWords(&icmp->id, icmp_size);
    icmp->check = getEtherChecksum();
    // send packet
    etherPutPacket(ether, 14 + ntohs(ip->length));
}

// Determines whether packet is ARP
bool etherIsArpRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    bool ok;
    uint8_t i = 0;
    ok = (ether->frameType == htons(0x0806));
    while (ok && (i < IP_ADD_LENGTH))
    {
        ok = (arp->destIp[i] == ipAddress[i]);
        i++;
    }
    if (ok)
        ok = (arp->op == htons(1));
    return ok;
}

// Sends an ARP response given the request data
void etherSendArpResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    uint8_t i, tmp;
    // set op to response
    arp->op = htons(2);
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->destAddress[i] = arp->sourceAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = arp->sourceAddress[i] = macAddress[i];
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp = arp->destIp[i];
        arp->destIp[i] = arp->sourceIp[i];
        arp->sourceIp[i] = tmp;
    }
    // send packet
    etherPutPacket(ether, 42);
}

// Sends an ARP request
void etherSendArpRequest(uint8_t packet[], uint8_t ip[])
{
    etherFrame* ether = (etherFrame*)packet;
    arpFrame* arp = (arpFrame*)&ether->data;
    uint8_t i;
    // fill ethernet frame
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
        ether->sourceAddress[i] = macAddress[i];
    }
    ether->frameType = 0x0608;
    // fill arp frame
    arp->hardwareType = htons(1);
    arp->protocolType = htons(0x0800);
    arp->hardwareSize = HW_ADD_LENGTH;
    arp->protocolSize = IP_ADD_LENGTH;
    arp->op = htons(1);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        arp->sourceAddress[i] = macAddress[i];
        arp->destAddress[i] = 0xFF;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        arp->sourceIp[i] = ipAddress[i];
        arp->destIp[i] = ip[i];
    }
    // send packet
    etherPutPacket(ether, 42);
}

// Determines whether packet is UDP datagram
// Must be an IP packet
bool etherIsUdp(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok;
    uint16_t tmp16;
    ok = (ip->protocol == 0x11);
    if (ok)
    {
        // 32-bit sum over pseudo-header
        sum = 0;
        etherSumWords(ip->sourceIp, 8);
        tmp16 = ip->protocol;
        sum += (tmp16 & 0xff) << 8;
        etherSumWords(&udp->length, 2);
        // add udp header and data
        etherSumWords(udp, ntohs(udp->length));
        ok = (getEtherChecksum() == 0);
    }
    return ok;
}

// Gets pointer to UDP payload of frame
uint8_t* etherGetUdpData(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    return &udp->data;
}

// Send responses to a udp datagram 
// destination port, ip, and hardware address are extracted from provided data
// uses destination port of received packet as destination of this packet
void etherSendUdpResponse(uint8_t packet[], uint8_t* udpData, uint8_t udpSize)
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    udpFrame* udp = (udpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    uint8_t *copyData;
    uint8_t i, tmp8;
    uint16_t tmp16;
    // swap source and destination fields
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        tmp8 = ether->destAddress[i];
        ether->destAddress[i] = ether->sourceAddress[i];
        ether->sourceAddress[i] = tmp8;
    }
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        tmp8 = ip->destIp[i];
        ip->destIp[i] = ip->sourceIp[i];
        ip->sourceIp[i] = tmp8;
    }
    // set source port of resp will be dest port of req
    // dest port of resp will be left at source port of req
    // unusual nomenclature, but this allows a different tx
    // and rx port on other machine
    udp->sourcePort = udp->destPort;
    // adjust lengths
    ip->length = htons(((ip->revSize & 0xF) * 4) + 8 + udpSize);
    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();
    udp->length = htons(8 + udpSize);
    // copy data
    copyData = &udp->data;
    for (i = 0; i < udpSize; i++)
        copyData[i] = udpData[i];
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&udp->length, 2);
    // add udp header except crc
    etherSumWords(udp, 6);
    etherSumWords(&udp->data, udpSize);
    udp->check = getEtherChecksum();

    // send packet with size = ether + udp hdr + ip header + udp_size
    etherPutPacket(ether, 22 + ((ip->revSize & 0xF) * 4) + udpSize);
}

uint16_t etherGetId()
{
    return htons(sequenceId);
}

void etherIncId()
{
    sequenceId++;
}

// Enable or disable DHCP mode
void etherEnableDhcpMode()
{
    dhcpEnabled = true;
}

void etherDisableDhcpMode()
{
    dhcpEnabled = false;
}

bool etherIsDhcpEnabled()
{
    return dhcpEnabled;
}
// Determines if the IP address is valid
bool etherIsIpValid()
{
    return ipAddress[0] || ipAddress[1] || ipAddress[2] || ipAddress[3];
}

// Sets IP address
void etherSetIpAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipAddress[0] = ip0;
    ipAddress[1] = ip1;
    ipAddress[2] = ip2;
    ipAddress[3] = ip3;
}

// Gets IP address
void etherGetIpAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipAddress[i];
}

// Sets IP subnet mask
void etherSetIpSubnetMask(uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3)
{
    ipSubnetMask[0] = mask0;
    ipSubnetMask[1] = mask1;
    ipSubnetMask[2] = mask2;
    ipSubnetMask[3] = mask3;
}

// Gets IP subnet mask
void etherGetIpSubnetMask(uint8_t mask[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        mask[i] = ipSubnetMask[i];
}

// Sets IP gateway address
void etherSetIpGatewayAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3)
{
    ipGwAddress[0] = ip0;
    ipGwAddress[1] = ip1;
    ipGwAddress[2] = ip2;
    ipGwAddress[3] = ip3;
}

// Gets IP gateway address
void etherGetIpGatewayAddress(uint8_t ip[4])
{
    uint8_t i;
    for (i = 0; i < 4; i++)
        ip[i] = ipGwAddress[i];
}

// Sets MAC address
void etherSetMacAddress(uint8_t mac0, uint8_t mac1, uint8_t mac2, uint8_t mac3, uint8_t mac4, uint8_t mac5)
{
    macAddress[0] = mac0;
    macAddress[1] = mac1;
    macAddress[2] = mac2;
    macAddress[3] = mac3;
    macAddress[4] = mac4;
    macAddress[5] = mac5;
}

// Gets MAC address
void etherGetMacAddress(uint8_t mac[6])
{
    uint8_t i;
    for (i = 0; i < 6; i++)
        mac[i] = macAddress[i];
}

bool etherIsTcp(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
  //  tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok;
   // uint16_t tmp16;
    ok = (ip->protocol == 0x6);
    if (ok)
    {
      // make sure that the checksum is correct here
    }
    return ok;
}

void sendSyn(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));


    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    portNum = (rand() % (49151 - 1024 + 1)) + 1024;

    tcp->sourcePort = htons(portNum);

    tcp->destPort = htons(1883);

    tcp->seqNum = 0x0000;

    tcp->ackNum = 0x0000;

    uint8_t dataOff =  (20+4)/4; // 20 Bytes header + 4 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x002; // SYN
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=2; //0;
    tcp->check=0;
    tcp->options[0]= 4; //2; //Kind = Maximum Segment Size
    tcp->options[1]= htons(1280); //4; // Length = 4
    tcp->options[2]= 0; // htons(1280); // Value = 1460 Bytes

    // calculate Checksum
    //IP checksum
    // Total length
        ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 4); // 20 bytes header and 4 bytes options
    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();


    uint16_t tcpLength = htons(20 + 4);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+4);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 4);


}

void sendAck(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));


    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // Total length
    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 0); // 20 bytes header and 0 bytes options

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    tcp->sourcePort = tcp->destPort;

    tcp->destPort = htons(1883);

    uint32_t ackNum = tcp->ackNum;
    uint32_t seqNum = tcp->seqNum;

    tcp->ackNum = seqNum;
    tcp->seqNum = ackNum;

    tcp->ackNum = tcp->ackNum + htons32(payload);

    payload = 1;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0010; // ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;


    // calculate Checksum

    uint16_t tcpLength = htons(20 + 0);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+0);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 0);


}


void sendConnectCmd(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->destPort = tcp->destPort;

    tcp->sourcePort = tcp->sourcePort;

    uint32_t ackNum = tcp->ackNum;
    uint32_t seqNum = tcp->seqNum;

    tcp->ackNum = ackNum;
    tcp->seqNum = seqNum;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0018; // PSH ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;


    mqtt->control = 0x10;

    mqtt->nameLength = htons(4);

    mqtt->name[0] = (uint8_t)'M';
    mqtt->name[1] = (uint8_t)'Q';
    mqtt->name[2] = (uint8_t)'T';
    mqtt->name[3] = (uint8_t)'T';

    mqtt->version = 4;

    mqtt->connectFlag = 0x02;

    mqtt->ttl = htons(60);

    char clientId[50] = "hello";

    strcpy(mqtt->clientId,clientId);

    mqtt->clientIdLength = htons(strlen(mqtt->clientId));

    mqtt->msgLength = 12 + strlen(mqtt->clientId); // 12 Bytes + Client ID array size

    // Total length
    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + mqtt->msgLength+2); // 20 IP header + 0 TCP options + 19 MQTT

    // 32-bit sum over ip header
    sum = 0;
    etherSumWords(&ip->revSize, 10);
    etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
    ip->headerChecksum = getEtherChecksum();

    // calculate Checksum

    uint16_t tcpLength = htons(20+mqtt->msgLength+2);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+mqtt->msgLength+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + mqtt->msgLength+2);


}

extern char str2[30];
extern char str3[30];

void publishMqttMessage(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttPublishFrame* mqtt = (mqttPublishFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->sourcePort = tcp->sourcePort;

    tcp->destPort = htons(1883);

    uint32_t ackNum = tcp->ackNum;
    uint32_t seqNum = tcp->seqNum;

    tcp->ackNum = ackNum;
    tcp->seqNum = seqNum;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0018; // PSH ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;


    mqtt->control = 0x30;

    char topicName[20];
    char topicMessage[100];

    strcpy(topicName,str2);
    strcpy(topicMessage,str3);

    strcat( mqtt->topicNameAndMessage, topicName);
    strcat( mqtt->topicNameAndMessage, topicMessage);

    mqtt->topicLength = htons(strlen(topicName));

    mqtt->msgLength = strlen(mqtt->topicNameAndMessage)+2; //


    // Total length
     ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + mqtt->msgLength+2); // 20 IP header + 0 TCP options + 19 MQTT

     // 32-bit sum over ip header
     sum = 0;
     etherSumWords(&ip->revSize, 10);
     etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
     ip->headerChecksum = getEtherChecksum();

    // calculate TCP Checksum

    uint16_t tcpLength = htons(20+mqtt->msgLength+2);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+mqtt->msgLength+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + mqtt->msgLength+2);


}

void subscribeRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttSubReqFrame* mqtt = (mqttSubReqFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->sourcePort = tcp->sourcePort;

    tcp->destPort = tcp->destPort;

    tcp->ackNum = tcp->ackNum;
    tcp->seqNum = tcp->seqNum;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0018; // PSH ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;


    mqtt->control = 0x82;

    mqtt->msgID = htons(1);

    strcpy(mqtt->topicName,str2);

    mqtt->topicLength = htons(strlen(mqtt->topicName));

    mqtt->topicName[mqtt->topicLength] = 0; // QoS

    mqtt->msgLength = strlen(str2)+5; //

    // Total length
     ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + mqtt->msgLength+2); // 20 IP header + 0 TCP options + 19 MQTT

     // 32-bit sum over ip header
     sum = 0;
     etherSumWords(&ip->revSize, 10);
     etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
     ip->headerChecksum = getEtherChecksum();

    // calculate TCP Checksum

    uint16_t tcpLength = htons(20+mqtt->msgLength+2);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+mqtt->msgLength+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + mqtt->msgLength+2);


}


void disconnectRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttPublishFrame* mqtt = (mqttPublishFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->sourcePort = tcp->destPort;

    tcp->destPort = htons(1883);

    uint32_t ackNum = tcp->ackNum;
    uint32_t seqNum = tcp->seqNum;

    tcp->ackNum = seqNum;
    tcp->seqNum = ackNum ;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0019; // FIN PSH ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;


    mqtt->control = 0xe0;
    mqtt->msgLength = 0;

      // Total length
    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 2); // 20 IP header + 0 TCP options + 19 MQTT

     // 32-bit sum over ip header
     sum = 0;
     etherSumWords(&ip->revSize, 10);
     etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
     ip->headerChecksum = getEtherChecksum();

    // calculate TCP Checksum

    uint16_t tcpLength = htons(20+2);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 2);


}

extern char str[MAX_CHARS+1];

void getMqttMessage(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttPublishFrame* mqtt = (mqttPublishFrame*)&tcp->data;

    uint8_t i; char* topicname;

    for(i=0;i<htons(mqtt->topicLength);i++)
    {
        topicname[i] = mqtt->topicNameAndMessage[i];
    }

    topicname[i+1] = 0;

    if (tcp->check != checksum ) //& strcmp(topicname,str2)
    {
        checksum = tcp->check;
        i = 0;

        for (i=0;i<10;i++)
            str[i]=0;

        uint8_t j =0;

        for (i=htons(mqtt->topicLength);i<mqtt->msgLength-2;i++)
        {
        str[j++] = mqtt->topicNameAndMessage[i];
        }
        str[j] = 0;
        putsUart0(str);
        putsUart0("\n\r");

        payload = mqtt->msgLength+2;
    }

}

bool isEtherMqttPingResponse(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    bool ok;
    ok = ( mqtt->control == 0xd0);
    if(ok){payload=2;}
    return ok;
}

bool isEtherUnSubACK(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttFrame* mqtt = (mqttFrame*)&tcp->data;

    bool ok;
    ok = ( mqtt->control == 0xb0);
    if(ok){payload=4;}
    return ok;
}

void sendPingRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttPublishFrame* mqtt = (mqttPublishFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version

    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->sourcePort = htons(portNum);

    tcp->destPort = tcp->destPort;

    tcp->ackNum = tcp->ackNum;

    tcp->seqNum = tcp->seqNum;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options

    uint8_t res = 0;

    uint16_t flags = 0x0018; // PSH ACK

    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;

    tcp->data=0;

    tcp->check=0;

    mqtt->control = 0xc0;
    mqtt->msgLength = 0;

    // Total length
    ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + 2); // 20 IP header + 0 TCP options + 19 MQTT

    // 32-bit sum over ip header
     sum = 0;
     etherSumWords(&ip->revSize, 10);
     etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
     ip->headerChecksum = getEtherChecksum();

    // calculate TCP Checksum

    uint16_t tcpLength = htons(20+2);

    // 32-bit sum over pseudo-header
   sum = 0;

    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);
    etherSumWords(tcp, 20+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + 2);

}

void initEeprom()
{
    SYSCTL_RCGCEEPROM_R = 1;
    while (EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING);
}

void writeEeprom(uint16_t add, uint32_t eedata)
{
    EEPROM_EEBLOCK_R = add >> 4;
    EEPROM_EEOFFSET_R = add & 0xF;
    EEPROM_EERDWR_R = eedata;
    while (EEPROM_EEDONE_R & EEPROM_EEDONE_WORKING);
}

uint32_t readEeprom(uint16_t add)
{
    EEPROM_EEBLOCK_R = add >> 4;
    EEPROM_EEOFFSET_R = add & 0xF;
    return EEPROM_EERDWR_R;
}

void UnSubscribeRequest(uint8_t packet[])
{
    etherFrame* ether = (etherFrame*)packet;
    ipFrame* ip = (ipFrame*)&ether->data;
    tcpFrame* tcp = (tcpFrame*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttUnSubReqFrame* mqtt = (mqttUnSubReqFrame*)&tcp->data;

    // MAC address of the red board
    ether->sourceAddress[0] = 0x02;
    ether->sourceAddress[1] = 0x03;
    ether->sourceAddress[2] = 0x04;
    ether->sourceAddress[3] = 0x05;
    ether->sourceAddress[4] = 0x06;
    ether->sourceAddress[5] = 0x07;

    // MAC address of the Linux PC
    ether->destAddress[0] = 0x1c;
    ether->destAddress[1] = 0x69;
    ether->destAddress[2] = 0x7a;
    ether->destAddress[3] = 0x07;
    ether->destAddress[4] = 0x94;
    ether->destAddress[5] = 0xe3;

    // Frame Type is IP
    ether->frameType = htons(0x0800);

    //Version
    ip->revSize = 0x45;

    // Services Field
    ip->typeOfService = 0x00;

    // IP address of the source
    ip->sourceIp[0] = ipAddress[0];
    ip->sourceIp[1] = ipAddress[1];
    ip->sourceIp[2] = ipAddress[2];
    ip->sourceIp[3] = ipAddress[3];

    // IP address of the destination
    ip->destIp[0] = 192;
    ip->destIp[1] = 168;
    ip->destIp[2] = 10;
    ip->destIp[3] = 2;

    ip->id = 0x0000;

    ip->flagsAndOffset = htons(0x4000);

    ip->ttl = 0x80; // or 128 in decimal

    ip->protocol = 0x06; //tcp

    tcp->sourcePort = htons(portNum);

    tcp->destPort = htons(1883);

    tcp->ackNum = tcp->ackNum;
    tcp->seqNum = tcp->seqNum;

    uint8_t dataOff =  (20+0)/4; // 20 Bytes header + 0 Bytes options
    uint8_t res = 0;
    uint16_t flags = 0x0018; // PSH ACK
    tcp->dataResFlags = htons( dataOff<<12 | (res>>5)<<9 | flags );

    tcp->winSize = htons(1280);

    tcp->urgPointer = 0;
    tcp->data=0;
    tcp->check=0;

    mqtt->control = 0xa2;

    mqtt->msgID = htons(1);

    strcpy(mqtt->topicName,str2);

    mqtt->topicLength = htons(strlen(mqtt->topicName));

    //mqtt->topicName[mqtt->topicLength] = 0; // QoS

    mqtt->msgLength = strlen(str2)+4; //

    // Total length
     ip->length = htons(((ip->revSize & 0xF) * 4) + 20 + mqtt->msgLength+2); // 20 IP header + 0 TCP options + 19 MQTT

     // 32-bit sum over ip header
     sum = 0;
     etherSumWords(&ip->revSize, 10);
     etherSumWords(ip->sourceIp, ((ip->revSize & 0xF) * 4) - 12);
     ip->headerChecksum = getEtherChecksum();

    // calculate TCP Checksum

    uint16_t tcpLength = htons(20+mqtt->msgLength+2);
    // 32-bit sum over pseudo-header
    sum = 0;
    etherSumWords(ip->sourceIp, 8);
    uint16_t tmp16 = ip->protocol;
    sum += (tmp16 & 0xff) << 8;
    etherSumWords(&tcpLength, 2);

    etherSumWords(tcp, 20+mqtt->msgLength+2);

    tcp->check = getEtherChecksum();

    etherPutPacket((uint8_t*)ether, 14 + ((ip->revSize & 0xF) * 4) +  20 + mqtt->msgLength+2);

}
