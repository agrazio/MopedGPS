#include <TinyGPS++.h>
#include "SSD1306Ascii.h"
#include "SSD1306AsciiAvrI2c.h"
#include <EEPROM.h>
#include <OneWire.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

// OLED DISPLAY
#define I2C_ADDRESS 0x3C
SSD1306AsciiAvrI2c display;

// ENCODER
#define pinSW 2
#define pinDT 3
#define pinCLK 4

#define DEBOUNCEMS 40 //50
volatile unsigned long lastRot;
volatile unsigned long lastClick;

//SETUP
volatile bool speedToSet;
volatile bool hourToSet;
volatile bool ledToSet;
volatile bool recordToSet;
volatile byte maxSpeed; // POSITION 0 in eeprom
volatile bool hourLegal; // POSITION 1 in eeprom
volatile bool led_onoff; // POSITION 3 in eeprom

// LED
#define LEDPIN 7
#define NUMPIXELS 12
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LEDPIN, NEO_GRB + NEO_KHZ800);
byte speedTresholds[11];

// TEMPERATURA
#define DS18B20_Pin 8
OneWire ds(DS18B20_Pin);
short temperature;

// GPS
TinyGPSPlus gps; // BLUE RX, GREEN TX

// TIMER
volatile unsigned int secondsRaw;
volatile unsigned int temp_secondsRaw;
volatile byte cronoStatus;
volatile unsigned long startCrono;
unsigned int minutes;
unsigned int seconds;

// MENU
volatile byte menu;
volatile bool changedMenu = false;
volatile byte setup_choice;

// GPS
byte kmh, lastkmh, recordKmh, recordalt;

void setup() {
  lastRot = millis();
  lastkmh = 0;

  // READING EEPROM
  maxSpeed = (byte)EEPROM.read(0);
  if (maxSpeed < 10 || maxSpeed > 99) // correzione
    maxSpeed = 99;

  hourLegal = EEPROM.read(1);
  recordKmh = EEPROM.read(2);
  led_onoff = (bool)EEPROM.read(3);

  // SETTINGS
  speedToSet = false;
  hourToSet = false;
  ledToSet = false;
  recordToSet = false;

  // ENCODER
  menu = 0;
  pinMode(pinCLK, INPUT_PULLUP);
  pinMode(pinDT, INPUT_PULLUP);
  pinMode(pinSW, INPUT_PULLUP);

  // INTERRUPT
  attachInterrupt(digitalPinToInterrupt(pinDT), doEncoderDT, CHANGE); //RISING
  attachInterrupt(digitalPinToInterrupt(pinSW), doSwitch, LOW);

  // DISPLAY
  display.begin(&Adafruit128x64, I2C_ADDRESS);
  display.setFont(Adafruit5x7);
  display.clear();
  Serial.begin(9600);

  // LED
  pixels.begin(); // This initializes the NeoPixel library.
  pixels.setBrightness(100); // 1/4 light

  // SOGLIE VELOCITA'
  calcSpeedTres();

  // CRONO
  secondsRaw = 0;
  temp_secondsRaw = 0;
  minutes = 0;
  seconds = 0;
  cronoStatus = 0;
  startCrono = 0;

  // TEMPERATURA
  temperature = getTemp();

  // BENVENUTO
  display.setFont(Callibri14);
  display.set2X();
  display.setCursor(5, 1);
  display.println(F("MopedGPS"));
  display.set1X();
  display.setFont(Adafruit5x7);
  display.setCursor(display.displayWidth() / 2 - 40, display.displayRows() / 2 + 1);
  display.print(F("PORCO RACING"));
  display.setCursor(display.displayWidth() / 2 - 30, (display.displayRows() / 2) + 2);
  display.print(F("ELECTRONICS"));

  theaterChase(pixels.Color(0, 0, 127), 50);

  display.clear();
  display.setCursor(display.displayWidth() / 2 - 52, display.displayRows() / 2 - 1);
  display.print(F("by Andrea Grazioli"));
  display.setCursor(display.displayWidth() / 2 - 37, 7);
  display.print(F("SW ver. 1.0"));
  delay(4000);
  display.clear();

  setup_choice = 0;
  menu = 1;

  pixels.setBrightness(32); // 1/4 luminosita
}


