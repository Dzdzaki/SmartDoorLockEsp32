#include <MFRC522.h> // Untuk RFID
#include <SPI.h>     // Untuk komunikasi SPI dengan RFID
#include <LiquidCrystal_I2C.h> // Untuk LCD I2C
#include <ESP32Servo.h>  // Untuk Servo (pastikan Anda menginstal library ini untuk ESP32)

// Definisikan pin-pin hardware
#define SS_PIN      5 // SDA pin for RFID (GPIO5) ungu // MOSI: GPIO 23 cokelat// MISO: GPIO 19 kuing
#define RST_PIN     4 // RST pin for RFID (GPIO4) putih
#define BUZZER_PIN  33 // GPIO33 putih
#define LED_PIN     32 // GPIO32 putih 
#define ECHO_PIN    27 // GPIO12 hijau
#define TRIG_PIN    14 // GPIO14 kuning
#define SERVO_PIN   13 // GPIO13 kuning



// Inisialisasi objek
MFRC522 rfid(SS_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2); // Alamat I2C LCD (sesuaikan jika berbeda, umum 0x27 atau 0x3F), 16 kolom, 2 baris
Servo myServo;

// Variabel global
unsigned long lastMovementTime = 0;
const long LED_TIMEOUT = 1000; // 1 menit dalam milidetik (60 * 1000)
bool isDoorLocked = true; // Status kunci pintu

// Array untuk UID yang diizinkan dan nama pengguna
String authorizedUIDs[] = {
  "695CB101", // UID Kartu 1
  "D709C905", // UID Kartu 2
  "436C3916", // UID Kartu 3
  "CAEF2F03"  // UID Kartu 4
  
};
String userNames[] = {
  "Kelas Tcg2a",
  "Kelas Tcg1b",
  "Kelas Tcg2b",
  "Kelas Tcg1a"
};

// Menghitung jumlah elemen dalam array secara dinamis
int numberOfUIDs = sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]);

// Posisi servo
const int lockedPosition = 0;   // Sudut untuk mengunci pintu
const int unlockedPosition = 90; // Sudut untuk membuka pintu (sesuaikan)
// --- Fungsi Debug Servo ---
void servoDebug() {
  Serial.println("\n--- Starting Servo Debug Test ---");
  lcd.clear();
  lcd.print("Servo Test...");
  lcd.setCursor(0, 1);
  lcd.print("Locked Pos");

  myServo.write(lockedPosition); // Posisikan servo ke posisi terkunci
  delay(1500); // Tunggu sebentar

  Serial.println("Servo moving to Unlocked Position...");
  lcd.clear();
  lcd.print("Servo Test...");
  lcd.setCursor(0, 1);
  lcd.print("Unlocked Pos");

  myServo.write(unlockedPosition); // Posisikan servo ke posisi terbuka
  delay(1500); // Tunggu sebentar

  Serial.println("Servo moving back to Locked Position...");
  lcd.clear();
  lcd.print("Servo Test...");
  lcd.setCursor(0, 1);
  lcd.print("Locked Pos");

  myServo.write(lockedPosition); // Posisikan servo kembali ke terkunci
  delay(1500); // Tunggu sebentar

  Serial.println("--- Servo Debug Test Complete ---");
  lcd.clear();
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Ready.");
  delay(1000); // Beri waktu untuk pesan "Ready"
}

void setup() {
  Serial.begin(115200);
  SPI.begin();       // Inisialisasi SPI bus
  rfid.PCD_Init();   // Inisialisasi modul MFRC522

  lcd.init();        // Inisialisasi LCD
  lcd.backlight();   // Nyalakan backlight LCD
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1000); // Beri waktu untuk LCD menampilkan pesan

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  myServo.attach(SERVO_PIN);
  myServo.write(lockedPosition); // Pastikan servo di posisi terkunci di awal
  digitalWrite(LED_PIN, LOW);    // Pastikan LED mati di awal

  lcd.clear();
  lcd.print("Smart Door Lock");
  lcd.setCursor(0, 1);
  lcd.print("Locked"); // Tampilkan status awal pintu
}

