// --- PUSTAKA/LIBRARY ---
#include <WiFi.h>
#include <MFRC522.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <Firebase_ESP_Client.h>

// --- PENGATURAN JARINGAN ---
const char* ssid = "Infinix11";
const char* password = "12345678";

// --- PENGATURAN FIREBASE (WAJIB DIISI) ---
#define API_KEY 
#define FIREBASE_PROJECT_ID 
#define USER_EMAIL 
#define USER_PASSWORD 
#define DATABASE_URL 
// --- INISIALISASI OBJEK FIREBASE ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- DEFINISI PIN HARDWARE ---
#define SS_PIN      5
#define RST_PIN     4
#define BUZZER_PIN  33
#define LED_PIN     32
#define ECHO_PIN    27
#define TRIG_PIN    14
#define SERVO_PIN   13

// --- INISIALISASI OBJEK HARDWARE ---
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;

// --- VARIABEL GLOBAL ---
unsigned long lastMovementTime = 0;
unsigned long doorOpenedTime = 0;
const long LED_TIMEOUT = 10000;
const long AUTOLOCK_TIMEOUT = 10000;
bool isDoorLocked = true;
String lastUserUnlockedUID = "";
String lastUserUnlockedName = "";

// --- POSISI SUDUT SERVO ---
const int lockedPosition = 0;
const int unlockedPosition = 90;

// --- FUNGSI UNTUK MENGIRIM LOG KE REALTIME DATABASE ---
void sendLogToRTDB(String status, String action, String user, String reason, String uid) {
  if (Firebase.ready()) {
    String path = "/access_logs"; // Path utama untuk semua log
    
    FirebaseJson content;
    content.set("status", status);
    // content.set("log_time", ".sv", "timestamp"); // <-- BARIS INI DIHAPUS
    if (action != "") content.set("action", action);
    if (user != "") content.set("user", user);
    if (reason != "") content.set("reason", reason);
    if (uid != "") content.set("uid", uid);

    // pushJSON akan membuat ID unik untuk setiap log baru
    if (Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &content)) {
      Serial.println("Log berhasil dikirim ke Realtime Database.");
    } else {
      Serial.println("Gagal mengirim log: " + fbdo.errorReason());
    }
  }
}

// --- FUNGSI VERIFIKASI PENGGUNA DARI REALTIME DATABASE ---
bool verifyUserFromRTDB(String rfidUID, String &userName) {
  if (!Firebase.ready()) {
    Serial.println("Firebase belum siap, verifikasi dibatalkan.");
    return false;
  }

  String path = "/auth_users/" + rfidUID + "/name";
  Serial.println("Mengecek UID ke RTDB: " + path);
  
  if (Firebase.RTDB.getString(&fbdo, path.c_str())) {
    if (fbdo.dataType() == "string") {
      userName = fbdo.stringData();
      Serial.println("Nama Pengguna: " + userName);
      return true; 
    }
  }
  
  Serial.println("Pengguna tidak ditemukan atau terjadi error.");
  Serial.println("ALASAN: " + fbdo.errorReason());
  userName = "Unknown Card";
  return false;
}


// --- FUNGSI UNTUK KONEKSI WIFI ---
void setup_wifi() {
  delay(10);
  Serial.println("\n");
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(ssid);

  lcd.clear();
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("WiFi...");

  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if(retries++ > 20) {
        Serial.println("\nFailed to connect to WiFi!");
        lcd.clear();
        lcd.print("WiFi Failed!");
        delay(2000);
        return;
    }
  }

  Serial.println("\nWiFi terhubung!");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());
}

// --- FUNGSI AUTO LOCK ---
void autoLockDoor() {
  myServo.write(lockedPosition);
  isDoorLocked = true;
  digitalWrite(LED_PIN, LOW);
  lastMovementTime = 0;
  doorOpenedTime = 0;
  lastUserUnlockedUID = "";
  lastUserUnlockedName = "";

  Serial.println("No movement. Auto-lock activated.");
  lcd.clear();
  lcd.print("No Movement...");
  lcd.setCursor(0, 1);
  lcd.print("Auto-Locked");
  delay(2000);

  sendLogToRTDB("AutoLocked", "Locked", "", "No movement", "");

  lcd.clear();
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Locked");
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1000);

  setup_wifi();

  lcd.clear();
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("Firebase...");

  // --- Konfigurasi Firebase untuk Realtime Database ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = nullptr;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  myServo.attach(SERVO_PIN);
  myServo.write(lockedPosition);
  digitalWrite(LED_PIN, LOW);
  
  lcd.clear();
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Locked");
  
  sendLogToRTDB("Online", "System Boot", "", "", "");
}

