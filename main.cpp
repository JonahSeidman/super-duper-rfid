#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <time.h>

// Wi-Fi credentials
const char* ssid = "X";
const char* password = "X";

// Firebase API key and database URL
#define API_KEY "X"
#define DATABASE_URL "X``your text``"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// RFID setup
#define RST_PIN 22
#define SS_PIN 21
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Web server setup
WebServer server(80);

// Time setup
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -28800;
const int daylightOffset_sec = 3600;

// Variables
String uidString;
String currentTime;

// Function declarations
void handleRoot();
void handleProfiles();
void handleNotFound();
void tokenStatusCallback(TokenInfo info);
void configureTime();
String getFormattedTime();
void handleAPIRFID();
void handleAPIUpdateName();
void handleExportData();
void renderHeader(String& html, String title);
void renderFooter(String& html);
void handleFavicon();
String getTokenType(TokenInfo info);
String getTokenStatus(TokenInfo info);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();
  delay(4);

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

  // Ensure no email/password is set
  auth.user.email = "";
  auth.user.password = "";

  // Initialize Firebase Config
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.anonymous = true;

  // Assign the callback function for token status
  config.token_status_callback = tokenStatusCallback;

  // Initialize Firebase
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Wait for authentication to complete
  Serial.println("Authenticating with Firebase...");

  unsigned long authTimeout = millis();
  const unsigned long authTimeoutDuration = 10000; // 10 seconds timeout

  while ((auth.token.uid.length() == 0) && (millis() - authTimeout < authTimeoutDuration)) {
    Firebase.ready(); // This updates the auth.token information
    delay(500);
    Serial.print(".");
  }

  if (auth.token.uid.length() != 0) {
    Serial.println("\nFirebase authentication successful.");
    Serial.print("User UID: ");
    Serial.println(auth.token.uid.c_str());
  } else {
    Serial.println("\nFailed to authenticate with Firebase.");
    Serial.println("Check your Firebase configuration and ensure anonymous authentication is enabled.");
  }

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
      uidString.concat(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      uidString.concat(String(mfrc522.uid.uidByte[i], HEX));
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

    // Send data to Firebase
    String basePath = "/rfid/";
    basePath.concat(uidString);

    String path_last_scanned = basePath;
    path_last_scanned.concat("/last_scanned");

    if (Firebase.RTDB.setString(&fbdo, path_last_scanned.c_str(), currentTime)) {
      Serial.println("Timestamp sent to Firebase successfully");
    } else {
      Serial.println("Failed to send timestamp to Firebase");
      Serial.println(fbdo.errorReason());
    }

    String path_uid = basePath;
    path_uid.concat("/uid");

    if (Firebase.RTDB.setString(&fbdo, path_uid.c_str(), uidString)) {
      Serial.println("UID sent to Firebase successfully");
    } else {
      Serial.println("Failed to send UID to Firebase");
      Serial.println(fbdo.errorReason());
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
  }
}

// Web server handlers
void handleRoot() {
  String html;
  renderHeader(html, "RFID Attendance Tracker");

  html.concat("<div class='container'>");
  html.concat("<h1>Welcome to the RFID Attendance Tracker</h1>");
  html.concat("<p>Use your RFID card to register your attendance.</p>");
  html.concat("<p>Current Time: ");
  html.concat(getFormattedTime());
  html.concat("</p>");
  html.concat("</div>");

  renderFooter(html);
  server.send(200, "text/html", html);
}

void handleProfiles() {
  // Retrieve data from Firebase
  if (Firebase.RTDB.getJSON(&fbdo, "/rfid")) {
    FirebaseJson& json = fbdo.jsonObject();
    String jsonStr;
    json.toString(jsonStr, true);

    // Parse JSON data
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      server.send(500, "text/plain", "Failed to parse data");
      return;
    }

    // Generate HTML page
    String html;
    renderHeader(html, "Profiles");

    html.concat("<div class='container'>");
    html.concat("<h1>Scanned Cards</h1>");
    html.concat("<table><tr><th>UID</th><th>Last Scanned</th></tr>");

    for (JsonPair kv : doc.as<JsonObject>()) {
      String uid = kv.value()["uid"].as<String>();
      String lastScanned = kv.value()["last_scanned"].as<String>();
      html.concat("<tr><td>");
      html.concat(uid);
      html.concat("</td>");
      html.concat("<td>");
      html.concat(lastScanned);
      html.concat("</td></tr>");
    }

    html.concat("</table></div>");

    renderFooter(html);
    server.send(200, "text/html", html);
  } else {
    server.send(500, "text/plain", "Failed to retrieve data from Firebase");
    Serial.println(fbdo.errorReason());
  }
}

