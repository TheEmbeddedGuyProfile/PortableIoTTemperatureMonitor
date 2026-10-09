/*
 *  Project: Portable IoT Contactless Thermometer
 *  Description: Reads temperature data from an MLX90614 IR sensor, displays it locally 
 *               on an SSD1306 OLED screen, and hosts a local web server to broadcast 
 *               the live readings over Wi-Fi.
 *  
 *  Hardware Connections (ESP8266):
 *  - I2C SDA: GPIO2
 *  - I2C SCL: GPIO0
 *  
 *  Required Libraries:
 *  - Adafruit GFX Library
 *  - Adafruit SSD1306
 *  - Adafruit MLX90614
 *  - ESP8266WiFi / ESP8266WebServer (Included with ESP8266 Board Package)
 */

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>       // For OLED
#include <Adafruit_SSD1306.h>   // For OLED
#include <Adafruit_MLX90614.h>  // For Temp Sensor
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// -----------------------------------------
// REPLACE WITH YOUR NETWORK CREDENTIALS
// -----------------------------------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

ESP8266WebServer server(80);
IPAddress ip;

// Global variable to hold the sensor value
String sensorValue = "0";

Adafruit_MLX90614 mlx = Adafruit_MLX90614();

// Function Prototypes
void displayTemperature(float ambient, float object);
void readSensor();
void handleRoot();
void handleReadSensor();
void testdrawrect(void);

// Non-blocking loop timing variables
unsigned long lastSensorReadTime = 0;
const unsigned long sensorReadInterval = 700; // Read sensor every 700ms

void setup() {
  Serial.begin(115200); // Start serial communication
  delay(100);           // Give serial time to stabilize
  
  // Initialize I2C pins safely (SDA = GPIO2, SCL = GPIO0)
  Wire.begin(2, 0);
  Wire.setClock(100000); // Set standard I2C speed to prevent bus collisions

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
 
  display.clearDisplay();
  testdrawrect();
  delay(500);

  if (!mlx.begin(MLX90614_I2CADDR, &Wire)) {
    Serial.println("Error connecting to MLX sensor. Check wiring.");
    // Prevent hardware Watchdog Timer (WDT) reset if sensor is unplugged
    while (1) { 
      delay(100); 
    } 
  }
  Serial.println("MLX90614 sensor initialized.");

  // Force the ESP8266 to act ONLY as a client station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // Clear any corrupted flash configurations
  delay(100);

  // Connect to Wi-Fi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // Explicit timeout counter to prevent infinite connection loops
  int timeoutCount = 0;
  while (WiFi.status() != WL_CONNECTED && timeoutCount < 30) {
    delay(500); // Keeps background SDK alive and feeds the WDT
    Serial.print(".");
    timeoutCount++;
  }

  if(WiFi.status() != WL_CONNECTED) {
    Serial.println("\nConnection Failed! Rebooting...");
    ESP.restart();
  }

  ip = WiFi.localIP();
  
  // Format OLED text to fit within 128px buffers without memory faults
  display.stopscroll();
  display.clearDisplay();
  display.setTextSize(1); 
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connected to Wi-Fi!");
  display.setTextSize(2); // Size 2 fits an IP address perfectly across the width
  display.println("IP:");
  display.println(ip.toString()); 
  display.display();
  
  Serial.println("");
  Serial.print("Web server IP address: ");
  Serial.println(ip);

  // Set up server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/readSensor", HTTP_GET, handleReadSensor);
  server.begin();
  delay(1000);
}

void loop() {
  server.handleClient();
  
  // Non-blocking timer allows server.handleClient() to run smoothly without locking up
  if (millis() - lastSensorReadTime >= sensorReadInterval) {
    lastSensorReadTime = millis();
    readSensor();
  }
}

// Function to handle the AJAX request for sensor data
void handleReadSensor() {
  server.send(200, "text/plain", sensorValue);
}

// Startup animation for OLED
void testdrawrect(void) {
  display.stopscroll();
  display.clearDisplay();
  for(int16_t i=0; i<display.height()/2; i+=2) {
    display.drawRect(i, i, display.width()-2*i, display.height()-2*i, SSD1306_WHITE);
    display.display(); 
    delay(1);
  }
  delay(500);
}

// Updates the OLED screen with new temperature readings
void displayTemperature(float ambient, float object) {
  static int count = 10;
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  
  // Every 10 ticks, flash the IP address so the user can easily find it again
  if(count == 0) {
    display.setTextSize(1);
    display.println("IP:");
    display.setTextSize(2);
    display.println(WiFi.localIP().toString());
    display.display();
    delay(1000);
    display.clearDisplay();
    display.setCursor(0, 0);
    count = 10;
  }

  display.setTextSize(2);
  display.print("Amb:");
  display.println(ambient, 1); // Format to 1 decimal place
  display.print("Obj:");
  display.println(object, 1);
  display.display();
  count--;
}

// Reads the IR sensor and converts the data to Celsius
void readSensor() {
  float ambientTempF = static_cast<float>(mlx.readAmbientTempF());
  float objectTempF = static_cast<float>(mlx.readObjectTempF());
      
  // Convert Fahrenheit to Celsius
  float ambientTempC = (ambientTempF - 32.0) * 5.0 / 9.0;
  float objectTempC = (objectTempF - 32.0) * 5.0 / 9.0;

  Serial.print("Ambient Temp (C): ");
  Serial.print(ambientTempC);
  Serial.print(" | Object Temp (C): ");
  Serial.println(objectTempC);
  
  displayTemperature(ambientTempC, objectTempC);
  
  // Update global variable for the web server
  sensorValue = "Ambient: " + String(ambientTempC, 1) + "C <br> Object: " + String(objectTempC, 1) + "C";
}

// Serves the HTML dashboard to connected clients
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP8266 IoT Thermometer</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family: Arial; text-align: center; background-color: #f4f4f9;} h1{color:#007BFF;} .value{font-size: 2em; font-weight: bold; padding: 20px; background: #fff; border-radius: 10px; display: inline-block; box-shadow: 0 4px 8px rgba(0,0,0,0.1);}</style>";
  html += "</head><body>";
  html += "<h1>Live Temperature Data</h1>";
  html += "<p>Updating automatically every 2 seconds...</p>";
  html += "<div id='sensorValue' class='value'>" + sensorValue + "</div>";
  html += "<script>";
  html += "function updateSensorData() {";
  html += "  var xhttp = new XMLHttpRequest();";
  html += "  xhttp.onreadystatechange = function() {";
  html += "    if (this.readyState == 4 && this.status == 200) {";
  html += "      document.getElementById('sensorValue').innerHTML = this.responseText;";
  html += "    }";
  html += "  };";
  html += "  xhttp.open('GET', '/readSensor', true);";
  html += "  xhttp.send();";
  html += "}";
  html += "setInterval(updateSensorData, 2000);"; // Fetch new data every 2 seconds via AJAX
  html += "</script></body></html>";
  
  server.send(200, "text/html", html);
}
