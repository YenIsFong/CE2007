// RSLK Self Test via UART

/* This example accompanies the books
   "Embedded Systems: Introduction to the MSP432 Microcontroller",
       ISBN: 978-1512185676, Jonathan Valvano, copyright (c) 2017
   "Embedded Systems: Real-Time Interfacing to the MSP432 Microcontroller",
       ISBN: 978-1514676585, Jonathan Valvano, copyright (c) 2017
   "Embedded Systems: Real-Time Operating Systems for ARM Cortex-M Microcontrollers",
       ISBN: 978-1466468863, , Jonathan Valvano, copyright (c) 2017
 For more information about my classes, my research, and my books, see
 http://users.ece.utexas.edu/~valvano/

Simplified BSD License (FreeBSD License)
Copyright (c) 2017, Jonathan Valvano, All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are
those of the authors and should not be interpreted as representing official
policies, either expressed or implied, of the FreeBSD Project.
*/

#include "msp.h"
#include <stdint.h>
#include <string.h>
#include "..\inc\UART0.h"
#include "..\inc\EUSCIA0.h"
#include "..\inc\FIFO0.h"
#include "..\inc\Clock.h"
//#include "..\inc\SysTick.h"
#include "..\inc\SysTickInts.h"
#include "..\inc\CortexM.h"
#include "..\inc\TimerA1.h"
#include "..\inc\Bump.h"
#include "..\inc\BumpInt.h"
#include "..\inc\LaunchPad.h"
#include "..\inc\Motor.h"
#include "../inc/IRDistance.h"
#include "../inc/ADC14.h"
#include "../inc/LPF.h"
#include "..\inc\Reflectance.h"
#include "../inc/TA3InputCapture.h"
#include "../inc/Tachometer.h"

#define P2_4 (*((volatile uint8_t *)(0x42098070)))
#define P2_3 (*((volatile uint8_t *)(0x4209806C)))
#define P2_2 (*((volatile uint8_t *)(0x42098068)))
#define P2_1 (*((volatile uint8_t *)(0x42098064)))
#define P2_0 (*((volatile uint8_t *)(0x42098060)))
volatile uint32_t ADCvalue;
volatile uint32_t ADCflag;
volatile uint32_t nr,nc,nl;
uint8_t Data; // QTR-8RC
int32_t Position; // 332 is right, and -332 is left of center
uint32_t main_count=0;
volatile uint8_t reflectance_data, bump_data,counter;


void RSLK_Reset(void){
    DisableInterrupts();

    LaunchPad_Init();
    //Initialise modules used e.g. Reflectance Sensor, Bump Switch, Motor, Tachometer etc
    // ... ...

    EnableInterrupts();
}

// RSLK Self-Test
// Sample program of how the text based menu can be designed.
// Only one entry (RSLK_Reset) is coded in the switch case. Fill up with other menu entries required for Lab5 assessment.
// Init function to various peripherals are commented off.  For reference only. Not the complete list.
void TimedPause(uint32_t time){
  Clock_Delay1ms(time);          // run for a while and stop
  Motor_Stop();
  while(LaunchPad_Input()==0);  // wait for touch
  while(LaunchPad_Input());     // wait for release
}


void SensorRead_ISR(void){  // runs at 2000 Hz
  uint32_t raw17,raw12,raw16;
  P1OUT ^= 0x01;         // profile
  P1OUT ^= 0x01;         // profile
  ADC_In17_12_16(&raw17,&raw12,&raw16);  // sample
  nr = LPF_Calc(raw17);  // right is channel 17 P9.0
  nc = LPF_Calc2(raw12);  // center is channel 12, P4.1
  nl = LPF_Calc3(raw16);  // left is channel 16, P9.1////IR SENSORS
  ADCflag = 1;           // semaphore
  P1OUT ^= 0x01;         // profile
}




uint16_t Period0;              // (1/SMCLK) units = 83.3 ns units
uint16_t First0=0;             // Timer A3 first edge, P10.4
uint32_t Done0=0;              // set each rising

uint16_t Period2;              // (1/SMCLK) units = 83.3 ns units
uint16_t First2=0;             // Timer A3 first edge, P8.2
uint32_t Done2=0;              // set each rising

