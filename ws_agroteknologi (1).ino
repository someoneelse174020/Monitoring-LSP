/*
  ===========================================================
  WS AGROTEKNOLOGI IoT — Monitoring & Kontrol Relay Otomatis
  Gabungan percobaan1 (logic relay) + percobaan2 (WiFi/ThingSpeak)
  + tambahan LittleFS supaya dashboard di-hosting oleh ESP32 sendiri
  ===========================================================

  BUG YANG DIPERBAIKI DARI FILE ASLI:
  1. Logic relay1 di percobaan1 kebalik (nyalain pakai HIGH padahal
     modulnya aktif-LOW sesuai setup()) -> sekarang dibenerin jadi LOW=nyala.
  2. Mapping soil moisture salah rentang (2048 -> harusnya 4095 utk ESP32
     yang ADC-nya 12-bit) -> sekarang pakai 4095.
  3. lcd.begin() diganti lcd.init() -> sesuai library LiquidCrystal_I2C
     yang dipakai (lcd.begin() bisa gagal compile di sebagian versi).
  4. Semua #define pin yang kosong sudah diisi (SESUAIKAN lagi dengan
     kabel fisik di panel lo kalau ternyata beda!).
  5. delay(1000) blocking dihapus, semua pakai elapsedMillis supaya
     WebServer (LittleFS) bisa tetap responsif.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ThingSpeak.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <LiquidCrystal_I2C.h>
#include <elapsedMillis.h>
#include <LittleFS.h>

// ===================== PIN (SESUAIKAN DENGAN WIRING FISIK LO) =====================
#define DHTPIN 25
#define DHTTYPE DHT21
#define RELAY1_PIN 14   // relay 1 -> kipas/pendingin, ikut suhu & kelembapan
#define RELAY2_PIN 17   // relay 2 -> belum ada kondisi otomatis dari file asli, standby
#define RELAY3_PIN 26   // relay 3 -> pompa air, ikut kelembapan tanah
const int soilPin = 34;

// ===================== WIFI =====================
const char* ssid = "UGMURO-INET";
const char* password = "Gepuk15000";

// ===================== THINGSPEAK =====================
const char* writeAPIKey = "GD6UJDLXW1LJJFIS";
const unsigned long channelID = 2604635;
WiFiClient client;
// field1 = suhu, field2 = kelembapan udara, field3 = kelembapan tanah,
// field4 = status relay1 (0/1), field5 = status relay3 (0/1)
// -> field4 & field5 cuma buat DITAMPILKAN di dashboard, bukan dikontrol
//    dari web (karena relay-nya otomatis, bukan manual)

// ===================== WEB SERVER (buat hosting dashboard dari LittleFS) =====================
WebServer server(80);

// ===================== SENSOR & LCD =====================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ===================== TIMER NON-BLOCKING =====================
elapsedMillis thingSpeakMillis;
elapsedMillis sensorMillis;
elapsedMillis displayMillis;
unsigned long thingSpeakInterval = 20000; // 20 detik, aman dari rate limit ThingSpeak
unsigned long sensorInterval = 2000;      // DHT21 butuh jeda min ~2 detik antar baca
unsigned long displayInterval = 1000;

// ===================== DATA =====================
float temperature = 0;
float humidity = 0;
int soilValue = 0;
int soilPercentage = 0;
int relay1Status = 0; // 0 = mati, 1 = nyala (buat dikirim ke ThingSpeak)
int relay3Status = 0;

void setup() {
  Serial.begin(115200);

  // ---------- LCD ----------
  lcd.init();
  lcd.backlight();
  lcd.setCursor(3, 0);
  lcd.print("Selamat Datang!");
  lcd.setCursor(0, 1);
  lcd.print("WS Agroteknologi IoT");
  lcd.setCursor(3, 3);
  lcd.print("-- UG MURO --");
  delay(3000);
  lcd.clear();

  // ---------- SENSOR & RELAY PIN ----------
  pinMode(soilPin, INPUT);
  dht.begin();
  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  pinMode(RELAY3_PIN, OUTPUT);
  // modul relay aktif-LOW, jadi HIGH = posisi aman/mati di awal
  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);
  digitalWrite(RELAY3_PIN, HIGH);

  // ---------- MOUNT LITTLEFS ----------
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount FAILED");
  } else {
    Serial.println("LittleFS Mounted OK");
  }

  // ---------- WIFI ----------
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");
  Serial.print(">> Buka dashboard di browser: http://");
  Serial.println(WiFi.localIP());

  ThingSpeak.begin(client);

  // ---------- ROUTING WEB SERVER KE FILE DI LITTLEFS ----------
  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/index.html", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css");
  server.serveStatic("/script.js", LittleFS, "/script.js");
  server.onNotFound([]() {
    server.send(404, "text/plain", "File tidak ditemukan di LittleFS");
  });
  server.begin();

  // ---------- HEADER LCD (statis, gak perlu ditulis ulang tiap loop) ----------
  lcd.setCursor(5, 0);
  lcd.print("Monitoring");
  lcd.setCursor(0, 1);
  lcd.print("Suhu   : ");
  lcd.setCursor(0, 2);
  lcd.print("K.Udara: ");
  lcd.setCursor(0, 3);
  lcd.print("K.Tanah: ");
}

void loop() {
  server.handleClient(); // wajib dipanggil terus biar dashboard responsif

  // ---------- BACA SENSOR ----------
  if (sensorMillis >= sensorInterval) {
    float nt = dht.readTemperature();
    float nh = dht.readHumidity();
    if (!isnan(nt) && !isnan(nh)) {
      temperature = nt;
      humidity = nh;
    }

    soilValue = analogRead(soilPin);
    soilPercentage = map(soilValue, 4095, 0, 0, 100); // FIX: rentang 4095 utk ESP32
    soilPercentage = constrain(soilPercentage, 0, 100);

    Serial.print("Suhu: "); Serial.print(temperature);
    Serial.print(" C\tKelembapan: "); Serial.print(humidity);
    Serial.print(" %\tTanah: "); Serial.print(soilPercentage);
    Serial.println(" %");

    sensorMillis = 0;
  }

  // ---------- LOGIKA RELAY OTOMATIS (FIX: polaritas dibenerin) ----------
  // Relay1: nyala kalau suhu > 24°C DAN kelembapan udara > 70%
  if (temperature > 24 && humidity > 70) {
    digitalWrite(RELAY1_PIN, LOW);  // LOW = nyala (aktif-LOW)
    relay1Status = 1;
  } else {
    digitalWrite(RELAY1_PIN, HIGH); // HIGH = mati
    relay1Status = 0;
  }

  // Relay3: nyala (pompa) kalau tanah kering, di bawah 30%
  if (soilPercentage < 30) {
    digitalWrite(RELAY3_PIN, LOW);
    relay3Status = 1;
  } else {
    digitalWrite(RELAY3_PIN, HIGH);
    relay3Status = 0;
  }
  // Relay2 belum ada kondisi otomatis di file asli lo, jadi dibiarkan mati/standby

  // ---------- UPDATE LCD ----------
  if (displayMillis >= displayInterval) {
    lcd.setCursor(9, 1);
    lcd.print("       "); // bersihin sisa karakter lama
    lcd.setCursor(9, 1);
    lcd.print(temperature);
    lcd.setCursor(16, 1);
    lcd.print(char(223)); // simbol derajat
    lcd.print("C");

    lcd.setCursor(9, 2);
    lcd.print("       ");
    lcd.setCursor(9, 2);
    lcd.print(humidity);
    lcd.setCursor(17, 2);
    lcd.print("%");

    lcd.setCursor(9, 3);
    lcd.print("       ");
    lcd.setCursor(9, 3);
    lcd.print(soilPercentage);
    lcd.setCursor(17, 3);
    lcd.print("%");

    displayMillis = 0;
  }

  // ---------- KIRIM DATA KE THINGSPEAK ----------
  if (thingSpeakMillis >= thingSpeakInterval) {
    ThingSpeak.setField(1, temperature);
    ThingSpeak.setField(2, humidity);
    ThingSpeak.setField(3, soilPercentage);
    ThingSpeak.setField(4, relay1Status);
    ThingSpeak.setField(5, relay3Status);

    int x = ThingSpeak.writeFields(channelID, writeAPIKey);
    Serial.println(x == 200 ? "Update successful." : "Update failed. HTTP code: " + String(x));

    thingSpeakMillis = 0;
  }
}