void loop() {
  temperature = getTemp();
  printNumberMenu(menu);
  saveSettings();

  
  // GESTIONE LED
  if (led_onoff) {
    printLed(kmh); // <--------- KMH
  }
  else
    printLed(0);

  displayCleaner();

  // CRONO
  if (cronoStatus == 1)
    secondsRaw = ((millis() - startCrono) / 1000) + temp_secondsRaw;

  minutes = secondsRaw / 60;
  seconds = secondsRaw % 60;

  // FIRST SCREEN
  // Velocità  - Temperatura testa - RPM
  if (menu == 1)
    firstScreen();
    
  // SECOND SCREEN
  // Latitudine - Longitudine - Altezza - Data - Ora
  if (menu == 2) 
    secondScreen();
    
  // THIRD SCREEN
  // RECORD
  if (menu == 3) 
    thirdScreen();

  //  FORTH SCREEN
  // SETUP
  if (menu == 4) 
    fourthScreen();

}

void firstScreen() {
  setup_choice = 0;
  attachInterrupt(digitalPinToInterrupt(pinSW), doSwitchCrono, LOW);

  printSpeedTempCrono();

  while (Serial.available() > 0) {
    if (gps.encode(Serial.read())) {
      if (gps.speed.isValid())
        kmh = (byte)gps.speed.kmph();
      else
        kmh = 0;
    }
  }
}

void secondScreen() {
  // Disabling interrupt click
  setup_choice = 0;
  detachInterrupt(digitalPinToInterrupt(pinSW));

  printGpsStat();
  while (Serial.available() > 0)
    gps.encode(Serial.read());
}

void thirdScreen() {
  // RECORD
  setup_choice = 0;
  detachInterrupt(digitalPinToInterrupt(pinSW));
  
  display.set2X();
  display.setCursor(0, 0);
  display.println(F("SPEED MAX"));

  display.set2X();
  display.setFont(Verdana_digits_24);
  display.print(recordKmh);
  display.setFont(System5x7);
}

void fourthScreen() {
  attachInterrupt(digitalPinToInterrupt(pinSW), doSwitch, LOW);

  display.home();
  display.set2X();
  display.print(F("SETUP"));

  if (setup_choice != 0)
    printSettings();
}

void printNumberMenu(byte m) {
  display.set1X();
  display.setCursor(123, 0);
  display.println(m);
}

void displayCleaner() {
  if (changedMenu) {
    display.clear();
    changedMenu = false;
  }
}

void printGpsStat() {
  display.setCursor(0, 0);
  display.set2X();

  // *** LAT E LONG
  if (gps.location.isValid())
  {
    display.println(gps.location.lat(), 4);
    display.println(gps.location.lng(), 4);
  }
  else
    display.println(F("-"));

  // *** ALTITUDE
  if (gps.altitude.isValid())
    display.print(gps.altitude.meters());
  else
    display.print(F("-"));

  // *** LABELS
  display.set1X();
  display.setCursor(85, 0);
  display.print(F(" LAT"));
  display.setCursor(85, 2);
  display.print(F(" LONG"));
  display.setCursor(85, 4);
  display.print(F(" MSLM"));

  display.setCursor(0, 7);
  display.set1X();
  display.println();
  if (gps.date.isValid())
  {
    display.print(gps.date.day());
    display.print(F("/"));
    display.print(gps.date.month());
    display.print(F("/"));
    display.print(gps.date.year());
  }
  else
    display.print(F("Nessun dato"));

  display.print(F(" "));
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10) display.print(F("0"));
    display.print(gps.time.hour() + (hourLegal + 1));
    display.print(F(":"));
    if (gps.time.minute() < 10) display.print(F("0"));
    display.print(gps.time.minute());
  }
  else
    display.print(F("-"));
}

void printSpeedTempCrono() {
  // VELOCITA
  if (kmh != lastkmh) {
    display.clear(0, 65, 0, 6);
    lastkmh = kmh;
  }
  display.home();
  display.set2X();
  display.setFont(Verdana_digits_24);

  if (kmh > 99)
    display.print(99);
  else
    display.print(kmh);

  if (kmh > recordKmh){
    recordKmh=kmh;
    recordToSet=true;
  }
  
  display.setFont(System5x7);

  // LABELS
  display.set1X();
  display.setCursor(17, 7);
  display.print(F("KMH"));
  display.setCursor(92, 5);
  display.print(F("CRONO"));
  display.setCursor(92, 1);
  display.print(F("TEMP"));

  // TEMP
  display.set2X();
  display.setCursor(92, 2);
  display.print(temperature);

  // CRONO
  display.setCursor(58, 6);

  if (minutes < 10)
    display.print(F("00"));
  else if (minutes < 100)
    display.print(F("0"));
  display.print(minutes);

  display.print(F(":"));

  if (seconds < 10)
    display.print(F("0"));
  display.print(seconds);
}