// max period is (2^16-1)*83.3 ns = 5.4612 ms
// min period determined by time to run ISR, which is about 1 us
void PeriodMeasure0(uint16_t time){
  Period0 = (time - First0)&0xFFFF; // 16 bits, 83.3 ns resolution
  First0 = time;                    // setup for next
  Done0++;
}

// max period is (2^16-1)*83.3 ns = 5.4612 ms
// min period determined by time to run ISR, which is about 1 us
void PeriodMeasure2(uint16_t time){
  Period2 = (time - First2)&0xFFFF; // 16 bits, 83.3 ns resolution
  First2 = time;                    // setup for next
  Done2++;
}

void PORT4_IRQHandler(void){
    bump_data=Bump_Read();
    P4->IFG=0x00;//set flag to 0
}





struct State {
  uint32_t out;                // 2-bit output
  uint32_t delay;              // time to delay in 1ms
  const struct State *next[4]; // Next if 2-bit input is 0-3
};
typedef const struct State State_t;

#define Center          &fsm[0]
#define Left1           &fsm[1]
#define Left2           &fsm[2]
#define Left_off1       &fsm[3]
#define Left_off2       &fsm[4]
#define Left_stop       &fsm[5]
#define Right1          &fsm[6]
#define Right2          &fsm[7]
#define Right_off1      &fsm[8]
#define Right_off2      &fsm[9]
#define Right_stop      &fsm[10]

//OHL
State_t fsm[11]={
  {0x03,  500, { Right1,         Left1,      Right1,     Center    }},   // Center 0,1,2,3
  {0x02,  500, { Left_off1,      Left2,      Right1,     Center    }},   // Left1
  {0x03,  500, { Left_off1,      Left1,      Right1,     Center    }},   // Left2
  {0x02, 5000, { Left_off2,      Left_off2,  Left_off2,  Left_off2 }},   // Left_off1
  {0x03, 5000, { Left_stop,      Left1,      Right1,     Center    }},   // Left_off2
  {0x00,  0, { Left_stop,      Left_stop,  Left_stop,  Left_stop }},   // Left_stop
  {0x01,  500, { Right_off1,     Left1,      Right2,     Center    }},   // Right1
  {0x03, 500, { Right_off1,     Left1,      Right1,     Center    }},   // Right2
  {0x01, 5000, { Right_off2,     Right_off2, Right_off2, Right_off2}},   // Right_off1
  {0x03,  5000, { Right_stop,     Left1,      Right1,     Center    }},   // Right_off2
  {0x00,  0, { Right_stop,     Right_stop, Right_stop, Right_stop}}    // Right_stop
};


