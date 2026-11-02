#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "driver/rtc_io.h"
#include "esp_sleep.h"

#define DHTPIN 18
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t BUZZER_PIN = 33;                  
const gpio_num_t BOTON_WAKE_PIN = GPIO_NUM_4;  

const char* ssid = "SSID";
const char* cont = "CONTRASENA";

const char* SUPABASE_URL = "URL_TABLA";
const char* SUPABASE_KEY = "PUBISHABLE_KEY";

const float TEMP_UMBRAL = 24.0;                 
const unsigned long DURACION_ALERTA = 5000;     

const unsigned long INTERVALO_LECTURA = 3000;  
const unsigned long INTERVALO_ENVIO   = 3000;  

unsigned long ultimaLectura = 0;
unsigned long ultimoEnvio = 0;
bool primerEnvio = true;
char estadoEnvio = ' ';

float tC, tF, tK, tR;

bool leerTemperaturas() {
  float c = dht.readTemperature();
  if (isnan(c)) return false;

  tC = c;
  tF = c * 1.8f + 32.0f;
  tK = c + 273.15f;
  tR = c * 1.8f + 491.67f;
  return true;
}

void lcdLinea(uint8_t fila, const char* texto) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", texto);
  lcd.setCursor(0, fila);
  lcd.print(buf);
}

void mostrarError() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("No se puede");
  lcd.setCursor(0, 1);
  lcd.print("leer sensor");
}

void mostrarLCD() {
  char tmp[17], linea[17];

  snprintf(tmp, sizeof(tmp), "C:%.1f F:%.1f", tC, tF);
  snprintf(linea, sizeof(linea), "%-15s%c", tmp, estadoEnvio);
  lcd.setCursor(0, 0);
  lcd.print(linea);

  snprintf(tmp, sizeof(tmp), "K:%.1f R:%.1f", tK, tR);
  lcdLinea(1, tmp);
}

const char* nombreEstadoWiFi(wl_status_t s) {
  switch (s) {
    case WL_NO_SSID_AVAIL:   return "SSID no encontrado";
    case WL_CONNECT_FAILED:  return "Fallo al conectar";
    case WL_CONNECTION_LOST: return "Conexion perdida";
    case WL_DISCONNECTED:    return "Desconectado";
    case WL_IDLE_STATUS:     return "En espera";
    case WL_CONNECTED:       return "Conectado";
    default:                 return "Desconocido";
  }
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conectando WiFi");

  WiFi.disconnect(true);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, cont);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
  }

  wl_status_t estado = WiFi.status();
  lcd.clear();
  if (estado == WL_CONNECTED) {
    Serial.print("WiFi OK, IP: ");
    Serial.println(WiFi.localIP());
    lcd.print("WiFi conectado");
  } else {
    Serial.printf("No se pudo conectar. Estado: %s (%d)\n", nombreEstadoWiFi(estado), estado);
    WiFi.disconnect(true); 
    lcd.print("WiFi fallo");
  }
  delay(1000);
  lcd.clear();
}

int enviarASupabase(bool alarma) {
  WiFiClientSecure client;
  client.setInsecure(); 

  HTTPClient http;
  if (!http.begin(client, SUPABASE_URL)) return -1;
  http.setTimeout(10000);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Prefer", "return=minimal");

  char body[160];
  snprintf(body, sizeof(body),
           "{\"temp_c\":%.2f,\"temp_f\":%.2f,\"temp_k\":%.2f,\"temp_r\":%.2f,\"alarma\":%s}",
           tC, tF, tK, tR, alarma ? "true" : "false");

  int code = http.POST(body);
  if (code < 0) {
    Serial.printf("Error HTTP: %s\n", http.errorToString(code).c_str());
  } else if (code != 201) {
    Serial.printf("Supabase respondió %d: %s\n", code, http.getString().c_str());
  }
  http.end();
  return code;
}

void alertaYDormir() {
  unsigned long inicio = millis();
  Serial.println("¡ALERTA! Temperatura sobre el umbral");

  digitalWrite(BUZZER_PIN, HIGH);
  lcd.clear();
  char tmp[17];
  snprintf(tmp, sizeof(tmp), "ALERTA! %.1fC", tC);
  lcdLinea(0, tmp);
  lcdLinea(1, "Apagando en 5 s");

  if (WiFi.status() == WL_CONNECTED) {
    int code = enviarASupabase(true);
    Serial.printf("Envío de alerta -> código %d\n", code);
  }

  int segAnterior = -1;
  while (millis() - inicio < DURACION_ALERTA) {
    unsigned long transcurrido = millis() - inicio;

    digitalWrite(BUZZER_PIN, ((transcurrido / 250) % 2 == 0) ? HIGH : LOW);

    int restantes = (DURACION_ALERTA - transcurrido + 999) / 1000;
    if (restantes != segAnterior) {
      segAnterior = restantes;
      snprintf(tmp, sizeof(tmp), "Apagando en %d s", restantes);
      lcdLinea(1, tmp);
    }
    delay(10);
  }

  digitalWrite(BUZZER_PIN, LOW);
  gpio_hold_en((gpio_num_t)BUZZER_PIN);
  gpio_deep_sleep_hold_en();

  lcd.clear();
  lcd.noBacklight();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  rtc_gpio_pullup_en(BOTON_WAKE_PIN);
  rtc_gpio_pulldown_dis(BOTON_WAKE_PIN);
  esp_sleep_enable_ext0_wakeup(BOTON_WAKE_PIN, 0);

  Serial.println("Entrando en deep sleep...");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);

  gpio_hold_dis((gpio_num_t)BUZZER_PIN);
  gpio_deep_sleep_hold_dis();
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();

  lcd.init();
  lcd.backlight();

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Despertó por botón");
    lcd.clear();
    lcd.print("Despertando...");
    delay(800);
  }

  conectarWiFi();
}

void loop() {
  unsigned long ahora = millis();
  if (ahora - ultimaLectura < INTERVALO_LECTURA) return;
  ultimaLectura = ahora;

  if (!leerTemperaturas()) {
    mostrarError();
    return;
  }

  Serial.printf("C=%.2f F=%.2f K=%.2f R=%.2f\n", tC, tF, tK, tR);

  if (tC > TEMP_UMBRAL) {
    alertaYDormir();
  }

  if (primerEnvio || ahora - ultimoEnvio >= INTERVALO_ENVIO) {
    primerEnvio = false;
    ultimoEnvio = ahora;

    conectarWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      int code = enviarASupabase(false);
      estadoEnvio = (code == 201) ? '*' : '!';
      Serial.printf("Envío -> código %d\n", code);
    } else {
      estadoEnvio = '!';
    }
  }

  mostrarLCD();
}
