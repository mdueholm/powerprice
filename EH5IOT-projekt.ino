/*! @mainpage EH5IOT-projekt
 * @htmlonly <style>div.image img[src="switch.jpg"]{width:40%;}</style> \endhtmlonly
 * @image html switch.jpg "Opstillingen"
 *
 * \anchor imgDef <!-- added line -->
 * ![imgDef]
 *
 * [imgDef]: img/switch.jpg "Diagram Caption" <!-- no need for the @ref here -->
 *
 * ## Some Other Section
 *
 * This is a test to put a link to the image above.  See [here](@ref imgDef)
 *
 * @section Introduktion
 *
 * Et forsøg på at lave Doxygen-venlig kode
 *
 * @section Forbindelser
 *
 * @subsection step1 Step 1: Her kommer step1
 * @subsection step2 Step 2: Her kommer step2
 *
 */

/**
 * @file EH5IOT-projekt.ino
 * @brief Energi måling og visning af pris
 */

/*
 * Project EH5IOT-projekt
 * Description: Measure power consumption, calculate the cost and show it on a display
 * Author: Morten Dueholm
 * Date: Fall 2022
 */

#include <Adafruit_SSD1306.h>    // Display driver
#include <FreeMono9pt7b.h>       // 7pt font for display
#include <FreeSansBold9pt7b.h>   // 9pt font for display
#include <FreeSerifBold12pt7b.h> // 12pt bold font for display
#include <stdio.h>
#include <string.h>
#include <math.h> // For math functions

#include "EmonLib.h" // Include Emon Library
EnergyMonitor emon1; // Create an instance

#include <movingAvg.h> // Moving average https://github.com/JChristensen/movingAvg
movingAvg Watt_ma(10); // Define the moving average object

#include "Debounce.h"            // Debounce library
Debounce debouncer = Debounce(); // Create an instance of the debouncer

#include "sht3x-i2c.h" // SHT3x temperature and humidity sensor
Sht3xi2c sensor(Wire); // Create an instance of the sensor

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define FONT0 FreeMono9pt7b       // 9pt monofont for display
#define FONT1 FreeSansBold9pt7b   // 9pt font for display
#define FONT2 FreeSerifBold12pt7b // 12pt bold font for display

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1                                                     // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Create display object

// We will be using D5 to control our LED
int ledPin = D5;

// Our button wired to D6
int buttonPin = D6;

int YEAR = 0, MONTH = 0, DAY = 0, HOUR = 0, MINUTE = 0, SECOND = 0; // Variables to hold the present time
// int PRESENT_HOUR = 0;
int presentDisplay = 1;                     // Variable to hold the present display
unsigned long millisCounter = millis();     // Variable to hold the present time in milliseconds
unsigned long displayUpdateInterval = 1000; // Variable to hold the display update interval in milliseconds

char data_str[400];       // String to hold the data, 288 should be enough, added a little overhead
float price[24] = {0};    // Array to hold the electricity-prices for every hour on present day
float price_now = 999999; // Variable to hold the electricity-price for the present hour
float price_Perhour;      // Variable to hold the total price for the present hour
bool gotData = false;     // Flag to indicate that data has been received

int Voltage = 228; // Variable to hold the present voltage

int watt = 0;             // Variable to hold the present power consumption
int W_avg;                // Variable to hold the average power consumption
double I_baseline = 0.10; // Variable to hold the baseline current
double Irms = 0;          // Variable to hold the present current

double temp, humid; // Variables to hold the present temperature and humidity

const struct networkFee_t // Struct to hold the network fee
{
  float high = 1.8644; // high price, https://www.elnetmidt.dk/priser
  float medium = 0;    // Not to be used until after 1/1-2023
  float low = 1.3180;  // low price, https://www.elnetmidt.dk/priser
} networkFee;

void myHandler(const char *event, const char *data)
{
  Serial.printlnf("\ndata: %s", data);
  Serial1.printf("myHandler\n"); // Print to serial monitor on Otii Arc
  strcpy(data_str, data);        // Copy the data to the string
  gotData = true;                // Set the data-received flag
}

SYSTEM_THREAD(ENABLED); // Enable system threading

