//============================================================================
// Name        : DisplayNixie.cpp
// Author      : GRA&AFCH
// Version     :
// Copyright   : Free
// Description : Display digits on shields
//============================================================================

#include <iostream>
#include <wiringPi.h>
#include <wiringPiSPI.h>
#include <string.h>
#include <wiringPiI2C.h>
#include <softTone.h>
#include <softPwm.h>
#include <time.h>
#include <math.h>

using namespace std;
#define LEpin 3
#define UP_BUTTON_PIN 1
#define DOWN_BUTTON_PIN 4
#define MODE_BUTTON_PIN 5
#define BUZZER_PIN 0
#define I2CAdress 0x68
#define I2CFlush 0

#define DEBOUNCE_DELAY 150
#define TOTAL_DELAY 17

#define SECOND_REGISTER 0x0
#define MINUTE_REGISTER 0x1
#define HOUR_REGISTER 0x2
#define WEEK_REGISTER 0x3
#define DAY_REGISTER 0x4
#define MONTH_REGISTER 0x5
#define YEAR_REGISTER 0x6

#define RED_LIGHT_PIN 28
#define GREEN_LIGHT_PIN 27
#define BLUE_LIGHT_PIN 29
#define MAX_POWER 100

#define SECOND_IN_SECONDS 1
#define MINUTE_IN_SECONDS 60
#define HOUR_IN_SECONDS 3600

#define UPPER_DOTS_MASK 0x80000000
#define LOWER_DOTS_MASK 0x40000000


#define LEFT_REPR_START 5
#define LEFT_BUFFER_START 0
#define RIGHT_REPR_START 2
#define RIGHT_BUFFER_START 4

#define PI 3.14159265

uint16_t SymbolArray[10]={1, 2, 4, 8, 16, 32, 64, 128, 256, 512};

tm date;
int fileDesc;
double oscillator = 0.0;
bool doFireworks = true;
bool doFadingBlue = false;
bool use24hour = false;
int redLight = 100;
int greenLight = 0;
int blueLight = 0;
int lightCycle = 0;
bool dotState = 0;
int rotator = 0;

int bcdToDec(int val) {
  return ((val / 16  * 10) + (val % 16));
}

int decToBcd(int val) {
  return ((val / 10  * 16) + (val % 10));
}

void initPin(int pin) {
  pinMode(pin, INPUT);
  pullUpDnControl(pin, PUD_UP);

}

uint32_t get32Rep(char * _stringToDisplay, int start) {
  uint32_t var32 = 0;

  var32= (SymbolArray[_stringToDisplay[start]-0x30])<<20;
  var32|=(SymbolArray[_stringToDisplay[start - 1]-0x30])<<10;
  var32|=(SymbolArray[_stringToDisplay[start - 2]-0x30]);
  return var32;
}

void fillBuffer(uint32_t var32, unsigned char * buffer, int start) {
  buffer[start] = var32>>24;
  buffer[start + 1] = var32>>16;
  buffer[start + 2] = var32>>8;
  buffer[start + 3] = var32;
}

void dotBlink()
{
  static unsigned int lastTimeBlink=millis();

  if ((millis() - lastTimeBlink) > 1000)
  {
    lastTimeBlink=millis();
    dotState = !dotState;
  }
}

void fadingBlue() {
  blueLight = int(abs(sin(oscillator*PI/180)*100));
  oscillator += 0.5;
  softPwmWrite(BLUE_LIGHT_PIN, blueLight);
}

void rotateFireWorks() {
  int fireworks[] = {0,0,1,
    -1,0,0,
    0,1,0,
    0,0,-1,
    1,0,0,
    0,-1,0
  };
  redLight += fireworks[rotator * 3];
  greenLight += fireworks[rotator * 3 + 1];
  blueLight += fireworks[rotator * 3 + 2];
  softPwmWrite(RED_LIGHT_PIN, redLight);
  softPwmWrite(GREEN_LIGHT_PIN, greenLight);
  softPwmWrite(BLUE_LIGHT_PIN, blueLight);
  lightCycle = lightCycle + 1;
  if (lightCycle == MAX_POWER) {
    rotator = rotator + 1;
    lightCycle  = 0;
  }
  if (rotator > 5)
    rotator = 0;
}

uint32_t addBlinkTo32Rep(uint32_t var) {
  if (dotState)
  {
    var &=~LOWER_DOTS_MASK;
    var &=~UPPER_DOTS_MASK;
  }
  else
  {
    var |=LOWER_DOTS_MASK;
    var |=UPPER_DOTS_MASK;
  }
  return var;
}


