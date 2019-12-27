/*
 * monitor door status switches
 * Log changes to SD card
 * Send changes to nRF24L01 master at node 00
 * Print updates on LCD
 */
 
#include <Wire.h>       // support for LCD backpack and RTC
#include <LiquidTWI2.h> // LCD backpack libraries
#include <TimeLib.h>    // various time data
#include <DS1307RTC.h>  // real time clock hardware
#include <SPI.h>        // SPI for SD card and nrf24l01 interfaces
#include <RF24.h>
#include <RF24Network.h>
#include "SdFat.h"      // file system on an SD card

//#define SERIAL_ON

// Pin numbers used for door switch sense inputs
const int frontDoorPin   = 3;   // the number of the pushbutton pin
const int mudroomDoorPin = A3;  // the number of the pushbutton pin
const int backDoorPin    = A2;  // the number of the pushbutton pin
const int pirPin         = A1;  // the number of the pushbutton pin
const int redLED         = 1;
const int greenLED       = 0;

// buzzer hardware pins
const int buzzerPin = 5;
const int buzzerOnDelay = 50;

// Notes and their frequencies for creating buzzer tones
const int C = 1046;
const int D = 1175;
const int E = 1319;
const int F = 1397;
const int G = 1568;
const int A = 1760;
const int B = 1976;
const int C1 = 2093;
const int D1 = 2349;

// SD chip select pin.  Be sure to disable any other SPI devices such as Enet.
const uint8_t chipSelect = SS;

//==============================================================================
// LCD display object
// Connect via i2c, address 0x20 (A0-A2 not jumpered)
LiquidTWI2 lcd(0x20);

//==============================================================================
// nRF24L01 object
RF24 radio(9,8);                // nRF24L01(+) radio attached using Getting Started board 
RF24Network network(radio);      // Network uses that radio
const uint16_t this_node = 02;   // Address of our node in Octal format ( 04,031, etc)
const uint16_t master_node = 00; // Address of the other node in Octal format

//==============================================================================
SdFat sd;         // File system object.
SdFile logfile;   // Log file object
uint32_t logTime; // Time in micros for next data record.
#define error(msg) sd.errorHalt(F(msg)) // Error messages stored in flash.
// 12345678901234567890123456789012
// YYMMDD,HHMMSS,F,M,B,P,C
const int doorChangeArrayRows = 5;
const int doorChangeArrayCols = 24;
static char doorChangeArray[doorChangeArrayRows][doorChangeArrayCols];

// initallize to max value, index is incremented before use
// Increment from max value should roll over to zero.
static unsigned char doorChangeArrayHead = 255; 
static unsigned char doorChangeArrayTail = 0;
//==============================================================================
//------------------------------------------------------------------------------
// Log file base name.  Must be four characters or less.
//------------------------------------------------------------------------------
#define FILE_BASE_NAME "DOOR"
const uint8_t BASE_NAME_SIZE = sizeof(FILE_BASE_NAME) - 1;
static char fileName[13] = FILE_BASE_NAME "0000.csv";

//------------------------------------------------------------------------------
// Find and open new unused file.
//------------------------------------------------------------------------------
void openNewFile() 
{
  // Find an unused file name.
  if (BASE_NAME_SIZE > 4) 
  {
    error("FILE_BASE_NAME too long");
  }
  
  while (sd.exists(fileName)) 
  {
    if (fileName[BASE_NAME_SIZE + 3] != '9')  // check 1's digit, if not 9 then increment and continue
    {
      fileName[BASE_NAME_SIZE + 3]++;
    } 
    else if (fileName[BASE_NAME_SIZE + 2] != '9') // if 1's was 9 then check 10's digit
    {
      fileName[BASE_NAME_SIZE + 3] = '0';
      fileName[BASE_NAME_SIZE + 2]++;
    } 
    else if (fileName[BASE_NAME_SIZE + 1] != '9') // if 1's and 10's are 9 then check 100's
    {
      fileName[BASE_NAME_SIZE + 3] = '0';
      fileName[BASE_NAME_SIZE + 2] = '0';
      fileName[BASE_NAME_SIZE + 1]++;
    } 
    else if (fileName[BASE_NAME_SIZE] != '9') // if 1000's digit is also 9 then roll over and reuse at 0000
    {
      fileName[BASE_NAME_SIZE + 3] = '0';
      fileName[BASE_NAME_SIZE + 2] = '0';
      fileName[BASE_NAME_SIZE + 1] = '0';
      fileName[BASE_NAME_SIZE    ] = '0';
    } 
    else 
    {
      error("Can't create file name");
    }
  }
  
  if (!logfile.open(fileName, O_WRONLY | O_CREAT | O_EXCL)) 
  {
    error("file.open");
  }
}

