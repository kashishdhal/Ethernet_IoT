#include <eth0.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
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

void isCommand()
{
    if(strcmp(str1,"publish")==0)
    {
            publishFlag=1;
            putsUart0("\n\r");
    }
    else if(strcmp("reboot", str1)==0 )
    {
        putsUart0("\r\nRebooting.......................");
        NVIC_APINT_R = NVIC_APINT_VECTKEY| NVIC_APINT_SYSRESETREQ;
    }

}