void doEncoderDT() {
  if ((millis() - lastRot) > DEBOUNCEMS)
  {
    // SCROLLING SCREENS -----------
    if (setup_choice == 0)
    {
      if (digitalRead(pinCLK) == digitalRead(pinDT))
        menu--;
      else
        menu++;

      if (menu > 4)
        menu = 1;
      if (menu < 1)
        menu = 4;
      changedMenu = true;
    }
    // ---------------------------
    // IN SETUP
    if (setup_choice > 10 && setup_choice < 16)
    {
      if (digitalRead(pinCLK) == digitalRead(pinDT))
        setup_choice--;
      else
        setup_choice++;

      if (setup_choice > 15)
        setup_choice = 11;
      if (setup_choice < 11)
        setup_choice = 15;
    }
    // CHANGING MAXSPEED
    if (setup_choice == 21)
    {
      if (digitalRead(pinCLK) == digitalRead(pinDT))
        maxSpeed--;
      else
        maxSpeed++;

      if (maxSpeed < 10)
        maxSpeed = 99;
      if (maxSpeed > 99)
        maxSpeed = 10;
    }
    // CHANGINH HOURLEGAL
    if (setup_choice == 22)
    {
      if (digitalRead(pinCLK) == digitalRead(pinDT))
        hourLegal = 0;
      else
        hourLegal = 1;
    }
    // CHANGING LED_ONOFF
    if (setup_choice == 23)
    {
      if (digitalRead(pinCLK) == digitalRead(pinDT))
        led_onoff = 0;
      else
        led_onoff = 1;
    }
  }
  lastRot = millis();
}

void doSwitch() {
  if ((millis() - lastClick) > DEBOUNCEMS) {
    // saving speedToSet
    if (setup_choice == 21)
      speedToSet = true;

    // saving ora legale
    if (setup_choice == 22)
      hourToSet = true;

    // saving led_onoff
    if (setup_choice == 23)
      ledToSet = true;

    // setting min rpm
    if (setup_choice == 11)
      setup_choice = 21;

    // setting max rpm
    if (setup_choice == 12)
      setup_choice = 22;

    // setting led
    if (setup_choice == 13)
      setup_choice = 23;

    // in menu
    if (setup_choice == 0)
      setup_choice = 11;

    // want to exit
    if (setup_choice == 14)
    {
      setup_choice = 0;
      changedMenu = true;
    }
   
    if(setup_choice == 15){
      led_onoff=1;
      hourLegal=0;
      maxSpeed=70;
      recordKmh=0;

      speedToSet=true;
      hourToSet=true;
      ledToSet=true;
      recordToSet=true;
    }
  }

  lastClick = millis();
}

void doSwitchCrono() {
  if ((millis() - lastClick) > DEBOUNCEMS) {
    if (cronoStatus == 0) {
      seconds += temp_secondsRaw;
      cronoStatus = 1;
      startCrono = millis();
    }
    else {
      if (cronoStatus == 1) {
        cronoStatus = 2;
        temp_secondsRaw = seconds;
      }
      else {
        if (cronoStatus == 2) {
          if (millis() - lastClick < 1500) {
            cronoStatus = 0;
            secondsRaw = 0;
            temp_secondsRaw = 0;
          }
          else {
            cronoStatus = 1;
            startCrono = millis();
          }
        }
      }
    }
  }
  lastClick = millis();
}

void printSettings() {
  //display.clearToEOL();
  display.set1X();
  display.clear(0, 9, 2, 7);

  // Selection sign
  if (setup_choice == 11 || setup_choice == 21)
    display.setCursor(0, 2);
  if (setup_choice == 12 || setup_choice == 22)
    display.setCursor(0, 3);
  if (setup_choice == 13 || setup_choice == 23)
    display.setCursor(0, 4);
  if (setup_choice == 14)
    display.setCursor(0, 5);
    // spazio vuoto 
  if (setup_choice == 15)
    display.setCursor(0, 7);


  display.print(F(">"));
  /////////////////////////
  display.setCursor(10, 2);
  display.print(F("MAX SPEED: "));
  display.print(maxSpeed);
  display.setCursor(10, 3);
  display.print(F("ORA LEGALE: "));
  display.print(hourLegal);
  display.setCursor(10, 4);
  display.print(F("LED ON: "));
  display.print(led_onoff);
  display.setCursor(10, 5);
  display.print(F("EXIT"));
  display.setCursor(10, 7);
  display.print(F("RESET"));
}