// --- LOOP UTAMA ---
void loop() {
  // --- RFID ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      rfidUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      rfidUID += String(rfid.uid.uidByte[i], HEX);
    }
    rfidUID.toUpperCase();
    Serial.print("RFID UID Detected: ");
    Serial.println(rfidUID);

    String detectedUserName;
    bool accessGranted = verifyUserFromRTDB(rfidUID, detectedUserName);

    if (accessGranted) {
      lcd.clear();
      tone(BUZZER_PIN, 1000, 200);

      if (isDoorLocked) {
        myServo.write(unlockedPosition);
        isDoorLocked = false;
        lastUserUnlockedUID = rfidUID;
        lastUserUnlockedName = detectedUserName;
        doorOpenedTime = millis();

        Serial.println("Access Granted: Door Unlocked by " + detectedUserName);
        lcd.print("Welcome,");
        lcd.setCursor(0, 1);
        lcd.print(detectedUserName);
        delay(2000);

        sendLogToRTDB("Granted", "Unlocked", detectedUserName, "", rfidUID);

      } else {
        if (rfidUID == lastUserUnlockedUID) {
          myServo.write(lockedPosition);
          isDoorLocked = true;
          digitalWrite(LED_PIN, LOW);
          lastMovementTime = 0;
          doorOpenedTime = 0;
          lastUserUnlockedUID = "";
          lastUserUnlockedName = "";

          Serial.println("Access Granted: Door Locked by " + detectedUserName);
          lcd.print("Door Locked.");
          lcd.setCursor(0, 1);
          lcd.print("Goodbye!");
          delay(1500);

          sendLogToRTDB("Granted", "Locked", detectedUserName, "", rfidUID);
        } else {
          Serial.println("Access Denied! Not the last user (" + detectedUserName + ") to unlock.");
          lcd.print("Access Denied!");
          lcd.setCursor(0, 1);
          lcd.print("Not last user");
          tone(BUZZER_PIN, 500, 500);
          delay(2000);
          
          sendLogToRTDB("Denied", "Lock Attempt", detectedUserName, "Not last user", rfidUID);
        }
      }
    } else {
      Serial.println("Access Denied! Unknown Card.");
      lcd.clear();
      lcd.print("Access Denied!");
      lcd.setCursor(0, 1);
      lcd.print("Unknown Card.");
      tone(BUZZER_PIN, 500, 500);
      delay(2000);

      sendLogToRTDB("Denied", "Access Attempt", "Unknown", "Unknown Card", rfidUID);
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    lcd.clear();
    lcd.print("Smart Door Lock");
    lcd.setCursor(0, 1);
    lcd.print(isDoorLocked ? "Locked" : "Unlocked");
  }

  // --- Sensor Ultrasonik (tidak ada perubahan) ---
  if (!isDoorLocked) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration > 0) {
      long distance = duration * 0.034 / 2;
      if (distance > 2 && distance < 150) {
        if (digitalRead(LED_PIN) == LOW) {
          digitalWrite(LED_PIN, HIGH);
          Serial.println("Movement detected! LED ON.");
        }
        lastMovementTime = millis();
      }
    }

    if (digitalRead(LED_PIN) == HIGH && (millis() - lastMovementTime > LED_TIMEOUT)) {
      digitalWrite(LED_PIN, LOW);
      Serial.println("No movement for 10 seconds. LED OFF.");
    }

    if ((millis() - doorOpenedTime > AUTOLOCK_TIMEOUT) && (millis() - lastMovementTime > AUTOLOCK_TIMEOUT)) {
      autoLockDoor();
    }
  }

  delay(50);
}
