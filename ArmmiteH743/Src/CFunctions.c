/*-*****************************************************************************
MMBasic for STM32H743 [ZI2 and VIT6] (Armmite H7)

CFunctions.c
Supports CFunctions

Copyright 2011-2023 Geoff Graham and  Peter Mather.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holders nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

4. The name MMBasic be used when referring to the interpreter in any
   documentation and promotional material and the original copyright message
  be displayed  on the console at startup (additional copyright messages may
   be added).

5. All advertising materials mentioning features or use of this software must
   display the following acknowledgement: This product includes software
   developed by Geoff Graham and Peter Mather.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/
#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
//Vector to CFunction static RAM
#define CFUNCRAM_SIZE   256
int CFuncRam[CFUNCRAM_SIZE/sizeof(int)];

//Vector to CFunction routine called every mSec
unsigned int CFuncmSec = (unsigned int)NULL;

extern volatile uint64_t uSecTimer;
extern volatile uint64_t FastTimer;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim16;
extern uint32_t ticks_per_microsecond;
void CallExecuteProgram(char *p);
MMFLOAT IntToFloat(long long int a){ return a; }
void CallCFuncmSec(void);
extern void routinechecksExternal(void);

//Vector to CFunction routine called every command (ie, from the BASIC interrupt checker)
unsigned int CFuncInt1 = (unsigned int)NULL;

//Vector to CFunction routine called by the interrupt 2 handler
unsigned int CFuncInt2 = (unsigned int)NULL;

//Vector to CFunction routine called by the interrupt 3 handler
unsigned int CFuncInt3 = (unsigned int)NULL;

//Vector to CFunction routine called by the interrupt 4 handler
unsigned int CFuncInt4 = (unsigned int)NULL;

//Vector to CSubComplete routine
extern volatile int CSubComplete;

//static uint64_t timer(void){ return (__HAL_TIM_GET_COUNTER(&htim2));}
static uint64_t timer(void){ return (__HAL_TIM_GET_COUNTER(&htim16) + (uSecTimer*50000));}   //same as cmm2
const void * const CallTable[] __attribute__((section(".cspace")))  = {	(void *)uSec,	//0x00
																		(void *)putConsole,	//0x04
																		(void *)getConsole,	//0x08
																		(void *)ExtCfg,	//0x0c
																		(void *)ExtSet,	//0x10
																		(void *)ExtInp,	//0x14
																		(void *)PinSetBit,	//0x18
																		(void *)PinRead,	//0x1c
																		(void *)MMPrintString,	//0x20
																		(void *)IntToStr,	//0x24
																		(void *)CheckAbort,	//0x28
																		(void *)GetMemory,	//0x2c
																		(void *)GetTempMemory,	//0x30
																		(void *)FreeMemory, //0x34
																		(void *)&DrawRectangle,	//0x38
																		(void *)&DrawBitmap,	//0x3c
																		(void *)DrawLine,	//0x40
																		(void *)FontTable,	//0x44
																		(void *)&ExtCurrentConfig,	//0x48
																		(void *)&HRes,	//0x4C
																		(void *)&VRes,	//0x50
																		(void *)SoftReset, //0x54
																		(void *)error,	//0x58
																		(void *)&ProgMemory,	//0x5c
																		(void *)&vartbl, //0x60
																		(void *)&varcnt,  //0x64
																		(void *)&DrawBuffer,	//0x68
																		(void *)&ReadBuffer,	//0x6c
																		(void *)&FloatToStr,	//0x70
                                                                        (void *)CallExecuteProgram, //0x74
																		(void *)&CFuncmSec, //0x78      was CallCFuncmSec,	//0x78
                                                                        (void *)CFuncRam,	//0x7c
                                                                        (void *)&ScrollLCD,	//0x80
																		(void *)IntToFloat, //0x84
																		(void *)FloatToInt64,	//0x88
																		(void *)&Option,	//0x8c
                                                                        (void *)&CFuncInt1,	//0x90
                                                                        (void *)&CFuncInt2,	//0x94
																		(void *)&CFuncInt3,	//0x98
                                                                        (void *)&CFuncInt4,	//0x9c
																		(void *)sin,	//0xa0
																		(void *)DrawCircle,	//0xa4
																		(void *)DrawTriangle,	//0xa8
																		(void *)timer,	//0xac
																		(void *)&CSubComplete	//0xb0
								   	   	   	   	   	   	   	   	   	   };

// used by CallCFunction() below to find a CFunction or CSub in program flash or the library
unsigned int *FindCFunction(unsigned int *p, char *CmdPtr) {
    while(*p != 0xffffffff) {
        if(*p++ == (unsigned int)CmdPtr) return p;
        p += (*p + 4) / sizeof(unsigned int);
    }
    return p;
}

long long int CallCFunction(char *CmdPtr, char *ArgList, char *DefP, char *CallersLinePtr) {
    void *arg[10] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    int typ[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    long long int ret, i64[10];
    MMFLOAT ff[10];
    char *pp;
    int i,type;
    uint32_t ii;
    unsigned int *p=(void * const )CallTable;
    MMFLOAT ftmp;
    if((uint32_t)p > 0x10000000)error("Internal error");
    // find the C code in flash
    if(*CmdPtr == cmdCFUN && *ArgList != '(') error("Syntax");      // check that a CFunction has an opening bracket
    if(*ArgList == '(') ArgList++;                                  // and step over it
    p = FindCFunction((unsigned int *)CFunctionFlash, CmdPtr);      // search through the program flash looking for a match to the function being called
   // if(*p == 0xffffffff) error("Internal fault 5(sorry)");
    if(*p == 0xffffffff && CFunctionLibrary != NULL)
              p = FindCFunction((unsigned int *)CFunctionLibrary, CmdPtr);// if unsuccessful search the library area
    if(*p == 0xffffffff) error("Internal fault 5(sorry)");

    // next, get the argument types (if specified)
    {  // first copy the type list to a buffer and trim the following closing bracket (if there)
        char buf[MAXSTRLEN];
        char *p = buf;
        if(*DefP == '(') DefP++;
        while(*DefP && *DefP != ')' && *DefP != '\'') *p++ = *DefP++;
        *p = 0;
        p = buf;
        skipspace(p);
        CheckIfTypeSpecified(p, &i, true);
        if(i != DefaultType) {
            // if there is a type list get each entry
            getargs(&p, 19, ",");
            for(i = 0; i < argc; i+=2) {                            // get each definition
                CheckIfTypeSpecified(argv[i], &typ[i/2], false);
                typ[i/2] &= ~T_IMPLIED;
            }
        }
    }

    // we have found the CFunction or CSub and the types on its command line
    CurrentLinePtr = CallersLinePtr;                                // report errors at the caller
    if(*ArgList != ')') {
        getargs(&ArgList, 19, ",");                                 // expand the command line of the caller
       // if(argc % 2 == 0) error("Syntax");                       // ALLOW no parameter on CSUB
        for(i = 0; i < argc; i += 2) {
            // if this is a straight variable we want to pass a pointer to its value in RAM
            if(IsNamestart((uint8_t)*argv[i]) && (*skipvar(argv[i], false) == 0 || *skipvar(argv[i], false) == ')') && !(FindSubFun(argv[i], 1) >= 0 && strchr(argv[i], '(') != NULL)) {
                arg[i/2] = findvar(argv[i], V_FIND | V_EMPTY_OK /* | V_NOFIND_ERR */ );   // if the argument
                if(typ[i/2] != 0 && !(TypeMask(vartbl[VarIndex].type) & typ[i/2])) error("Incompatible type");
            } else {
                // else it must be an expression of some sort
                // get the value based on the type specified in the definition
                switch(typ[i/2]) {
                    case T_INT: i64[i/2] = getinteger(argv[i]);
                                arg[i/2] = &i64[i/2];
                                break;
                    case T_NBR: ftmp = getnumber(argv[i]);
                                ff[i/2] = ftmp;
                                arg[i/2] = &ff[i/2];
                                break;
                    case T_STR: arg[i/2] = GetTempStrMemory();
                                Mstrcpy(arg[i/2], getstring(argv[i]));
                                break;
                    default:    // the type has not been specified (old style CFunction)
                                type = T_NOTYPE;
                                evaluate(argv[i], &ftmp, &i64[i/2], &pp, &type, false);
                                ff[i/2] = ftmp;
                                if(type & T_NBR) {
                                    arg[i/2] = &ff[i/2];
                                } else if(type & T_INT)
                                    arg[i/2] = &i64[i/2];
                                else {
                                    arg[i/2] = GetTempStrMemory();
                                    Mstrcpy(arg[i/2], pp);
                                }
                                break;
                }
            }
        }
    }
    p++;      // step over the size word

    // run the function in flash
    ii = *p++;
    p = (unsigned int *)((unsigned int) p | 0x1);
    ret = ((long long int (*)(void *, void *, void *, void *, void *, void *, void *, void *, void *, void *)) (p + ii)) (arg[0], arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7], arg[8], arg[9]);              // run the CFunction

    return ret;
}
// If the CFuncmSec vector is set then call the CFunction
void CallCFuncmSec(void){
    typedef void func(void);
    func* f=(func*)(void *)CFuncmSec;
    f();
}

// save the interpreter state if re entering it
void CallExecuteProgram(char *p) {
    char *nextstmtSaved = nextstmt;
    LocalIndex++;
    ExecuteProgram(p);
    nextstmt = nextstmtSaved;
    LocalIndex--;
    TempMemoryIsChanged = true;                                     // signal that temporary memory should be checked
}

// If the CFuncmInt1 vector is set then call the CFunction
void CallCFuncInt1(void){
    typedef void func(void);
    func* f=(func*)(void *)CFuncInt1;
    f();
}

// If the CFuncmInt2 vector is set then call the CFunction
void CallCFuncInt2(void){
    typedef void func(void);
    func* f=(func*)(void *)CFuncInt2;
    f();
}

// If the CFuncmInt3 vector is set then call the CFunction
void CallCFuncInt3(void){
    typedef void func(void);
    func* f=(func*)(void *)CFuncInt3;
    f();
}

// If the CFuncmInt4 vector is set then call the CFunction
void CallCFuncInt4(void){
    typedef void func(void);
    func* f=(func*)(void *)CFuncInt4;
    f();
}