void loop() {
  // --- RFID Reader ---
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String rfidUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      rfidUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : ""); // Tambahkan '0' di depan jika nilai < 0x10
      rfidUID += String(rfid.uid.uidByte[i], HEX);              // Konversi byte ke heksadesimal
    }
    rfidUID.toUpperCase(); // Ubah ke huruf besar untuk konsistensi
    Serial.print("RFID UID: ");
    Serial.println(rfidUID);

    bool accessGranted = false;
    String detectedUserName = "Unknown Card"; // Default nama jika tidak ditemukan

    // Cek apakah UID yang dibaca ada dalam daftar yang diizinkan
    for (int i = 0; i < numberOfUIDs; i++) {
      if (rfidUID == authorizedUIDs[i]) {
        accessGranted = true;
        detectedUserName = userNames[i];
        break; // Keluar dari loop setelah menemukan kecocokan
      }
    }

    if (accessGranted) {
      Serial.println("Access Granted!");
      lcd.clear();
      lcd.print("Welcome,");
      lcd.setCursor(0, 1);
      lcd.print(detectedUserName); // Tampilkan nama pengguna yang terdeteksi
      tone(BUZZER_PIN, 1000, 200); // Bunyi 'beep' untuk akses granted
      delay(2000); // Tampilkan pesan selamat datang sebentar

      // Logika Buka/Kunci Pintu
      if (isDoorLocked) {
        myServo.write(unlockedPosition); // Gerakkan servo untuk membuka kunci
        delay(500); // Beri waktu servo untuk bergerak
        isDoorLocked = false;
        Serial.println("Door Unlocked.");
      } else {
        // Tapout - kunci pintu kembali
        myServo.write(lockedPosition); // Gerakkan servo untuk mengunci
        delay(500); // Beri waktu servo untuk bergerak
        isDoorLocked = true;
        digitalWrite(LED_PIN, LOW); // Matikan LED saat tapout/kunci
        lastMovementTime = 0; // Reset timer pergerakan saat pintu terkunci
        Serial.println("Door Locked.");

        lcd.clear();
        lcd.print("Door Locked.");
        lcd.setCursor(0, 1);
        lcd.print("Goodbye!");
        delay(1500);
      }
    } else {
      Serial.println("Access Denied!");
      lcd.clear();
      lcd.print("Access Denied!");
      lcd.setCursor(0, 1);
      lcd.print("Unknown Card.");
      tone(BUZZER_PIN, 500, 500); // Bunyi 'buzz' untuk akses denied
      delay(2000);
    }
    rfid.PICC_HaltA(); // Hentikan komunikasi dengan kartu
    rfid.PCD_StopCrypto1(); // Hentikan enkripsi untuk kartu yang saat ini dipilih

    lcd.clear();
    lcd.print("Smart Door Lock");
    lcd.setCursor(0, 1);
    lcd.print(isDoorLocked ? "Locked" : "Unlocked"); // Tampilkan status kunci terbaru
  }

  // --- Ultrasonic Sensor (Hanya aktif jika pintu TIDAK terkunci) ---
  if (!isDoorLocked) {
    long duration, distance;
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // pulseIn dengan timeout untuk mencegah blokir jika tidak ada echo
    duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout 30ms

    // Hanya hitung jarak jika ada pulsa pantul yang terdeteksi (duration > 0)
    if (duration > 0) {
      distance = duration * 0.034 / 2; // Hitung jarak dalam cm

      Serial.print("Distance: ");
      Serial.print(distance);
      Serial.println(" cm");

      // Deteksi pergerakan/keberadaan orang (misalnya, jika ada objek di bawah 150 cm)
      // Sesuaikan ambang batas jarak ini sesuai dengan penempatan sensor dan area deteksi yang diinginkan.
      if (distance > 2 && distance < 150) { // Minimal 2cm untuk menghindari noise, maksimal 150cm
        if (digitalRead(LED_PIN) == LOW) { // Nyalakan LED hanya jika belum nyala
          digitalWrite(LED_PIN, HIGH);
          Serial.println("Movement detected! LED ON.");
        }
        lastMovementTime = millis(); // Perbarui waktu terakhir pergerakan
      }
    }

    // Cek timeout LED (hanya jika LED sedang nyala)
    if (digitalRead(LED_PIN) == HIGH && (millis() - lastMovementTime > LED_TIMEOUT)) {
      digitalWrite(LED_PIN, LOW);
      Serial.println("No movement for 1 minute. LED OFF.");
    }
  }
  delay(50); // Small delay for general stability of the loop
}
