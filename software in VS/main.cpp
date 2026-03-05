#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <SensirionI2cSht4x.h> 
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "LittleFS.h"
#include "time.h"
#include <Preferences.h>

// Definicje pinów dla Beetle ESP32-C3 / DevKit
#define I2C_SDA 2 
#define I2C_SCL 3 
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Globalne zmienne
const char* ssid = "";
const char* password = "";

float temp, hum;
float calTemp;
float p1r, p1v, p2r, p2v, p3r, p3v;

Preferences prefs;
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
SensirionI2cSht4x sht4x; 
WebServer server(80);

/*
 * @brief Updates the OLED display with current environmental data.
 */
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.printf("T: %.2f%cC", calTemp, (char)247);
  display.setCursor(0, 20);
  display.printf("W: %.2f %%RH", hum);
  display.display();
}
/*
 * @brief Calibrates temperature using polynomial interpolation with Lagrange method
 * @param raw - raw value 
 * @param p1r, p2r, p3r - raw readings that were apply in a reference points
 * @param p1v, p2v, p3v - reference values that were apply in a reference points
 * @return The calibrated temperature as a float. Returns the original raw value if points are too close to each other to avoid division by zero.
*/
float getCalibrated(float raw) {

  if (abs(p1r - p2r) < 0.001 || abs(p1r - p3r) < 0.001 || abs(p2r - p3r) < 0.001) {
    return raw;
  }

  float L1 = p1v * ((raw - p2r) * (raw - p3r)) / ((p1r - p2r) * (p1r - p3r));
  float L2 = p2v * ((raw - p1r) * (raw - p3r)) / ((p2r - p1r) * (p2r - p3r));
  float L3 = p3v * ((raw - p1r) * (raw - p2r)) / ((p3r - p1r) * (p3r - p2r));

  return L1 + L2 + L3;
}

/*
 * @brief Calculates relative humidity corrected for temperature difference.
 * Uses saturation vapor pressure (Magnus formula) to adjust humidity 
 * from the sensor's raw temperature to the calibrated temperature.
 * @param rawTemp - Temperature measured by the BME280 sensor.
 * @param rawHum - Humidity measured by the BME280 sensor.
 * @param calTemp - Calibrated temperature value.
 * @return Corrected relative humidity in % (clamped to 0-100 range).
 */
float getCorrectedHumidity(float rawTemp, float rawHum, float calTemp) {

  auto getSaturationPressure = [](float t) {return 6.112 * exp((17.67 * t) / (t + 243.5));};

  float es_raw = getSaturationPressure(rawTemp);
  float es_cal = getSaturationPressure(calTemp);

  float e = (rawHum / 100.0) * es_raw;

  float correctedHum = (e / es_cal) * 100.0;

  if (correctedHum > 100.0) return 100.0;
  if (correctedHum < 0.0) return 0.0;
  
  return correctedHum;
}
/*
 * @brief Remembers calibration points even if the power will be off.  
 * @param p1r, p2r, p3r - raw readings that were apply in a reference points
 * @param p1v, p2v, p3v - reference values that were apply in a reference points
*/
void loadCalibration() {
    prefs.begin("calib", true);
    p1r = prefs.getFloat("p1r", 10.0); p1v = prefs.getFloat("p1v", 10.0);
    p2r = prefs.getFloat("p2r", 40.0); p2v = prefs.getFloat("p2v", 40.0);
    p3r = prefs.getFloat("p3r", 80.0); p3v = prefs.getFloat("p3v", 80.0);
    prefs.end();
}