void setup()
{
  Serial1.begin(115200);
  Serial1.printf("Setup\n"); // Print to serial monitor on Otii Arc
  setupDisplay();            // Setup the display
  waitforConnection();       // Wait for connection to the cloud

  /** @brief Setting timezone to Denmark/CET */
  Time.zone(+1); // Central European Time

  Particle.subscribe("elpris_DK", myHandler, MY_DEVICES); // Subscribe to the integration response event

  emon1.current(19, 12.5); // Current: Input pin is 19, calibration is 12.5

  Watt_ma.begin(); // Initialize the moving average object

  pinMode(ledPin, OUTPUT);                   // Set led-pin as output
  pinMode(buttonPin, INPUT_PULLUP);          // Set button-pin as input
  debouncer.attach(buttonPin, INPUT_PULLUP); // Attach the button to the debouncer
  debouncer.interval(100);                   // Interval in ms

  sensor.begin(CLOCK_SPEED_400KHZ); // Initialize the temp-sensor
}

void loop()
{
  bool buttonState = digitalRead(buttonPin);
  if (buttonState == LOW)
  {
    millisCounter = displayUpdateInterval + 1; // Set the counter to update display next time
    presentDisplay = 2;
  }

  if (Time.day() != DAY) // If the day has changed
    gotData = false;     // Reset the data-received flag to get data from current day

  if (!gotData)  // If we have not received data yet
    getPrices(); // Get the prices for the present day

  /** Update display */
  if (millis() - millisCounter > displayUpdateInterval)
  {
    millisCounter = millis(); // Reset the millis-counter
    debugPrint();
    // millisCounter = millis(); // reset the counter

    if (presentDisplay == 1)
    {
      displayUpdateInterval = 2000;
      getTime();       // Get the time
      getCurrent();    // Get the current
      writeDisplay1(); // Write to the display
    }

    else
    {
      presentDisplay = 1;
      displayUpdateInterval = 10000;
      writeDisplay2();
    }
  }
}

void writeDisplay1() // Write page 1 to the display
{
  HOUR = (Time.hour());        // Get the present hour
  display.clearDisplay();      // Clear the buffer
  display.setTextColor(WHITE); // Set the text color to white

  /** Display date and time */
  display.setFont();       // Set font to default
  display.setCursor(0, 0); // (0,0) is top-left corner (x right, y down). An offset may be needed when using external fonts
  // display.print(Time.timeStr());
  display.print(String(DAY) + "/" + String(MONTH) + "-" + String(YEAR) + " " + String(HOUR) + ":");

  /** @brief Add a leading zero if minute is less than 10 */
  if (MINUTE < 10)
    display.print("0" + String(MINUTE));
  else
    display.print(String(MINUTE));

  /** @brief Calculate price incl. VAT and other expences  */
  if (HOUR >= 17 && HOUR <= 20)                          // if it is between 17:00 and 20:00
    price_now = price[HOUR] * 0.00125 + networkFee.low;  // price MWh->kWh incl. VAT and other expences, cheap hours
  else                                                   // if it is between 21:00 and 6:00
    price_now = price[HOUR] * 0.00125 + networkFee.high; // price MWh->kWh incl. VAT and other expences, expensive hours

  price_Perhour = price_now * W_avg / 1000; // Total price/hour in DKK/kWh

  /** @brief Display power usage  */
  display.setFont();                // Set font
  display.setCursor(0, 28);         // (0,0) is top-left corner (x right, y down).
  display.println("Forbrug i W: "); // Print text to display buffer
  display.setFont(&FONT1);          // Use FONT1
  display.setCursor(85, 36);        // Set cursor position
  display.print(String(W_avg));     // Print text to display buffer

  /** @brief Display price watt/hour  */
  display.setFont(); // Set font
  display.setCursor(0, 50);
  display.println("Pris kr/time: ");
  display.setFont(&FONT1); // Set font
  display.setCursor(85, 58);
  display.print(String(price_Perhour, 2));
  display.display(); // Display the buffer
}

void writeDisplay2() // Write page 2 to the display
{
  if (sensor.single_shot(&temp, &humid) == 0) // Read SHT3x sensor-data in single-shot mode
  {
    Serial.printf("Sensor single shot read \n"); // Print to serial monitor
    Serial1.printf("writeDisplay2\n");           // Print to serial monitor on Otii Arc
  }

  temp = round(temp * 100.0) / 100.0;           // Get temp, round to 2 decimals
  humid = round(humid * 100.0) / 100.0;         // Get humidity, round to 2 decimals
  price_now = round(price_now * 100.0) / 100.0; // Get current price, round to 2 decimals

  HOUR = (Time.hour());
  display.clearDisplay(); // Clear the buffer
  display.setTextColor(WHITE);

  /** @brief current price  */
  display.setFont(); // Set font
  display.setCursor(0, 6);
  display.println("Elpris nu, kr. ");
  display.setFont(&FONT1); // Set font
  display.setCursor(85, 14);
  display.printf(String(price_now, 2));

  /** @brief Display temp  */
  display.setFont(); // Set font
  display.setCursor(0, 28);
  display.println("Temp. i C ");
  display.setFont(&FONT1); // Set font
  display.setCursor(85, 36);
  display.print(String(temp, 1));

  /** @brief Display humidity  */
  display.setFont(); // Set font
  display.setCursor(0, 50);
  display.println("Luftfugt i % ");
  display.setFont(&FONT1); // Set font
  display.setCursor(85, 58);
  display.print(String(humid, 1));
  display.display();
}

