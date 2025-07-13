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
#define LDR_PIN     34 // Pin untuk sensor LDR (ADC1)

// --- INISIALISASI OBJEK HARDWARE ---
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;

// --- VARIABEL GLOBAL & KONSTANTA ---
unsigned long lastMovementTime = 0;
const long NO_MOVEMENT_LOGOUT_TIMEOUT = 10000; // Auto-logout setelah 10 detik tanpa gerakan
bool isDoorLocked = true;
String lastUserUnlockedUID = "";
String lastUserUnlockedName = "";
const int LDR_THRESHOLD = 1500; // Nilai ambang batas gelap/terang (WAJIB DIKALIBRASI)

// --- POSISI SUDUT SERVO ---
const int lockedPosition = 0;
const int unlockedPosition = 90;

// --- FUNGSI UNTUK MENGIRIM LOG KE REALTIME DATABASE ---
void sendLogToRTDB(String status, String action, String user, String reason, String uid) {
  if (Firebase.ready()) {
    String path = "/access_logs";
    FirebaseJson content;
    content.set("status", status);
    if (action != "") content.set("action", action);
    if (user != "") content.set("user", user);
    if (reason != "") content.set("reason", reason);
    if (uid != "") content.set("uid", uid);

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
  if (Firebase.RTDB.getString(&fbdo, path.c_str()) && fbdo.dataType() == "string") {
    userName = fbdo.stringData();
    Serial.println("Nama Pengguna: " + userName);
    return true; 
  }
  Serial.println("Pengguna tidak ditemukan atau terjadi error: " + fbdo.errorReason());
  userName = "Unknown Card";
  return false;
}

// --- FUNGSI UNTUK KONEKSI WIFI ---
void setup_wifi() {
  delay(10);
  Serial.println("\nMenghubungkan ke WiFi: " + String(ssid));
  lcd.clear();
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("WiFi...");
  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries++ < 20) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nGagal terhubung ke WiFi!");
    lcd.clear();
    lcd.print("WiFi Failed!");
    delay(2000);
    return;
  }
  Serial.println("\nWiFi terhubung! IP: " + WiFi.localIP().toString());
}

// --- FUNGSI AUTO LOCK / LOGOUT ---
void autoLockDoor() {
  myServo.write(lockedPosition);
  isDoorLocked = true;
  digitalWrite(LED_PIN, LOW);
  lastMovementTime = 0;
  lastUserUnlockedUID = "";
  lastUserUnlockedName = "";

  Serial.println("Tidak ada gerakan. Auto-logout/lock diaktifkan.");
  lcd.clear();
  lcd.print("No Movement...");
  lcd.setCursor(0, 1);
  lcd.print("Auto-Locked");
  delay(2000);

  sendLogToRTDB("AutoLocked", "Locked", "", "No movement timeout", "");

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
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  myServo.attach(SERVO_PIN);
  myServo.write(lockedPosition);
  digitalWrite(LED_PIN, LOW);

  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1000);

  setup_wifi();

  lcd.clear();
  lcd.print("Connecting to");
  lcd.setCursor(0,1);
  lcd.print("Firebase...");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = nullptr;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

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
    Serial.println("RFID UID Detected: " + rfidUID);

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
        // === PERUBAHAN: Mulai timer auto-logout saat pintu dibuka ===
        lastMovementTime = millis(); 

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
          lastUserUnlockedUID = "";
          lastUserUnlockedName = "";
          Serial.println("Door Locked by " + detectedUserName);
          lcd.print("Door Locked.");
          delay(1500);
          sendLogToRTDB("Granted", "Locked", detectedUserName, "", rfidUID);
        } else {
          // ... (logika jika kartu salah mencoba mengunci)
        }
      }
    } else {
      // ... (logika jika akses ditolak)
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    lcd.clear();
    lcd.print("Smart Door Lock");
    lcd.setCursor(0, 1);
    lcd.print(isDoorLocked ? "Locked" : "Unlocked");
  }

  // === BLOK LOGIKA BARU (Hanya berjalan saat pintu tidak terkunci) ===
  if (!isDoorLocked) {
    // 1. Logika Sensor Cahaya (LDR) untuk kontrol LED
    int ldrValue = analogRead(LDR_PIN);
    if (ldrValue < LDR_THRESHOLD) {
      digitalWrite(LED_PIN, HIGH); // Gelap, nyalakan LED
    } else {
      digitalWrite(LED_PIN, LOW); // Terang, matikan LED
    }

    // 2. Logika Sensor Gerak (Ultrasonik) untuk me-reset timer
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 15000); 
    if (duration > 120) {
      long distance = duration * 0.034 / 2;
      if (distance < 150) {
        Serial.println("Movement detected! Resetting auto-logout timer.");
        lastMovementTime = millis(); // Reset timer jika ada gerakan
      }
    }

    // 3. Logika Timeout untuk Auto-Logout
    if (millis() - lastMovementTime > NO_MOVEMENT_LOGOUT_TIMEOUT) {
      autoLockDoor();
    }
  }

  delay(50);
}