//------------------------------------------------------------------------------
// write log file header, typically once per log file on log file create
//------------------------------------------------------------------------------
// Write data header.
void writeHeader() 
{
  logfile.println(F("DATE,TIME,FRONT,MUD,BACK,PIR,CHANGE"));
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
void updateDisplay( int frontDoorState, int mudroomDoorState, int backDoorState, int pirState ) 
{
  tmElements_t tm;
  char timedatebuffer[17];

  // Interval is how long we wait to update display
  static unsigned long displayInterval=1000;

  // Tracks the time since last event fired
  static unsigned long previousDisplayMillis=0;
  
  // Get snapshot of time
  unsigned long currentMillis = millis();

  // Check for update display
  if ((unsigned long)(currentMillis - previousDisplayMillis) >= displayInterval) 
  {
    // Use the snapshot to set track time until next event
    previousDisplayMillis = currentMillis;

    // It's time to do something!
    if (RTC.read(tm)) 
    {
      sprintf(timedatebuffer, "%02d/%02d/%02d FMBP", 
      tm.Month,
      tm.Day,
      tmYearToY2k(tm.Year) );
      
      // set the cursor to column 0, line 1
      lcd.setCursor(0, 0);
      lcd.print(timedatebuffer);

      sprintf(timedatebuffer, "%02d:%02d:%02d %1u%1u%1u%1u", 
      tm.Hour,
      tm.Minute,
      tm.Second,
      frontDoorState,
      mudroomDoorState,
      backDoorState,
      pirState);
      
      // (note: line 1 is the second row, since counting begins with 0):
      lcd.setCursor(0, 1);
      lcd.print(timedatebuffer);
    } 
    else 
    {
      if (RTC.chipPresent()) 
      {
        lcd.print(F("RTC Stopped"));
      } 
      else 
      {
        lcd.print(F("RTC ERROR"));
      }
    }    
  }
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
static unsigned char frontDoorState   = 0; // the current reading from the input pin
static unsigned char mudroomDoorState = 0; // the current reading from the input pin
static unsigned char backDoorState    = 0; // the current reading from the input pin
static unsigned char pirState         = 0; // the current reading from the input pin
//------------------------------------------------------------------------------
int updateDoorSwitchState() 
{
  static unsigned char lastFrontDoorReading = LOW;    // the previous reading from the input pin
  static unsigned long frontDoorChangeMillis = 0;     // time reading when last input change
  static unsigned char lastMudroomDoorReading = LOW;  // the previous reading from the input pin
  static unsigned long mudroomDoorChangeMillis = 0;   // time reading when last input change
  static unsigned char lastBackDoorReading = LOW;     // the previous reading from the input pin
  static unsigned long backDoorChangeMillis = 0;      // time reading when last input change
  static unsigned char lastPirReading = LOW;          // the previous reading from the input pin
  static unsigned long pirChangeMillis = 0;           // time reading when last input change

  unsigned char doorChangeFlag = 0;
  // the door switch reading debounce time
  static unsigned long debounceDelay = 50;

  // Tracks the time since last event fired
  static unsigned long previousDebounceMillis = 0;  // the last time the output pin was toggled

  unsigned char frontDoorReadPin   = digitalRead(frontDoorPin);
  unsigned char mudroomDoorReadPin = digitalRead(mudroomDoorPin);
  unsigned char backDoorReadPin    = digitalRead(backDoorPin);
  unsigned char pirReadPin         = digitalRead(pirPin);
  
  // Get snapshot of time
  unsigned long currentMillis = millis();

  // If the switch changed, due to noise or pressing:
  if (frontDoorReadPin != lastFrontDoorReading) 
  {
    // reset the debouncing timer
    frontDoorChangeMillis = currentMillis;
  }

  // If the switch changed, due to noise or pressing:
  if (mudroomDoorReadPin != lastMudroomDoorReading) 
  {
    // reset the debouncing timer
    mudroomDoorChangeMillis = currentMillis;
  }

  // If the switch changed, due to noise or pressing:
  if (backDoorReadPin != lastBackDoorReading) 
  {
    // reset the debouncing timer
    backDoorChangeMillis = currentMillis;
  }

  // If the switch changed, due to noise or pressing:
  if (pirReadPin != lastPirReading) 
  {
    // reset the debouncing timer
    pirChangeMillis = currentMillis;
  }

  if ((currentMillis - frontDoorChangeMillis) > debounceDelay) 
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    if (frontDoorReadPin != frontDoorState) 
    {
      frontDoorState = frontDoorReadPin;
      doorChangeFlag = 1;
    }
  }

  if ((currentMillis - mudroomDoorChangeMillis) > debounceDelay) 
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    if (mudroomDoorReadPin != mudroomDoorState) 
    {
      mudroomDoorState = mudroomDoorReadPin;
      doorChangeFlag = 1;
    }
  }

  if ((currentMillis - backDoorChangeMillis) > debounceDelay) 
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    if (backDoorReadPin != backDoorState) 
    {
      backDoorState = backDoorReadPin;
      doorChangeFlag = 1;
    }
  }

  if ((currentMillis - pirChangeMillis) > debounceDelay) 
  {
    // whatever the reading is at, it's been there for longer than the debounce
    // delay, so take it as the actual current state:
    if (pirReadPin != pirState) 
    {
      pirState = pirReadPin;
      doorChangeFlag = 1;
    }
  }

  lastFrontDoorReading   = frontDoorReadPin;
  lastMudroomDoorReading = mudroomDoorReadPin;
  lastBackDoorReading    = backDoorReadPin;
  lastPirReading         = pirReadPin;

  return( doorChangeFlag );
}