void saveSettings() {
  if (speedToSet) {
    EEPROM.write(0, maxSpeed);
    setup_choice = 11;
    calcSpeedTres();
    speedToSet = false;
  }
  if (hourToSet) {
    EEPROM.write(1, hourLegal);
    setup_choice = 12;
    hourToSet = false;
  }
  if (ledToSet) {
    EEPROM.write(3, led_onoff);
    setup_choice = 13;
    ledToSet = false;
  }
  if(recordToSet) {
    EEPROM.write(2, kmh);
    recordKmh = EEPROM.read(2);
    recordToSet=false;
  }
}

void printLed(byte kmh) {
  if (kmh == 0)
    pixelsTreshold(0, 0, 0);

  if (kmh > 0 && kmh <= speedTresholds[1])
    pixelsTreshold(1, 0, 0);

  if (kmh > speedTresholds[1] && kmh <= speedTresholds[2])
    pixelsTreshold(2, 0, 0);

  if (kmh > speedTresholds[2] && kmh <= speedTresholds[3])
    pixelsTreshold(3, 0, 0);

  if (kmh > speedTresholds[3] && kmh <= speedTresholds[4])
    pixelsTreshold(4, 0, 0);

  if (kmh > speedTresholds[4] && kmh <= speedTresholds[5])
    pixelsTreshold(5, 0, 0);

  if (kmh > speedTresholds[5] && kmh <= speedTresholds[6])
    pixelsTreshold(6, 0, 0);

  if (kmh > speedTresholds[6] && kmh <= speedTresholds[7])
    pixelsTreshold(7, 1, 0);

  if (kmh > speedTresholds[7] && kmh <= speedTresholds[8])
    pixelsTreshold(7, 2, 0);

  if (kmh > speedTresholds[8] && kmh <= speedTresholds[9])
    pixelsTreshold(7, 3, 0);

  if (kmh > speedTresholds[9] && kmh <= speedTresholds[10])
    pixelsTreshold(7, 3, 1);

  if (kmh >= speedTresholds[10])
    pixelsTreshold(7, 3, 2);

  pixels.show();
}

void calcSpeedTres() {
  float temp = maxSpeed / 12;

  speedTresholds[0] = 0;
  for (byte i = 0; i < 11; i++) {
    speedTresholds[i] = (byte)(int)(temp * (i + 1));
  }
}

void pixelsTreshold(byte g, byte y, byte r) {
  byte i;
  for (i = 0; i < g; i++)
    pixels.setPixelColor(i, pixels.Color(0, 150, 0));
  for (i = g; i < g + y; i++)
    pixels.setPixelColor(i, pixels.Color(255, 255, 0));
  for (i = g + y; i < g + y + r; i++)
    pixels.setPixelColor(i, pixels.Color(255, 0, 0));
  for (i = 11; i > g + y + r; i--) //11
    pixels.setPixelColor(i, pixels.Color(0, 0, 0));

  if (g == 0 & y == 0 &  r == 0)
    pixels.setPixelColor(0, pixels.Color(0, 0, 0));
}

short getTemp() {
  //returns the temperature from one DS18B20 in DEG Celsius

  byte data[12];
  byte addr[8];

  if ( !ds.search(addr)) {
    //no more sensors on chain, reset search
    ds.reset_search();
    return -99;
  }

  if ( OneWire::crc8( addr, 7) != addr[7]) {
    return -99;
  }

  if ( addr[0] != 0x10 && addr[0] != 0x28) {
    return -99;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1); // start conversion, with parasite power on at the end

  byte present = ds.reset();
  ds.select(addr);
  ds.write(0xBE); // Read Scratchpad

  for (int i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }

  ds.reset_search();

  byte MSB = data[1];
  byte LSB = data[0];

  float tempRead = ((MSB << 8) | LSB); //using two's compliment
  float TemperatureSum = tempRead / 16;

  return (short)TemperatureSum;
}

void theaterChase(uint32_t c, uint8_t wait) {
  for (int j = 0; j < 20; j++) { //do 10 cycles of chasing
    for (int q = 0; q < 3; q++) {
      for (uint16_t i = 0; i < 12; i = i + 3) {
        pixels.setPixelColor(i + q, c);  //turn every third pixel on
      }
      pixels.show();

      delay(wait);

      for (uint16_t i = 0; i < 12; i = i + 3) {
        pixels.setPixelColor(i + q, 0);      //turn every third pixel off
      }
    }
  }
}
