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
#include "uart0.h"


// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t count=0;
char str[MAX_CHARS+1];
char str1[30];
char str2[30];
char str3[30];
uint8_t pos[MAX_FIELDS];
uint8_t argCount=0;
extern uint8_t publishFlag;
extern uint8_t subscribeFlag;
extern TCPState NextState;
uint8_t clientId[4];

void posArg()
{
    uint8_t i; argCount = 0;

    //empty out the argument position string
    for (i=0;i<5;i++){pos[i]=0;}

    // Here count is a global variable containing length of string
    for (i = 0; i < count; i++)
    {
        if(str[i]==32 | str[i]==44)
        {
            str[i]=0x00;
        }
        if(str[i]!=0x00 & str[i-1]==0x00)
        {
            pos[argCount] = i;
            argCount++;
        }

    }
    return;
}

void parseString()
{
    uint8_t i; uint8_t tempCount=0;

    for(i=0;i<20;i++)
    {
        str1[i]=0; str2[i]=0; str3[i]=0;
    }

    for (i=pos[0];i<20;i++)
    {
        if(str[i]==0){break;}
        else str1[tempCount++] = str[i];
    }

    if(pos[1]!=0)
    {
    tempCount = 0;
    for (i=pos[1];i<count;i++)
    {
        if(str[i]==0){break;}
        else str2[tempCount++] = str[i];
    }
    }

    if(pos[2]!=0)
    {
    tempCount = 0;
    for (i=pos[2];i<count;i++)
    {
        if(str[i]==0){break;}
        else str3[tempCount++] = str[i];
    }
    }

    return;
}


void getString()
{
    char c; count = 0;
        Loop1:
        c = getcUart0();
          if(c == 8 | c == 127)
            {
                if(count>0){count--;goto Loop1;}
                else{goto Loop1;}
            }

          if(c == 10 | c == 13){str[count] = 0x00; return;}
          else
            {
                if(c>=32){str[count++] = c;}
                else goto Loop1;
            }

            if(count == MAX_CHARS)
            {
                str[count] = 0x00;
                putsUart0("You have exceeded the maximum characters, you typed\r\n");
                return;
             }
            else goto Loop1;
}

void getIpFromStr()
{
    char tempStr[5]; uint8_t i; uint8_t j=0; uint8_t k=0;
    for(i=0;i<strlen(str2)+1;i++)
    {
        if(str2[i]==0)
        {
            clientId[j] = atoi(tempStr);
            break;
        }

        if(str2[i]!='.')
            tempStr[k++] = str2[i];
        else
        {
            clientId[j++] = atoi(tempStr);
            for(k=0;k<5;k++)
            {tempStr[k]=0;}
            k = 0;
        }
    }
}

void isCommand()
{
    if(strcmp(str1,"pub")==0)
    {
            publishFlag=1;
            NextState = closed;
            putsUart0("\n\r");
    }

    else if(strcmp(str1,"sub")==0)
    {
            subscribeFlag=1;
            NextState = closed;
            putsUart0("\n\r");
    }

    else if(strcmp(str1,"ifconfig")==0)
        {
            displayConnectionInfo();
        }

    else if(strcmp(str1,"setip")==0)
        {
            getIpFromStr();
            etherSetIpAddress(clientId[0],clientId[1],clientId[2], clientId[3]);
            writeEeprom(1, clientId[0]);
            writeEeprom(2, clientId[1]);
            writeEeprom(3, clientId[2]);
            writeEeprom(4, clientId[3]);
            putsUart0("\n\r");
        }

    else if(strcmp(str1,"unsub")==0)
        {
            NextState = sendUnsubReq;
            putsUart0("\n\r");
        }

    else if(strcmp("reboot", str1)==0 )
    {
        putsUart0("\r\nRebooting.......................");
        NVIC_APINT_R = NVIC_APINT_VECTKEY| NVIC_APINT_SYSRESETREQ;
    }

}

void shell()
{
    getString();
    // To process the string, calculate the positions of arguments
    posArg(); parseString();
    isCommand();
}
