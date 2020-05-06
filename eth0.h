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

#ifndef ETH0_H_
#define ETH0_H_

#include <stdint.h>
#include <stdbool.h>

#define ETHER_UNICAST        0x80
#define ETHER_BROADCAST      0x01
#define ETHER_MULTICAST      0x02
#define ETHER_HASHTABLE      0x04
#define ETHER_MAGICPACKET    0x08
#define ETHER_PATTERNMATCH   0x10
#define ETHER_CHECKCRC       0x20

#define ETHER_HALFDUPLEX     0x00
#define ETHER_FULLDUPLEX     0x100

#define LOBYTE(x) ((x) & 0xFF)
#define HIBYTE(x) (((x) >> 8) & 0xFF)

typedef enum
{
    SynSent,
    SynAckRcvd,
    Established,
    sendAckState,
    publishMQTT,
    subscribeMQTT,
    disconnectReq,
    subAck,
    sendUnsubReq,
    unSubAck,
    FinWait1,
    FinWait2,
    TimeWait,
    closed
} TCPState;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void etherInit(uint16_t mode);
bool etherIsLinkUp();

bool etherIsDataAvailable();
bool etherIsOverflow();
uint16_t etherGetPacket(uint8_t packet[], uint16_t maxSize);
bool etherPutPacket(uint8_t packet[], uint16_t size);

bool etherIsIp(uint8_t packet[]);
bool etherIsIpUnicast(uint8_t packet[]);

bool etherIsPingRequest(uint8_t packet[]);
void etherSendPingResponse(uint8_t packet[]);

bool etherIsArpRequest(uint8_t packet[]);
void etherSendArpResponse(uint8_t packet[]);
void etherSendArpRequest(uint8_t packet[], uint8_t ip[]);

bool etherIsUdp(uint8_t packet[]);
uint8_t* etherGetUdpData(uint8_t packet[]);
void etherSendUdpResponse(uint8_t packet[], uint8_t* udpData, uint8_t udpSize);

void etherEnableDhcpMode();
void etherDisableDhcpMode();
bool etherIsDhcpEnabled();
bool etherIsIpValid();
void etherSetIpAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetIpAddress(uint8_t ip[4]);
void etherSetIpGatewayAddress(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
void etherGetIpGatewayAddress(uint8_t ip[4]);
void etherSetIpSubnetMask(uint8_t mask0, uint8_t mask1, uint8_t mask2, uint8_t mask3);
void etherGetIpSubnetMask(uint8_t mask[4]);
void etherSetMacAddress(uint8_t mac0, uint8_t mac1, uint8_t mac2, uint8_t mac3, uint8_t mac4, uint8_t mac5);
void etherGetMacAddress(uint8_t mac[6]);

bool etherIsTcp(uint8_t packet[]);
bool isEtherSYNACK(uint8_t packet[]);
bool isEtherConnectACK(uint8_t packet[]);
bool isEtherACK(uint8_t packet[]);
bool isEtherFINACK(uint8_t packet[]);
bool isEtherSubACK(uint8_t packet[]);
bool isEtherPushACK(uint8_t packet[]);
bool isEtherMqttPublish(uint8_t packet[]);
bool isEtherMqttPingResponse(uint8_t packet[]);
bool isEtherUnSubACK(uint8_t packet[]);

void sendSyn(uint8_t packet[]);
void sendAck(uint8_t packet[]);
void sendConnectCmd(uint8_t packet[]);
void publishMqttMessage(uint8_t packet[]);
void disconnectRequest(uint8_t packet[]);
void subscribeRequest(uint8_t packet[]);
void getMqttMessage(uint8_t packet[]);
void sendPingRequest(uint8_t packet[]);
void initEeprom();
void writeEeprom(uint16_t add, uint32_t eedata);
uint32_t readEeprom(uint16_t add);
void displayConnectionInfo();
void UnSubscribeRequest(uint8_t packet[]);

uint16_t htons(uint16_t value);
#define ntohs htons

uint32_t htons32(uint32_t value);
#define ntohs32 htons32

#endif