void waitforConnection()
{
  Serial1.printf("waitforConnection\n"); // Print to serial monitor on Otii Arc
  /** @brief Waiting for Argon to connect to the cloud  */
  display.setCursor(0, 18);    //
  display.setFont(&FONT1);     // Use FONT1
  display.setTextColor(WHITE); // Set the text color to white
  display.println(("Connecting"));
  display.println(("Please wait"));
  display.display(); // Display the buffer
  Serial.println("Waiting for connection");

  waitUntil(Particle.connected); // Wait until connected to the cloud

  Serial.println("Connected to the cloud");
}

void setupDisplay() // Setup the display
{
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3c))
  { // Address 0x3D for 128x64
    Serial.println("SSD1306 allocation failed");
    for (;;)
      ; // Don't proceed, loop forever
  }
  display.setFont(&FONT2); // Use FONT2
  display.clearDisplay();  // Clear the buffer
  display.setRotation(2);  // Rotate: 0=0 degrees, 2=180 degrees
}

void getTime() // Get the time
{
  /** @brief Split current time to separat int's*/
  YEAR = (Time.year());
  MONTH = (Time.month());
  DAY = (Time.day());
  HOUR = (Time.hour());
  MINUTE = (Time.minute());
}

void getPrices() // Get todays electricityprices via webhook
{
  Serial1.printf("getPrices\n"); // Print to serial monitor on Otii Arc
  Serial.printlnf("Waiting for connection");
  waitUntil(Particle.connected); // Wait for connection

  /** @brief Get the prices for the present day */
  Serial.printlnf("Getting data");
  display.clearDisplay();   // Clear the buffer
  display.setCursor(0, 18); // (0,0) is top-left corner (x right, y down).
  display.println("Getting data");
  display.display();
  String get_data = String::format("{ \"year\": \"%d\", \"month\":\"%02d\", \"day\": \"%02d\"}", Time.year(), Time.month(), Time.day());

  // Trigger the integration
  Particle.publish("elpris", get_data, PUBLIC);

  /* First call to strtok should be done with string and delimiter as first and second parameter*/
  while (!gotData) // Wait for the data to be received
  {
    delay(2000);
    Serial.print("Still getting data ");
  }
  Serial.println("Got data");

  display.clearDisplay();        // Clear the buffer
  display.print("Got the data"); // Print text to display buffer
  display.display();             // Display the buffer
  delay(2000);

  char *token = strtok(data_str, ",'"); // split the string into tokens
  unsigned int count = 0;               // counter for the array

  while (token != NULL) // while there are tokens left
  {
    price[count] = atof(token); // convert the token to a float and store it in the array
    token = strtok(NULL, ",'"); // get the next token, delimiter is "'"

    Serial.printf("%d %f\n", count, price[count]); // print the token and the value
    count++;                                       // increment the counter
  }
}

void getCurrent()
{
  Irms = emon1.calcIrms(1480); // Calculate Irms only
  Irms = Irms - I_baseline;    // Baseline for sensor uden forbrug med monteret el-måler til sensor-kalibrering
  watt = Irms * Voltage;       // Calculate real power

  W_avg = Watt_ma.reading(watt); // Calculate the moving average
}

void debugPrint() // For debugging
{
  /** @brief For debugging   */
  Serial.print("W_avg ");
  Serial.print(W_avg); // print the moving average
  Serial.print("  HOUR ");
  Serial.print(HOUR); // print the moving average
  Serial.print("  price(HOUR) ");
  Serial.print(price[HOUR]);
  Serial.print("  price_now ");
  Serial.print(price_now);
  Serial.print("  price_Perhour ");
  Serial.print(price_Perhour);
  Serial.print("  presentDisplay ");
  Serial.print(presentDisplay);
  Serial.print("  displayUpdateInterval ");
  Serial.print(displayUpdateInterval);
  Serial.print("  millisCounter ");
  Serial.print(millisCounter);
  Serial.println(' ');
}