#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <FS.h>
#include <WebSocketsServer.h>

ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

ESP8266WebServer server(80);    // create a web server on port 80
WebSocketsServer webSocket(81); // create a websocket server on port 81

File fsUploadFile; // a File variable to temporarily store the received file

const char *mdnsName = "esp8266"; // Domain name for the mDNS responder

const char *ssid = "esp8266";     // The name of the Wi-Fi network that will be created
const char *password = "esp8266"; // The password required to connect to it, leave blank for an open network

const char *OTAPassword = "esp8266"; ///TODO update before using! Using a hash instead password is advised 

/*__________________________________________________________SETUP__________________________________________________________*/

void setup()
{
    Serial.begin(115200); // Start the Serial communication to send messages to the computer
    Serial.println("Booting");
    delay(10);
    Serial.println("\r\n");

    startWiFi(); // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection

    startMDNS(); // Start the mDNS responder

    startOTA(); // Start the OTA service

    startSPIFFS(); // Start the SPIFFS and list all contents

    startWebSocket(); // Start a WebSocket server

    startServer(); // Start a HTTP server with a file read handler and an upload handler
}

/*__________________________________________________________LOOP__________________________________________________________*/

bool echoCommands = true;

bool broadcastStateUpdates = true;

void loop()
{
    MDNS.update();
    ArduinoOTA.handle();   // listen for OTA events
    webSocket.loop();      // constantly check for websocket events
    server.handleClient(); // run the server
}

/*__________________________________________________________SETUP_FUNCTIONS__________________________________________________________*/

void startWiFi()
{                                // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
    WiFi.softAP(ssid, password); // Start the access point
    Serial.print("Access Point \"");
    Serial.print(ssid);
    Serial.println("\" started\r\n");

    wifiMulti.addAP("shortcut-playground", "shortplay1242"); // add Wi-Fi networks you want to connect to
    wifiMulti.addAP("Cloud 9", "protecturnuts2");
    wifiMulti.addAP("ssid_from_AP_3", "your_password_for_AP_3");

    Serial.println("Connecting");
    while (wifiMulti.run() != WL_CONNECTED && WiFi.softAPgetStationNum() < 1)
    { // Wait for the Wi-Fi to connect
        delay(250);
        Serial.print('.');
    }
    Serial.println("\r\n");
    if (WiFi.softAPgetStationNum() == 0)
    { // If the ESP is connected to an AP
        Serial.print("Connected to ");
        Serial.println(WiFi.SSID()); // Tell us what network we're connected to
        Serial.print("IP address:\t");
        Serial.print(WiFi.localIP()); // Send the IP address of the ESP8266 to the computer
    }
    else
    { // If a station is connected to the ESP SoftAP
        Serial.print("Station connected to AP");
    }
    Serial.print("\n");
}

void startMDNS()
{ // Start the mDNS responder
    if (MDNS.begin(mdnsName))
    {
        Serial.print("mDNS responder started: http://");
        Serial.print(mdnsName);
        Serial.println(".local");
    }
    else
        Serial.println("Error starting mDNS responder.");
}

void startOTA()
{ // Start the OTA service
    ArduinoOTA.setPassword(OTAPassword);
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
        {
            type = "sketch";
        }
        else
        { // U_SPIFFS
            type = "filesystem";
        }

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Starting OTA update " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\r\nUpdate successful. Restarting...");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            Serial.println("End Failed");
    });
    ArduinoOTA.begin();
    Serial.println("OTA ready\n");
}