void handleAPIRFID() {
  // This handler can be used to provide RFID data as JSON
  if (Firebase.RTDB.getJSON(&fbdo, "/rfid")) {
    FirebaseJson& json = fbdo.jsonObject();
    String jsonStr;
    json.toString(jsonStr, true);
    server.send(200, "application/json", jsonStr);
  } else {
    server.send(500, "text/plain", "Failed to retrieve data from Firebase");
    Serial.println(fbdo.errorReason());
  }
}

void handleAPIUpdateName() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  // Parse the JSON body
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if (error) {
    Serial.print("Failed to parse updateName request: ");
    Serial.println(error.c_str());
    server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
    return;
  }

  String uid = doc["uid"].as<String>();
  String name = doc["name"].as<String>();

  if (uid.isEmpty()) {
    server.send(400, "application/json", "{\"success\":false, \"message\":\"UID is required\"}");
    return;
  }

  // Update name in Firebase
  String basePath = "/rfid/";
  basePath.concat(uid);
  basePath.concat("/name");

  if (Firebase.RTDB.setString(&fbdo, basePath.c_str(), name)) {
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(500, "application/json", "{\"success\":false, \"message\":\"Failed to update name in Firebase\"}");
    Serial.println(fbdo.errorReason());
  }
}

void handleExportData() {
  // Export data as JSON
  if (Firebase.RTDB.getJSON(&fbdo, "/rfid")) {
    FirebaseJson& json = fbdo.jsonObject();
    String jsonStr;
    json.toString(jsonStr, true);
    server.sendHeader("Content-Disposition", "attachment; filename=\"rfid_data.json\"");
    server.send(200, "application/json", jsonStr);
  } else {
    server.send(500, "text/plain", "Failed to retrieve data from Firebase");
    Serial.println(fbdo.errorReason());
  }
}

void handleFavicon() {
  server.send(204, "image/x-icon", ""); // No Content
}

void handleNotFound() {
  server.send(404, "text/plain", "404: Not Found");
}

// Helper functions

void renderHeader(String& html, String title) {
  html.concat("<!DOCTYPE html><html><head>");
  html.concat("<meta name='viewport' content='width=device-width, initial-scale=1'>");
  html.concat("<title>");
  html.concat(title);
  html.concat("</title>");
  html.concat("<style>");
  html.concat("body { font-family: Arial; margin: 0; padding: 0; background-color: #f2f2f2; }");
  html.concat("nav { background-color: #333; color: #fff; padding: 10px; }");
  html.concat("nav a { color: #fff; margin-right: 15px; text-decoration: none; }");
  html.concat(".container { padding: 20px; }");
  html.concat("table { width: 100%; border-collapse: collapse; }");
  html.concat("th, td { border: 1px solid #ddd; padding: 8px; }");
  html.concat("th { background-color: #333; color: white; }");
  html.concat("</style></head><body>");
  html.concat("<nav><a href='/'>Home</a><a href='/profiles'>Profiles</a><a href='/exportData'>Export Data</a></nav>");
}

void renderFooter(String& html) {
  html.concat("</body></html>");
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

// Token status callback function
void tokenStatusCallback(TokenInfo info) {
  Serial.printf("Token Info: type = %s, status = %s\n", getTokenType(info).c_str(), getTokenStatus(info).c_str());
  if (info.status == token_status_error) {
    Serial.printf("Token Error: %s\n", info.error.message.c_str());
  }
}

String getTokenType(TokenInfo info) {
  switch (info.type) {
    case token_type_undefined:
      return "undefined";
    case token_type_legacy_token:
      return "legacy token";
    case token_type_id_token:
      return "ID token";
    case token_type_custom_token:
      return "custom token";
    case token_type_oauth2_access_token:
      return "OAuth2 access token";
    default:
      return "unknown";
  }
}

String getTokenStatus(TokenInfo info) {
  switch (info.status) {
    case token_status_uninitialized:
      return "uninitialized";
    case token_status_on_signing:
      return "on signing";
    case token_status_on_request:
      return "on request";
    case token_status_on_refresh:
      return "on refresh";
    case token_status_ready:
      return "ready";
    case token_status_error:
      return "error";
    default:
      return "unknown";
  }
}
