// Ethernet Example
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

// Pinning for IoT projects with wireless modules:
// N24L01+ RF transceiver
//   MOSI (SSI0Tx) on PA5
//   MISO (SSI0Rx) on PA4
//   SCLK (SSI0Clk) on PA2
//   ~CS on PE0
//   INT on PB2
// Xbee module
//   DIN (UART1TX) on PC5
//   DOUT (UART1RX) on PC4

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "tm4c123gh6pm.h"
#include "eth0.h"
#include "gpio.h"
#include "spi0.h"
#include "uart0.h"
#include "wait.h"
#include "shell.h"

// Pins
#define RED_LED PORTF,1
#define BLUE_LED PORTF,2
#define GREEN_LED PORTF,3
#define PUSH_BUTTON PORTF,4

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------
uint8_t publishFlag = 0;





//-----------------------------------------------------------------------------
// Subroutines                
//-----------------------------------------------------------------------------

// Initialize Hardware
void initHw()
{
	// Configure HW to work with 16 MHz XTAL, PLL enabled, system clock of 40 MHz
    SYSCTL_RCC_R = SYSCTL_RCC_XTAL_16MHZ | SYSCTL_RCC_OSCSRC_MAIN | SYSCTL_RCC_USESYSDIV | (4 << SYSCTL_RCC_SYSDIV_S);

    // Enable clocks
    enablePort(PORTF);
    _delay_cycles(3);

    // Configure LED and pushbutton pins
    selectPinPushPullOutput(RED_LED);
    selectPinPushPullOutput(GREEN_LED);
    selectPinPushPullOutput(BLUE_LED);
    selectPinDigitalInput(PUSH_BUTTON);
}