//------------------------------------------------------------------------------
// Interval between data records in milliseconds.
// The interval must be greater than the maximum SD write latency plus the
// time to acquire and write data to the SD to avoid overrun errors.
// Run the bench example to check the quality of your SD card.
const uint32_t SAMPLE_INTERVAL_MS = 1000;

//------------------------------------------------------------------------------
// Log a data record to SD card
//------------------------------------------------------------------------------
void logData( int doorChange ) 
{
  tmElements_t tm;
  char databuffer[10];

  // increment array index to next storage slot and check array bounds
  // if index is greater than max index then reset index to first array element (zero)
  // max index is array size minus 1 since index starts count at zero
  // could write as index greater than or equal to max index
  // if( doorChangeArrayHead >= doorChangeArrayRows )
  
  if( (++doorChangeArrayHead) > (doorChangeArrayRows-1) ) 
  {
    doorChangeArrayHead = 0;
  }

  // clear old data for new data
  memset( doorChangeArray[doorChangeArrayHead], 0, sizeof( doorChangeArray[0] ) );
  
  if (RTC.read(tm)) 
  {
    sprintf(databuffer, "%02d%02d%02d,", 
    tmYearToY2k(tm.Year),
    tm.Month,
    tm.Day );
    logfile.write( databuffer );
    memcpy( &doorChangeArray[doorChangeArrayHead][0], databuffer, 7 );
    
    sprintf(databuffer, "%02d%02d%02d,", 
    tm.Hour,
    tm.Minute,
    tm.Second );
    logfile.write( databuffer );
    memcpy( &doorChangeArray[doorChangeArrayHead][7], databuffer, 7 );
  } 
  else 
  {
    if (RTC.chipPresent()) 
    {
      sprintf(databuffer, "RTCSTP,");
      logfile.write( databuffer );
      logfile.write( databuffer );
      memcpy( &doorChangeArray[doorChangeArrayHead][0], databuffer, 7 );
      memcpy( &doorChangeArray[doorChangeArrayHead][7], databuffer, 7 );
    } 
    else 
    {
      sprintf(databuffer, "RTCERR,");
      logfile.write( databuffer );
      logfile.write( databuffer );
      memcpy( &doorChangeArray[doorChangeArrayHead][0], databuffer, 7 );
      memcpy( &doorChangeArray[doorChangeArrayHead][7], databuffer, 7 );
    }
  }    

  sprintf(databuffer, "%1d,%1d,%1d,%1d,%1d", 
  frontDoorState, mudroomDoorState, backDoorState, pirState, doorChange );
  logfile.write(databuffer);
  logfile.println();
  memcpy( &doorChangeArray[doorChangeArrayHead][14], databuffer, 9 );
}

//==============================================================================
//------------------------------------------------------------------------------
void setup() 
{
#ifdef SERIAL_ON  
  Serial.begin(9600);
#endif
  pinMode(frontDoorPin, INPUT_PULLUP);
  pinMode(mudroomDoorPin, INPUT_PULLUP);
  pinMode(backDoorPin, INPUT_PULLUP);
  pinMode(pirPin, INPUT);

#ifndef SERIAL_ON  
  pinMode(redLED, OUTPUT);
  pinMode(greenLED, OUTPUT);
#endif

  // setup buzzer pins
  // Set the buzzer pin as an OUTPUT, set ground side of buzzer to low
  pinMode(buzzerPin, OUTPUT);
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);

  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);

  // set the LCD type
  //lcd.setMCPType(LTI_TYPE_MCP23017); 
  lcd.setMCPType(LTI_TYPE_MCP23008); 
  // set up the LCD's number of rows and columns:
  lcd.begin(16, 2);
  lcd.setBacklight(HIGH);

  