void startSPIFFS()
{                   // Start the SPIFFS and list all contents
    SPIFFS.begin(); // Start the SPI Flash File System (SPIFFS)
    Serial.println("SPIFFS started. Contents:");
    {
        Dir dir = SPIFFS.openDir("/");
        while (dir.next())
        { // List the file system contents
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
        Serial.printf("\n");
    }
}

void startWebSocket()
{                                      // Start a WebSocket server
    webSocket.begin();                 // start the websocket server
    webSocket.onEvent(webSocketEvent); // if there's an incomming websocket message, go to function 'webSocketEvent'
    Serial.println("WebSocket server started.");
}

void startServer()
{                                             // Start a HTTP server with a file read handler and an upload handler
    server.on("/edit.html", HTTP_POST, []() { // If a POST request is sent to the /edit.html address,
        server.send(200, "text/plain", "");
    },
              handleFileUpload); // go to 'handleFileUpload'

    server.onNotFound(handleNotFound); // if someone requests any other file or page, go to function 'handleNotFound'
                                       // and check if the file exists

    server.begin(); // start the HTTP server
    Serial.println("HTTP server started.\n");

    MDNS.addService("http", "tcp", 80);
}

/*__________________________________________________________SERVER_HANDLERS__________________________________________________________*/

void handleNotFound()
{ // if the requested file or page doesn't exist, return a 404 not found error
    if (!handleFileRead(server.uri()))
    { // check if the file exists in the flash memory (SPIFFS), if so, send it
        server.send(404, "text/plain", "404: File Not Found");
    }
}

bool handleFileRead(String path)
{ // send the right file to the client (if it exists)
    Serial.println("handleFileRead: " + path);
    if (path.endsWith("/"))
        path += "index.html";                  // If a folder is requested, send the index file
    String contentType = getContentType(path); // Get the MIME type
    String pathWithGz = path + ".gz";
    if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path))
    {                                                       // If the file exists, either as a compressed archive, or normal
        if (SPIFFS.exists(pathWithGz))                      // If there's a compressed version available
            path += ".gz";                                  // Use the compressed verion
        File file = SPIFFS.open(path, "r");                 // Open the file
        size_t sent = server.streamFile(file, contentType); // Send it to the client
        file.close();                                       // Close the file again
        Serial.println(String("\tSent file: ") + path);
        return true;
    }
    Serial.println(String("\tFile Not Found: ") + path); // If the file doesn't exist, return false
    return false;
}

void handleFileUpload()
{ // upload a new file to the SPIFFS
    HTTPUpload &upload = server.upload();
    String path;
    if (upload.status == UPLOAD_FILE_START)
    {
        path = upload.filename;
        if (!path.startsWith("/"))
            path = "/" + path;
        if (!path.endsWith(".gz"))
        {                                     // The file server always prefers a compressed version of a file
            String pathWithGz = path + ".gz"; // So if an uploaded file is not compressed, the existing compressed
            if (SPIFFS.exists(pathWithGz))    // version of that file must be deleted (if it exists)
                SPIFFS.remove(pathWithGz);
        }
        Serial.print("handleFileUpload Name: ");
        Serial.println(path);
        fsUploadFile = SPIFFS.open(path, "w"); // Open the file for writing in SPIFFS (create if it doesn't exist)
        path = String();
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (fsUploadFile)
            fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (fsUploadFile)
        {                         // If the file was successfully created
            fsUploadFile.close(); // Close the file again
            Serial.print("handleFileUpload Size: ");
            Serial.println(upload.totalSize);
            server.sendHeader("Location", "/success.html"); // Redirect the client to the success page
            server.send(303);
        }
        else
        {
            server.send(500, "text/plain", "500: couldn't create file");
        }
    }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{ // When a WebSocket message is received
    switch (type)
    {
    case WStype_DISCONNECTED: // if the websocket is disconnected
        Serial.printf("[%u] Disconnected!\n", num);
        break;
    case WStype_CONNECTED:
    { // if a new websocket connection is established
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
    }
    break;
    case WStype_TEXT: // if new text data is received
        Serial.printf("[%u] get Text: %s\n", num, payload);
        if (payload[0] == 'O')
        {
            if (payload[1] == 'F')
            {
                echoCommands = false;
            }
            else
            {
                echoCommands = true;
            }
            webSocket.sendTXT(num, (String("Command echo ") + String(echoCommands ? "enabled" : "disabled")).c_str());
        }
        else if (payload[0] == 'L')
        { //Logging status enable
            //broadcastStateUpdates = !broadcastStateUpdates;
            broadcastStateUpdates = !broadcastStateUpdates;
            webSocket.sendTXT(num, (String("Broadcast state changes:") + String(broadcastStateUpdates ? "enabled" : "disabled")).c_str());
        }
        break;
    }
}

/*__________________________________________________________HELPER_FUNCTIONS__________________________________________________________*/

String formatBytes(size_t bytes)
{ // convert sizes in bytes to KB and MB
    if (bytes < 1024)
    {
        return String(bytes) + "B";
    }
    else if (bytes < (1024 * 1024))
    {
        return String(bytes / 1024.0) + "KB";
    }
    else if (bytes < (1024 * 1024 * 1024))
    {
        return String(bytes / 1024.0 / 1024.0) + "MB";
    }
}

String getContentType(String filename)
{ // determine the filetype of a given filename, based on the extension
    if (filename.endsWith(".html"))
        return "text/html";
    else if (filename.endsWith(".css"))
        return "text/css";
    else if (filename.endsWith(".js"))
        return "application/javascript";
    else if (filename.endsWith(".ico"))
        return "image/x-icon";
    else if (filename.endsWith(".gz"))
        return "application/x-gzip";
    return "text/plain";
}