void displayConnectionInfo()
{
    uint8_t i;
    char str[10];
    uint8_t mac[6];
    uint8_t ip[4];
    putcUart0('\r');
   putcUart0('\n');
    etherGetMacAddress(mac);

    putsUart0("HW: ");
    for (i = 0; i < 6; i++)
    {
        sprintf(str, "%02x", mac[i]);
        putsUart0(str);
        if (i < 6-1)
            putcUart0(':');
    }
    putcUart0('\r');
     putcUart0('\n');
    etherGetIpAddress(ip);
    putsUart0("IP: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    if (etherIsDhcpEnabled())
        putsUart0(" (dhcp)");
    else
        putsUart0(" (static)");
    putcUart0('\r');
     putcUart0('\n');
    etherGetIpSubnetMask(ip);
    putsUart0("SN: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    putcUart0('\r');
     putcUart0('\n');
    etherGetIpGatewayAddress(ip);
    putsUart0("GW: ");
    for (i = 0; i < 4; i++)
    {
        sprintf(str, "%u", ip[i]);
        putsUart0(str);
        if (i < 4-1)
            putcUart0('.');
    }
    putcUart0('\r');
     putcUart0('\n');
    if (etherIsLinkUp())
        putsUart0("Link is up\n\r");
    else
        putsUart0("Link is down\n\r");
    putcUart0('\r');
      putcUart0('\n');

}

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

// Max packet is calculated as:
// Ether frame header (18) + Max MTU (1500) + CRC (4)
#define MAX_PACKET_SIZE 1522
TCPState NextState = closed;


int main(void)
{
    uint8_t* udpData;
    uint8_t data[MAX_PACKET_SIZE];

    // Init controller
    initHw();

    // Setup UART0
    initUart0();
    setUart0BaudRate(115200, 40e6);

    // Init ethernet interface (eth0)
    putsUart0("\n\rStarting eth0\n\r");
    etherSetIpAddress(192, 168, 10, 138);
    etherSetMacAddress(2, 3, 4, 5, 6, 7);

    // Unicast is needed to respond to others MAC
    // Broadcast is needed to repond to "who are you?"
    // HALFDUPLEX gurantees that TX and RX are not done at same time
    etherInit(ETHER_UNICAST | ETHER_BROADCAST | ETHER_HALFDUPLEX);
    // we are using local administration MAC assignment so it could be anything
    // clears a bit in memory to disable DHCP
    etherDisableDhcpMode();
    // needs to be replaced by the number assigned to the group


    etherSetIpSubnetMask(255, 255, 255, 0);
    // this is the address that gets you to the web
    etherSetIpGatewayAddress(192, 168, 10, 1);
    // wait for the chip to settle down
    waitMicrosecond(100000);
    // dump all the settings to ethernet chip
    displayConnectionInfo();

    // Flash LED to make sure everything went well
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(100000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(100000);



//    // Print Welcome message on the console
//    putcUart0(0x0a); putcUart0(0x0d); putcUart0(0x0a); putcUart0(0x0d);
//    putsUart0("Please enter the command");
//    putcUart0(0x0a); putcUart0(0x0d); putsUart0(">>");
//

   // sendSyn(data);


    // Main Loop
    // RTOS and interrupts would greatly improve this code,
    // but the goal here is simplicity
    while (true)
    {

        // Put terminal processing here
        if (kbhitUart0())
        {
              getString();
              // To process the string, calculate the positions of arguments
              posArg(); parseString();
              isCommand();
        }

        // Packet processing
        if (etherIsDataAvailable())
        {
            if (etherIsOverflow())
            {
                setPinValue(RED_LED, 1);
                waitMicrosecond(100000);
                setPinValue(RED_LED, 0);
            }

            // Get packet
            etherGetPacket(data, MAX_PACKET_SIZE);

            // Handle ARP request
            if (etherIsArpRequest(data))
            {
                etherSendArpResponse(data);
            }

         //    Handle IP datagram
            if (etherIsIp(data))
            {
                if (etherIsIpUnicast(data))
                {
                    // handle icmp ping request
                    if (etherIsPingRequest(data))
                    {
                      etherSendPingResponse(data);
                    }
                }
                }
            }

        if(publishFlag)
        {
//            sendSyn(data);
//            NextState = SynSent;


        switch(NextState)
        {
            case closed:
                sendSyn(data);
                NextState = SynSent;
                break;

            case SynSent:

                //putsUart0("\n\rCurrent state: syn sent\n\r");
                if(isEtherSYNACK(data))
                  {

                      NextState = SynAckRcvd;

                  }
                break;

            case SynAckRcvd:
                sendAck(data);

                NextState = Established;
                break;


            case Established:
                putsUart0("\n\rCurrent state: Established\n\r");
                sendConnectCmd(data);
                NextState = publishMQTT;
                break;

            case publishMQTT:
                putsUart0("\n\rCurrent state: Publish MQTT\n\r");
                if(isEtherConnectACK(data))
                  {
                    publishMqttMessage(data);
                    NextState = disconnectReq;
                  }
                break;

            case disconnectReq:
                putsUart0("\n\rCurrent state: disconnect Req\n\r");
                if(isEtherACK(data))
                  {
                    disconnectRequest(data);
                    NextState = FinWait1;
                  }

                break;

            case FinWait1:
                if(isEtherFINACK(data))
                  {
                    NextState = FinWait2;
                  }
                break;

            case FinWait2:
                sendAck(data);
                NextState = TimeWait;
                break;

            case TimeWait:
                waitMicrosecond(100000);
                NextState = closed;
                publishFlag = 0;
                break;



            }
            }



        }
    }


//					// Process UDP datagram
//					// test this with a udp send utility like sendip
//					//   if sender IP (-is) is 192.168.1.198, this will attempt to
//					//   send the udp datagram (-d) to 192.168.1.199, port 1024 (-ud)
//					// sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "on" 192.168.1.199
//                    // sudo sendip -p ipv4 -is 192.168.1.198 -p udp -ud 1024 -d "off" 192.168.1.199
//					if (etherIsUdp(data))
//					{
//						udpData = etherGetUdpData(data);
//						if (strcmp((char*)udpData, "on") == 0)
//			                setPinValue(GREEN_LED, 1);
//                        if (strcmp((char*)udpData, "off") == 0)
//			                setPinValue(GREEN_LED, 0);
//						etherSendUdpResponse(data, (uint8_t*)"Received", 9);
//					}
//

//					}