void UpdateData() {
    float rawTemp = 0;
    float rawHum = 0;
    // Sensirion API: odczyt wysokiej precyzji
    uint16_t error = sht4x.measureHighPrecision(rawTemp, rawHum);
    if (!error) {
        temp = rawTemp;
        calTemp = getCalibrated(temp);
        hum = getCorrectedHumidity(temp, rawHum, calTemp);
    }
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
html += " let diff = data.calTemp - data.temp;";
html += " let diffText = (diff >= 0 ? '+' : '') + diff.toFixed(2);";
html += " document.getElementById('temp_val').innerHTML = data.calTemp.toFixed(2);";
html += " document.getElementById('temp_diff').innerHTML = '(' + diffText + ' °C)';";
html += " document.getElementById('hum_val').innerHTML = data.hum.toFixed(2);";
html += " document.getElementById('pressure_val').innerHTML = data.pressure.toFixed(2);";
html += " document.getElementById('temp_bar').style.width = (data.calTemp / 50 * 100) + '%';";
html += " document.getElementById('hum_bar').style.width = data.hum + '%';";
html += " document.getElementById('pressure_bar').style.width = ((data.pressure - 950) / 100 * 100) + '%';"; 
html += " });";
html += "}, 5000);"; 

//Calibration
html += "function saveCalibration() {";
  html += "  const form = document.getElementById('calibForm');";
  html += "  const formData = new FormData(form);";
  html += "  const params = new URLSearchParams(formData);";
  html += "  fetch('/saveCalib?' + params.toString())";
  html += "    .then(response => response.text())";
  html += "    .then(msg => {";
  html += "      alert(msg);";
  html += "      window.location.reload();";
  html += "    });";
  html += "}";
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
html += ".calib-box { margin-top: 80px; padding: 20px; background: rgba(255,255,255,0.2); border-radius: 15px; border: 1px solid white; }";
html += "</style></head>";

html += "<body><div class='container'><div class='row justify-content-center'>";
html += "<div class='col-12 text-center mb-4 text-white'><h1>Pomiary w komorze</h1></div>";

//Cards for every parameter
html += "<div class='col-md-4 mb-3'><div class='card p-4 text-center'><h5>Temperatura</h5><div class='value'><span id='temp_val'>--</span>&deg;C</div><div id='temp_diff' style='font-size: 1.2rem; color: #666;'>--</div><div class='progress mt-3'><div id='temp_bar' class='progress-bar bg-danger' style='width: 0%'></div></div></div></div>";
html += "<div class='col-md-4 mb-3'><div class='card p-4 text-center'><h5>Wilgotność</h5><div class='value'><span id='hum_val'>--</span>%</div><div class='progress mt-3'><div id='hum_bar' class='progress-bar bg-info' style='width: 0%'></div></div></div></div>";

//Buttons
html += "<div class='col-12 btn-group-custom'>";
html += "<button onclick='saveData()' class='btn btn-light btn-lg me-2 shadow'>Zapisz Punkt Pomiarowy</button>";
html += "<a href='/download' class='btn btn-outline-success btn-lg me-2 shadow'>Pobierz Excel (CSV)</a>";
html += "<button onclick='clr()' class='btn btn-danger btn-sm shadow-sm d-block mx-auto mt-5'>Wyczyść historię pomiarów</button>";
html += "</div>";

//Calibration block
html += "<div class='calib-box mb-4'><h3>Ustawienia kalibracji 3-punktowej dla temperatury</h3><form id='calibForm' class='row g-3'>";
html += "<div class='col-md-4'><h6>Punkt Niski</h6>Odczyt: <input name='p1r' class='form-control' value='"+String(p1r)+"'> Wzorzec: <input name='p1v' class='form-control' value='"+String(p1v)+"'></div>";
html += "<div class='col-md-4'><h6>Punkt Średni</h6>Odczyt: <input name='p2r' class='form-control' value='"+String(p2r)+"'> Wzorzec: <input name='p2v' class='form-control' value='"+String(p2v)+"'></div>";
html += "<div class='col-md-4'><h6>Punkt Wysoki</h6>Odczyt: <input name='p3r' class='form-control' value='"+String(p3r)+"'> Wzorzec: <input name='p3v' class='form-control' value='"+String(p3v)+"'></div>";
html += "<div class='col-12 mt-3'><button type='button' onclick='saveCalibration()' class='btn btn-success w-100'>Zastosuj i Zapisz Kalibrację</button></div>";
html += "</form></div>";

html += "</div></div></body></html>";

//Sending all date to website
server.send(200, "text/html", html);
}

/*
 * @brief Sends current sensor readings in JSON format.
 * Used by the web interface's JavaScript to update the dashboard 
 * without reloading the entire page.
 */
void handledata() {
    UpdateData();
    String json = "{\"temp\":" + String(temp, 2) + ",\"calTemp\":" + String(calTemp, 2) + ",\"hum\":" + String(hum, 2) + "}";
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

  file.printf("%s,%.2f,%.2f\n", timeStringBuff, temp, hum);
  file.close();
  server.send(200, "text/plain", "Zapisano pomiar!");
}

/*---------Setup Function--------
 * This function is run only one time at the beggining. It sets all important data like: time, baundrate and pins
 * It starts littleFS, bme and display
 * It sends information for the website
 * At the end it launch server
*/

void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);
    loadCalibration();

    if(!LittleFS.begin(true)) Serial.println("LittleFS Error");
    
    // Inicjalizacja Sensirion SHT4x z adresem 0x44
    sht4x.begin(Wire, 0x44); 
    sht4x.softReset();
    delay(10);

    if(!display.begin(0x3C, true)) Serial.println("OLED Error");
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.println("SHT45 Ready...");
    display.display();

    WiFi.begin(ssid, password);
    configTime(3600, 3600, "pool.ntp.org");
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
}
Serial.println("");
Serial.println("Połączono z WiFi!");
Serial.print("Adres IP: ");
Serial.println(WiFi.localIP());

server.on("/", handleroot);
  server.on("/readData", handledata);
  server.on("/save", handleSave);
  server.on("/saveCalib", []() {
    prefs.begin("calib", false);
    if(server.hasArg("p1r")) prefs.putFloat("p1r", server.arg("p1r").toFloat());
    if(server.hasArg("p1v")) prefs.putFloat("p1v", server.arg("p1v").toFloat());
    if(server.hasArg("p2r")) prefs.putFloat("p2r", server.arg("p2r").toFloat());
    if(server.hasArg("p2v")) prefs.putFloat("p2v", server.arg("p2v").toFloat());
    if(server.hasArg("p3r")) prefs.putFloat("p3r", server.arg("p3r").toFloat());
    if(server.hasArg("p3v")) prefs.putFloat("p3v", server.arg("p3v").toFloat());
    prefs.end();
    loadCalibration(); 
    server.send(200, "text/plain", "Kalibracja zapisana pomyślnie!");
  });
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
    updateDisplay();
    lastDisplayUpdate = currentMillis;
  }
  if (currentMillis - lastAutoSave > 60000) {
    UpdateData();
    handleSave();
    lastAutoSave = currentMillis;
  }

}