State_t *Spt;  // pointer to the current state
uint32_t Input;
uint32_t Output;
/*Run FSM continuously
1) Output depends on State (LaunchPad LED)
2) Wait depends on State
3) Input (LaunchPad buttons)
4) Next depends on (Input,State)
 */























 int main(void) {
  uint32_t cmd=0xDEAD, menu=0;

  DisableInterrupts();
  Clock_Init48MHz();  // makes SMCLK=12 MHz
  //SysTick_Init(48000,2);  // set up SysTick for 1000 Hz interrupts
  Motor_Init();
  Motor_Stop();
  LaunchPad_Init();
  Bump_Init();
  //BumpInt_Init();
  //Bumper_Init();
  //IRSensor_Init();
  //Tachometer_Init();
  EUSCIA0_Init();     // initialize UART
  EnableInterrupts();

//////////////////////////////////////////////////////////////////////////////////
  uint32_t raw17,raw12,raw16;
    int32_t n; uint32_t s;
    ADCflag = 0;
    s = 256; // replace with your choice
    ADC0_InitSWTriggerCh17_12_16();   // initialize channels 17,12,16
    ADC_In17_12_16(&raw17,&raw12,&raw16);  // sample
    int left_distance,right_distance,center_distance;
    LPF_Init(raw17,s);     // P9.0/channel 17//right
    LPF_Init2(raw12,s);     // P4.1/channel 12//center
    LPF_Init3(raw16,s);     // P9.1/channel 16//left
    UART0_Init();          // initialize UART0 115,200 baud rate
    TimerA1_Init(&SensorRead_ISR,250);    // 2000 Hz sampling
    EnableInterrupts();
///////////////////////////////////////////////////////////////////////////////////


    TimedPause(1000);
  while(1){                     // Loop forever
      // write this as part of Lab 5
//start of line trace
/*
      Data = Reflectance_Read(1000);
      Position = Reflectance_Position(Data);//intepreted data

      if(Data==0xFF){
          Motor_Stop();
      }

      else if (Position==0){//line at centre go straight
          Motor_Forward(1000,1000);
          Clock_Delay1ms(100);
          Motor_Stop();

      }
      else if(Position >0){//go left
//          Motor_Left(1500,0);
//          Clock_Delay1ms(200);
          Motor_Forward(0,1000);
          Clock_Delay1ms(100);
          Motor_Stop();


      }
      else if(Position<0){//go right
//          Motor_Right(0,1500);
//          Clock_Delay1ms(200);
          Motor_Forward(1000,0);
          Clock_Delay1ms(100);
          Motor_Stop();

      }

*/
// end of line tracing
//start of obstacle detection
/*
      left_distance=LeftConvert(nl);
      right_distance=RightConvert(nr);
      center_distance=CenterConvert(nc);

      if(Bump_Read()!=63){//63 dec is 0x111111
          Motor_Backward(2000,2000);
          Clock_Delay1ms(500);
          Motor_Stop();
          Motor_Left(3000,3000);
          Clock_Delay1ms(500);
          Motor_Stop();
      }
      if (center_distance>150){
          Motor_Forward(2000,2000);
      }
      else{
          Motor_Stop();
          Clock_Delay1ms(500);

          if(left_distance<150&&right_distance<150){
              Motor_Left(3000,3000);
              Clock_Delay1ms(200);
              Motor_Stop();
              Clock_Delay1ms(400);
          }
          else if(left_distance>right_distance){
              Motor_Left(3000,3000);
              Clock_Delay1ms(200);
              Motor_Stop();
              Clock_Delay1ms(400);
          }
          else{
              Motor_Right(3000,3000);
              Clock_Delay1ms(200);
              Motor_Stop();
              Clock_Delay1ms(400);
          }
      }//end else
*/
//end of obstacle detection
//start of FSM



















  }//end while(1)
}

#if 0
//Sample program for using the UART related functions.
int Program5_4(void){
//int main(void){
    // demonstrates features of the EUSCIA0 driver
  char ch;
  char string[20];
  uint32_t n;
  DisableInterrupts();
  Clock_Init48MHz();  // makes SMCLK=12 MHz
  EUSCIA0_Init();     // initialize UART
  EnableInterrupts();
  EUSCIA0_OutString("\nLab 5 Test program for EUSCIA0 driver\n\rEUSCIA0_OutChar examples\n");
  for(ch='A'; ch<='Z'; ch=ch+1){// print the uppercase alphabet
     EUSCIA0_OutChar(ch);
  }
  EUSCIA0_OutChar(LF);
  for(ch='a'; ch<='z'; ch=ch+1){// print the lowercase alphabet
    EUSCIA0_OutChar(ch);
  }
  while(1){
    EUSCIA0_OutString("\n\rInString: ");
    EUSCIA0_InString(string,19); // user enters a string
    EUSCIA0_OutString(" OutString="); EUSCIA0_OutString(string); EUSCIA0_OutChar(LF);

    EUSCIA0_OutString("InUDec: ");   n=EUSCIA0_InUDec();
    EUSCIA0_OutString(" OutUDec=");  EUSCIA0_OutUDec(n); EUSCIA0_OutChar(LF);
    EUSCIA0_OutString(" OutUFix1="); EUSCIA0_OutUFix1(n); EUSCIA0_OutChar(LF);
    EUSCIA0_OutString(" OutUFix2="); EUSCIA0_OutUFix2(n); EUSCIA0_OutChar(LF);

    EUSCIA0_OutString("InUHex: ");   n=EUSCIA0_InUHex();
    EUSCIA0_OutString(" OutUHex=");  EUSCIA0_OutUHex(n); EUSCIA0_OutChar(LF);
  }
}
#endif

