#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// --- ตั้งค่า WiFi และ Firebase ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";

String FIREBASE_HOST = "https://smart-parking-2d37c-default-rtdb.asia-southeast1.firebasedatabase.app";


int trigPins[] = {5, 17, 4, 15};
int echoPins[] = {18, 16, 2, 13};
int redLeds[] = {32, 33, 25, 26}; 
int greenLeds[] = {27, 14, 12, 19}; 

float threshold = 15.0; // ระยะเซนติเมตรที่จะถือว่ามีรถจอด
bool lastStates[4] = {false, false, false, false}; // เก็บสถานะก่อนหน้าเพื่อลดการส่งข้อมูลซ้ำ

WiFiClientSecure client;
HTTPClient http;

// ฟังก์ชันอ่านค่าระยะทางแบบเฉลี่ยเพื่อความนิ่ง (Stable Read)
float readStableDistance(int trig, int echo) {
  float sum = 0;
  int samples = 3;
  for (int i = 0; i < samples; i++) {
    digitalWrite(trig, LOW); 
    delayMicroseconds(2);
    digitalWrite(trig, HIGH); 
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    
    // pulseIn จะคืนค่าเป็น microsecond (Timeout 30ms)
    long duration = pulseIn(echo, HIGH, 30000); 
    
    // ถ้าวัดไม่ได้ (0) ให้ตีค่าเป็นระยะไกลมาก
    float distance = (duration == 0) ? 999.0 : duration * 0.034 / 2;
    sum += distance;
    delay(20); 
  }
  return sum / samples;
}

void setup() {
  Serial.begin(115200);
  
  // Mode ของพิน
  for (int i = 0; i < 4; i++) {
    pinMode(trigPins[i], OUTPUT); 
    pinMode(echoPins[i], INPUT);
    pinMode(redLeds[i], OUTPUT); 
    pinMode(greenLeds[i], OUTPUT);
    
    // เริ่มต้นให้ไฟเขียวติด (สถานะว่าง)
    digitalWrite(greenLeds[i], HIGH);
    digitalWrite(redLeds[i], LOW);
  }

  // เชื่อมต่อ WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  
  client.setInsecure(); // เชื่อมต่อ Firebase แบบ HTTPS 
  Serial.println("\nWiFi Connected!");
  Serial.println("Smart Parking System Ready!");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected. Reconnecting...");
    WiFi.begin(ssid, password);
    delay(5000);
    return;
  }

  bool hasChanged = false;
  String json = "{";

  // อ่านค่าจากเซนเซอร์ทั้ง 4 ช่อง
  for (int i = 0; i < 4; i++) {
    float d = readStableDistance(trigPins[i], echoPins[i]);
    bool isOccupied = (d < threshold);
    
    // สลับสถานะไฟ LED ตามการตรวจจับ
    if (isOccupied) {
      digitalWrite(redLeds[i], HIGH);
      digitalWrite(greenLeds[i], LOW);
    } else {
      digitalWrite(redLeds[i], LOW);
      digitalWrite(greenLeds[i], HIGH);
    }

    // เช็คว่าสถานะเปลี่ยนจากครั้งก่อนหรือไม่ (เพื่อลดการส่ง HTTP บ่อยเกินไป)
    if (isOccupied != lastStates[i]) {
      hasChanged = true;
      lastStates[i] = isOccupied;
    }

    // สร้าง JSON Payload
    json += "\"slot" + String(i + 1) + "\":{";
    json += "\"status\":\"" + String(isOccupied ? "occupied" : "vacant") + "\",";
    json += "\"distance\":" + String(d);
    json += "}";
    if (i < 3) json += ",";
  }
  json += "}";

  // ส่งข้อมูลไปยัง Firebase เฉพาะเมื่อมีการเปลี่ยนแปลงสถานะ
  // หรือจะลบเงื่อนไข hasChanged ออกหากต้องการให้ส่งทุกๆ 2 วินาทีตลอดเวลา
  if (hasChanged) {
    http.begin(client, FIREBASE_HOST + "/slots.json");
    http.addHeader("Content-Type", "application/json");
    
    int httpCode = http.PUT(json); // ใช้ PUT เพื่อเขียนทับข้อมูลเดิมใน path /slots
    
    if (httpCode > 0) {
      Serial.printf("Update Success! HTTP Code: %d\n", httpCode);
    } else {
      Serial.printf("Update Failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }

  delay(2000); // หน่วงเวลา 2 วินาทีก่อนเริ่มรอบถัดไป
}
