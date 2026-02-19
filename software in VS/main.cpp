#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include<Adafruit_Sensor.h>
#include <Adafruit_GFX.h>
#include<Adafruit_SH110X.h>
#include "LittleFS.h"
#include "time.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

//Global Variables
const char* ssid = "TermoPrecyzja";
const char* password = "ol!wlYjk54#daWSX234&dskLP";
float temp;
float hum;
float pressure;
float altitude;

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_BME280 bme;
WebServer server(80);

/*
 * @brief Updates the OLED display with current environmental data.
 * @param temp - Current temperature in Celsius.
 * @param hum - Current relative humidity in percentage.
 * @param pressure - Current atmospheric pressure in hPa.
 * @param altitude - Calculated altitude in meters.
 */
void updateDisplay(float temp, float hum, float pressure, float altitude) {
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.printf("T: %.2f%cC", temp, (char)247);
  display.setCursor(0, 20);
  display.printf("W: %.2f %%RH", hum);
  display.setCursor(0, 40);
  display.printf("C: %.2f hPa", pressure);
  display.setCursor(0, 57);
  display.printf("A: %.2f m", altitude);
  display.display();
}

/*--------Web function--------
 * This function provides the functionallity of website. It creates three blocks, each for different parametr with the diagram. It also creates a buttons:
   1) for saving current data,
   2) for downloading all saved data
   3) for earasing all previous data 
 * There is also a java script which enables refreshing only sensor parameters every 5 seconds on the website.
 */
void handleroot(){

String html = "<!DOCTYPE html><html lang='pl'>";

//Title
html += "<head>";
html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
html += "<title>Panel ESP32</title>";
html += "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.0/dist/css/bootstrap.min.css' rel='stylesheet'>";

//Java script
html += "<script>";
html += "setInterval(function() {";
html += " fetch('/readData').then(response => response.json()).then(data => {";
html += " document.getElementById('temp_val').innerHTML = data.temp.toFixed(2);";
html += " document.getElementById('hum_val').innerHTML = data.hum.toFixed(2);";
html += " document.getElementById('pressure_val').innerHTML = data.pressure.toFixed(2);";
html += " document.getElementById('temp_bar').style.width = (data.temp / 50 * 100) + '%';";
html += " document.getElementById('hum_bar').style.width = data.hum + '%';";
html += " document.getElementById('pressure_bar').style.width = ((data.pressure - 950) / 100 * 100) + '%';"; 
html += " });";
html += "}, 5000);"; 

//Functionality of button Save data
html += "function saveData() {";
html += "  fetch('/save').then(response => response.text()).then(msg => alert(msg));";
html += "}";

//Functionality of button Clear all data
html += "function clr() {";
html += "  if(confirm('Czy na pewno chcesz BEZPOWROTNIE usunąć wszystkie zapisane pomiary?')) {";
html += "    fetch('/clear').then(response => response.text()).then(msg => {";
html += "      alert(msg);";
html += "      window.location.reload();"; 
html += "    });";
html += "  }";
html += "}";
html += "</script>";

//Visual aspects of website
html += "<style>";
html += "body { background: linear-gradient(135deg, #74ebd5 0%, #9face6 100%); min-height: 100vh; padding: 20px; }";
html += ".card { border: none; border-radius: 20px; box-shadow: 0 10px 30px rgba(0,0,0,0.1); transition: 0.3s; }";
html += ".card:hover { transform: translateY(-5px); }";
html += ".value { font-size: 3rem; font-weight: bold; color: #333; }";
html += ".btn-group-custom { margin-top: 30px; text-align: center; }";
html += "</style></head>";

html += "<body><div class='container'><div class='row justify-content-center'>";
html += "<div class='col-12 text-center mb-4 text-white'><h1>Pomiary w komorze</h1></div>";

//Cards for every parameter
html += "<div class='col-md-4 mb-3'><div class='card p-4 text-center'><h5>Temperatura</h5><div class='value'><span id='temp_val'>--</span>&deg;C</div><div class='progress mt-3'><div id='temp_bar' class='progress-bar bg-danger' style='width: 0%'></div></div></div></div>";
html += "<div class='col-md-4 mb-3'><div class='card p-4 text-center'><h5>Wilgotność</h5><div class='value'><span id='hum_val'>--</span>%</div><div class='progress mt-3'><div id='hum_bar' class='progress-bar bg-info' style='width: 0%'></div></div></div></div>";
html += "<div class='col-md-4 mb-3'><div class='card p-4 text-center'><h5>Ciśnienie</h5><div class='value'><span id='pressure_val'>--</span>hPa</div><div class='progress mt-3'><div id='pressure_bar' class='progress-bar bg-success' style='width: 0%'></div></div></div></div>";

//Buttons
html += "<div class='col-12 btn-group-custom'>";
html += "<button onclick='saveData()' class='btn btn-warning btn-lg me-2 shadow'>Zapisz Punkt Pomiarowy</button>";
html += "<a href='/download' class='btn btn-light btn-lg me-2 shadow'>Pobierz Excel (CSV)</a>";
html += "<button onclick='clr()' class='btn btn-danger btn-sm shadow-sm d-block mx-auto mt-5'>Wyczyść historię pomiarów</button>";
html += "</div>";

html += "</div></div></body></html>";

//Sending all date to website
server.send(200, "text/html", html);
}