#ifdef SERIAL_ON  
  // Wait for USB Serial 
  //while (!Serial) SysCall::yield(); delay(1000);
  //Serial.println(F("Type any character to start"));
  //while (!Serial.available()) SysCall::yield();
#endif

  // Initialize at the highest speed supported by the board that is
  // not over 50 MHz. Try a lower speed if SPI errors occur.
  if (!sd.begin(chipSelect, SD_SCK_MHZ(50))) 
  {
    sd.initErrorHalt();
  }

  openNewFile();
  
#ifdef SERIAL_ON  
  // Read any Serial data.
  //do {delay(10); } while (Serial.available() && Serial.read() >= 0);
  Serial.print(F("Logging to: "));
  Serial.println(fileName);
  Serial.println(F("Type any character to stop"));
#endif

  // Write data header.
  writeHeader();

  //SPI.begin();
  radio.begin();
  network.begin(/*channel*/ 90, /*node address*/ this_node);

  // Start on a multiple of the sample interval.
  logTime = micros()/(1000UL*SAMPLE_INTERVAL_MS) + 1;
  logTime *= 1000UL*SAMPLE_INTERVAL_MS;

  memset( doorChangeArray, 0, doorChangeArrayRows * doorChangeArrayCols );
}
//------------------------------------------------------------------------------
void loop() 
{
  network.update(); // pump network
  int doorChange = updateDoorSwitchState();
  updateDisplay(frontDoorState, mudroomDoorState, backDoorState, pirState);
#ifndef SERIAL_ON
  if( frontDoorState || mudroomDoorState || backDoorState || pirState )
  {
    digitalWrite( redLED, HIGH );
    digitalWrite( greenLED, LOW );
  }
  else
  {
    digitalWrite( redLED, LOW );
    digitalWrite( greenLED, HIGH );
  }
#endif


  if( 1 == doorChange )
  {
    logData( doorChange );
    RF24NetworkHeader header(/*to node*/ master_node);
    header.type = 'C'; // change notification
    bool ok = network.write(header,doorChangeArray[doorChangeArrayHead],sizeof(doorChangeArray[0]));
#ifdef SERIAL_ON  
    if (ok) Serial.println("X:ok.");
    else Serial.println("X:failed.");
#endif    
  }

  // Force data to SD and update the directory entry to avoid data loss.
  if (!logfile.sync() || logfile.getWriteError()) 
  {
    error("sd write error");
  }

  network.update(); // pump network

  while ( network.available() ) 
  {     // Is there anything ready for us?
    RF24NetworkHeader header;        // If so, grab it and print it out
    network.read(header,0,0);
#ifdef SERIAL_ON  
    Serial.println((char)header.type);
#endif    
    if(header.type == 'D')
    {
      header.to_node = master_node;
      header.from_node = this_node;
      header.type = 'D';
      for( int j = 0; j < doorChangeArrayRows; j++ )
      {
        bool ok = network.write(header,doorChangeArray[j],sizeof(doorChangeArray[0]));
#ifdef SERIAL_ON  
        Serial.println( doorChangeArray[ j ] );
        if (ok) Serial.println("D:ok.");
        else Serial.println("D:failed.");
#endif        
      }
     }
     else
     {
        if(header.type == 'S')
        {
          header.to_node = master_node;
          header.from_node = this_node;
          header.type = 'S';
          bool ok = network.write(header,doorChangeArray[doorChangeArrayHead],sizeof(doorChangeArray[0]));
#ifdef SERIAL_ON  
          if (ok) Serial.println("S:ok.");
          else Serial.println("S:failed.");
#endif          
        }
     }
  }

#ifdef SERIAL_ON  
  // Commands via Serial Console
  if ( Serial.available() )
  {
    char c = toupper(Serial.read());
    if ( c == 'D' )
    {      
      Serial.println(F("Last N door change events:"));
      for( int j = 0; j < doorChangeArrayRows; j++ )
      {
        Serial.println( doorChangeArray[ j ] );
      }
    }
    else
    {
      if ( c == 'Q' )
      {
        Serial.println(F("*** Exiting Application"));      
        // Close file and stop.
        logfile.close();
        Serial.println(F("Done"));
        SysCall::halt();
      }
    }
  }
 #endif
}
