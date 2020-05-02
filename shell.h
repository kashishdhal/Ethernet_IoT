#ifndef SHELL_H_

#define SHELL_H_

#include <stdint.h>
#include <stdbool.h>


#define MAX_CHARS 80
#define MAX_FIELDS 6

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void isCommand();
void parseString();
void posArg();
void getString();
void shell();
void getIpFromStr();


#endif
