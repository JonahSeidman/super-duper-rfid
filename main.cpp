#include <WiFi.h>
#include <FirebaseClient.h> // FirebaseClient by Mobizt
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <time.h>

// Replace with your network credentials
const char* ssid = "xxx";
const char* password = "xxx";

// Replace with your Firebase project credentials
#define FIREBASE_API_KEY "xxx"
#define FIREBASE_PROJECT_ID "esp32rfidproject"
#define FIREBASE_DATABASE_URL "xxx/" // Ensure trailing slash

// Initialize FirebaseClient object
FirebaseClient firebaseClient; // m 

// RFID setup
#define RST_PIN 22
#define SS_PIN 21
MFRC522 mfrc522(SS_PIN, RST_PIN);  // Create MFRC522 instance

// Web server setup
WebServer server(80);

// Time setup
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -28800;      // Adjust to your timezone (e.g., -28800 for PST)
const int daylightOffset_sec = 3600;    // Adjust if daylight saving time is applicable

// Variables
String uidString;
String currentTime;

// Function declarations
void handleRoot();
void handleProfiles();
void handleNotFound();
void configureTime();
String getFormattedTime();
void handleAPIRFID();
void handleAPIUpdateName();
void handleExportData();
void renderHeader(String& html, String title);
void renderFooter(String& html);
void handleFavicon();

void setup() {
  Serial.begin(9600); // Ensure this matches monitor_speed=112500
  delay(1000);
  
  // Initialize SPI bus
  SPI.begin();
  
  // Initialize MFRC522 RFID reader
  mfrc522.PCD_Init();
  delay(4);
  Serial.println("RFID reader initialized.");

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("An error occurred while mounting LittleFS");
  } else {
    Serial.println("LittleFS mounted successfully");
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Configure time
  configureTime();

  // FirebaseClient doesn't have a "begin" function.
  // Firebase-related operations should directly use the API methods for interacting with Realtime Database.
  
  // Set up web server routes
  server.on("/", handleRoot);
  server.on("/profiles", handleProfiles);
  server.on("/api/rfid", handleAPIRFID);
  server.on("/api/updateName", HTTP_POST, handleAPIUpdateName);
  server.on("/exportData", handleExportData);
  server.on("/favicon.ico", handleFavicon);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();

  // Check for new RFID cards
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    uidString = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      if (mfrc522.uid.uidByte[i] < 0x10) {
        uidString += "0";
      }
      uidString += String(mfrc522.uid.uidByte[i], HEX);
    }
    uidString.toUpperCase();
    Serial.print("Card UID: ");
    Serial.println(uidString);

    // Get current time
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char timeString[25];
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    currentTime = String(timeString);

    // Placeholder for Firebase interaction
    // FirebaseClient API must be used correctly here depending on the available API methods for the library.

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
}

// ------------------- Web Server Handlers -------------------

void handleRoot() {
  String html;
  renderHeader(html, "RFID Attendance Tracker");

  html += "<div class='container'>";
  html += "<h1>Welcome to the RFID Attendance Tracker</h1>";
  html += "<p>Use your RFID card to register your attendance.</p>";
  html += "<p>Current Time: ";
  html += getFormattedTime();
  html += "</p>";
  html += "</div>";

  renderFooter(html);
  server.send(200, "text/html", html);
}

void handleProfiles() {
  // Placeholder: Firebase interaction will depend on correct FirebaseClient usage
}

void handleAPIRFID() {
  // Placeholder: Firebase interaction will depend on correct FirebaseClient usage
}

void handleAPIUpdateName() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Placeholder: Firebase interaction will depend on correct FirebaseClient usage
}

void handleExportData() {
  // Placeholder: Firebase interaction will depend on correct FirebaseClient usage
}

void handleFavicon() {
  server.send(204, "image/x-icon", ""); // No Content
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// ------------------- Helper Functions -------------------

void renderHeader(String& html, String title) {
  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>";
  html += title;
  html += "</title>";
  html += "<style>";
  html += "body { font-family: Arial; margin: 0; padding: 0; background-color: #f2f2f2; }";
  html += "nav { background-color: #333; color: #fff; padding: 10px; }";
  html += "nav a { color: #fff; margin-right: 15px; text-decoration: none; }";
  html += ".container { padding: 20px; }";
  html += "table { width: 100%; border-collapse: collapse; }";
  html += "th, td { border: 1px solid #ddd; padding: 8px; }";
  html += "th { background-color: #333; color: white; }";
  html += "</style></head><body>";
  html += "<nav><a href='/'>Home</a><a href='/profiles'>Profiles</a><a href='/exportData'>Export Data</a></nav>";
}

void renderFooter(String& html) {
  html += "</body></html>";
}

void configureTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Configuring time...");

  struct tm timeinfo;
  int retries = 0;
  const int maxRetries = 10;
  while (!getLocalTime(&timeinfo) && retries < maxRetries) {
    Serial.println("Waiting for time synchronization...");
    delay(1000);
    retries++;
  }

  if (retries < maxRetries) {
    Serial.printf("Time configured: %04d-%02d-%02d %02d:%02d:%02d\n",
                  timeinfo.tm_year + 1900,
                  timeinfo.tm_mon + 1,
                  timeinfo.tm_mday,
                  timeinfo.tm_hour,
                  timeinfo.tm_min,
                  timeinfo.tm_sec);
  } else {
    Serial.println("Failed to obtain time");
  }
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not available";
  }
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}