/*
 * @brief Updates data from BME280
 */
void UpdateData(){
  temp = bme.readTemperature();
  hum = bme.readHumidity();
  pressure = bme.readPressure()/100.0F;
  altitude = bme.readAltitude(1013.25);
}
/*
 * @brief Sends current sensor readings in JSON format.
 * Used by the web interface's JavaScript to update the dashboard 
 * without reloading the entire page.
 */
void handledata(){

  UpdateData();
  if (isnan(temp) || isnan(hum) || isnan(pressure) || isnan(altitude)) {
    server.send(500, "application/json", "{\"error\":\"Błąd odczytu\"}");
    return;
  }
  String json = "{\"temp\":" + String(temp, 2) + ",\"hum\":" + String(hum, 2) + ",\"pressure\":"+ String(pressure, 2)+ ",\"altitude\":"+ String(altitude,2)+"}";
  server.send(200, "application/json", json);
}

/*----------Function for Creating File------------
 * This function is resposible for saving data to a file named pomiary.csv. 
 * When this file doesn't exist it creates it with headers for easy identification.
 * This function also provides real time aspect, so the measurements are identify with the time.
*/
void handleSave() {

  bool fileExists = LittleFS.exists("/pomiary.csv");

  File file = LittleFS.open("/pomiary.csv", FILE_APPEND);

  if(!file) {server.send(500, "text/plain", "Blad zapisu pliku");return;}
  if(!fileExists || file.size() == 0) {file.println("Data i Godzina,Temperatura [C],Wilgotnosc [%],Cisnienie [hPa]");}

  struct tm timeinfo;
  char timeStringBuff[20] = "Brak czasu";

  if(getLocalTime(&timeinfo)) {strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);}

  file.printf("%s,%.2f,%.2f,%.2f\n", timeStringBuff, temp, hum, pressure);
  file.close();
  server.send(200, "text/plain", "Zapisano pomiar!");
}

/*---------Setup Function--------
 * This function is run only one time at the beggining. It set all important data like: time, baundrate and pins
 * It starts littleFS, bme and display
 * It sends information for the website
 * At the end it launch server
*/
void setup(){
  configTime(3600, 3600, "pool.ntp.org");
  Serial.begin(115200);
  Wire.begin(22, 21);
  
  if(!LittleFS.begin(true)) { Serial.println("Błąd LittleFS"); }
  if(!bme.begin(0x76)){ Serial.println("Nie znaleziono BME280");}
  if(!display.begin(0x3C,true)){Serial.println("OLED błąd!");}

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0,0);
  display.println("Laczenie WiFi...");
  display.display();

  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {delay(500);}

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nPołączono!");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Brak WiFi - start w trybie offline");
  }

  server.on("/", handleroot);
  server.on("/readData", handledata);
  server.on("/save", handleSave);
  server.on("/download", []() {
    if (!LittleFS.exists("/pomiary.csv")) {
        server.send(404, "text/plain", "Brak pliku. Zapisz najpierw jakis pomiar.");
        return;
    }
    File f = LittleFS.open("/pomiary.csv", FILE_READ);
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Content-Disposition", "attachment; filename=pomiary.csv");
    server.streamFile(f, "text/csv");
    f.close();
  });
  server.on("/clear", []() {
    LittleFS.remove("/pomiary.csv");
    File f = LittleFS.open("/pomiary.csv", FILE_WRITE);
    if(f) {
        f.println("Data i Godzina,Temperatura [C],Wilgotnosc [%],Cisnienie [hPa]");
        f.close();
        server.send(200, "text/plain", "Historia wyczyszczona. Nowy plik gotowy.");
    } else {
        server.send(500, "text/plain", "Blad tworzenia nowego pliku.");
    }
  });
  server.begin();
}

/*
 * @brief Updates display and data every two seconds 
 * @brief Saves data every one minute
 */
void loop(){
  server.handleClient();
  
  static unsigned long lastSensorUpdate = 0;
  static unsigned long lastDisplayUpdate = 0;
  static unsigned long lastAutoSave = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastSensorUpdate > 2000) {
    UpdateData();
    lastSensorUpdate = currentMillis;
  }
  if (currentMillis - lastDisplayUpdate > 2000) {
    updateDisplay(temp, hum, pressure, altitude);
    lastDisplayUpdate = currentMillis;
  }
  if (currentMillis - lastAutoSave > 60000) {
    UpdateData();
    handleSave();
    lastAutoSave = currentMillis;
  }
}