int main(int argc, char* argv[]) {
  int curr_arg = 1;
  while (curr_arg < argc)
  {
    if (!strcmp(argv[curr_arg],"24hour"))
      use24hour = true;
    else if (!strcmp(argv[curr_arg],"fireworks"))
    {
      doFireworks = true;
      doFadingBlue = false;
    }
    else if (!strcmp(argv[curr_arg],"fadingBlue"))
    {
      doFireworks = false;
      doFadingBlue = true;
    }
    else
    {
      printf("ERROR: %s Unknown argument, \"%s\" on command line.\n\n", argv[0], argv[1]);
      printf("USAGE: %s            -- Use system clock in 12-hour mode.\n", argv[0]);
      printf("       %s 24hour     -- use 24-hour mode.\n", argv[0]);
      printf("       %s fireworks  -- enable fireworks lighting.\n", argv[0]);
      printf("       %s fadingBlue -- enable fading blue lighting.\n", argv[0]);
      puts("\nNOTE:  Any combination/order of above arguments is allowed.\n");
      exit(10);
    }
    curr_arg += 1;
  }
  wiringPiSetup();
  //softToneCreate (BUZZER_PIN);
  //softToneWrite(BUZZER_PIN, 1000);
  softPwmCreate(RED_LIGHT_PIN, redLight, MAX_POWER);
  softPwmCreate(GREEN_LIGHT_PIN, greenLight, MAX_POWER);
  softPwmCreate(BLUE_LIGHT_PIN, blueLight, MAX_POWER);
  initPin(UP_BUTTON_PIN);
  initPin(DOWN_BUTTON_PIN);
  initPin(MODE_BUTTON_PIN);
  fileDesc = wiringPiI2CSetup(I2CAdress);
  if (wiringPiSPISetupMode (0, 2000000, 2)) printf("SPI ok");
  else {printf("SPI NOT ok"); return 0;}
  long hourDelay = millis();
  long minuteDelay = hourDelay;
  do {

    // variables to store date and time components
    int hours, minutes, seconds, day, month, year;

    // time_t is arithmetic time type
    time_t now;

    // Obtain current time
    // time() returns the current time of the system as a time_t value
    time(&now);

    // localtime converts a time_t value to calendar time and
    // returns a pointer to a tm structure with its members
    // filled with the corresponding values
    struct tm *local = localtime(&now);

    hours = local->tm_hour;      	// get hours since midnight (0-23)
    minutes = local->tm_min;     	// get minutes passed after the hour (0-59)
    seconds = local->tm_sec;     	// get seconds passed after minute (0-59)

    day = local->tm_mday;        	// get day of month (1 to 31)
    month = local->tm_mon + 1;   	// get month of year (0 to 11)
    year = local->tm_year + 1900;	// get year since 1900

    char _stringToDisplay[8];

    // print local time
    if (hours < 12 || use24hour)	// before midday
      sprintf( _stringToDisplay, "%02d%02d%02d", hours, minutes, seconds);

    else	// after midday
      sprintf( _stringToDisplay, "%02d%02d%02d", hours - 12, minutes, seconds);

    pinMode(LEpin, OUTPUT);

    dotBlink();

    unsigned char buff[8];

    uint32_t var32 = get32Rep(_stringToDisplay, LEFT_REPR_START);
    var32 = addBlinkTo32Rep(var32);
    fillBuffer(var32, buff , LEFT_BUFFER_START);

    var32 = get32Rep(_stringToDisplay, RIGHT_REPR_START);
    var32 = addBlinkTo32Rep(var32);
    fillBuffer(var32, buff , RIGHT_BUFFER_START);


    //printf("%i,%i,%i\n", redLight, greenLight, blueLight);
    if (doFireworks)
      rotateFireWorks();
    else if (doFadingBlue)
      fadingBlue();
    else
    {
      redLight = 0;
      greenLight = 0;
      blueLight = 100;
    }
    softPwmWrite(RED_LIGHT_PIN, redLight);
    softPwmWrite(GREEN_LIGHT_PIN, greenLight);
    softPwmWrite(BLUE_LIGHT_PIN, blueLight);
    digitalWrite(LEpin, LOW);
    wiringPiSPIDataRW(0, buff, 8);
    digitalWrite(LEpin, HIGH);
    delay (TOTAL_DELAY);
  }
  while (true);
  return 0;
}
