#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- RFID ---
#define SS_PIN  10
#define RST_PIN  9
MFRC522 rfid(SS_PIN, RST_PIN);

// --- Servo ---
#define SERVO_PIN 3
Servo servo;

// --- Buzzer ---
#define BUZZER_PIN 5

// --- Bouton Morse ---
#define BOUTON          4
#define DEBOUNCE  30       // délai anti-rebond en ms
#define MAX_CODE_LEN    10       // longueur max du code Morse saisi

const unsigned long DOT_TIME   = 200;   // durée max d'un appui court (point)
const unsigned long DASH_TIME  = 600;   // durée min d'un appui long (tiret)
const unsigned long END_TIME   = 1200;  // silence après lequel le code est validé
const unsigned long SERVO_TIME = 2000; // durée d'ouverture de la porte

const char* expected = ".-.-";       // code Morse secret à saisir
byte authUID[4] = {0x3D, 0x27, 0xAF, 0x62}; // UID du badge autorisé

// --- Variables Morse ---
char code[MAX_CODE_LEN + 1] = "";  // code Morse saisi (tableau de caractères)
int  codeLen    = 0;
bool btnState = false;
bool btnPrev = false;
unsigned long tPress    = 0;  // horodatage début d'appui
unsigned long tRelease  = 0;  // horodatage dernier relâchement (base du END_TIME)
unsigned long tDeb = 0;  // horodatage pour le debounce

// --- Variables servo non-bloquant ---
bool servoOpen = false;      // true pendant l'ouverture de la porte
unsigned long tServo = 0;     // horodatage d'ouverture

// -------------------------------------------------------

// Vérifie si l'UID du badge correspond à l'UID autorisé octet par octet
bool badgeOK(byte *uid, byte taille) {
  if (taille != 4) return false;
  for (int i = 0; i < 4; i++)
    if (uid[i] != authUID[i]) return false;
  return true;
}

// Affiche un message sur l'écran OLED (ligne1 petite, ligne2 grande)
void affiche(const char* ligne1, const char* ligne2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(ligne1);
  if (strlen(ligne2) > 0) {
    display.setCursor(0, 20);
    display.setTextSize(2);
    display.println(ligne2);
  }
  display.display();
}

// Buzzer avec intensité croissante via PWM (analogWrite)
void beep() {
  for (int i = 50; i <= 255; i += 50) {
    analogWrite(BUZZER_PIN, i);  // augmente le rapport cyclique PWM
    delay(100);
    analogWrite(BUZZER_PIN, 0);
    delay(100);
  }
}

// Déclenche l'ouverture : buzzer + servo, sans bloquer la boucle principale
void openDoor() {
  beep();
  servo.write(90);       // position ouverte
  servoOpen = true;
  tServo = millis();     // on note l'heure pour fermer après SERVO_TIME
}

// Remet le code Morse à zéro
void resetCode() {
  memset(code, 0, sizeof(code));  // efface le tableau proprement
  codeLen  = 0;
  tRelease = 0;
}

// -------------------------------------------------------

void setup() {
  Serial.begin(9600);
  SPI.begin();           // initialise le bus SPI (utilisé par le lecteur RFID)
  rfid.PCD_Init();       // initialise le module MFRC522 via SPI

  // Initialise l'écran OLED via I2C à l'adresse 0x3C
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED introuvable");
    while (1);           // bloque si l'écran est absent
  }
  affiche("Present badge", "ou code Morse");

  servo.attach(SERVO_PIN);
  servo.write(0);        // position fermée au démarrage

  pinMode(BOUTON, INPUT_PULLUP); // bouton actif à l'état bas
  pinMode(BUZZER_PIN, OUTPUT);
  analogWrite(BUZZER_PIN, 0);   // buzzer éteint
}

void loop() {

  // --- Fermeture automatique du servo après SERVO_TIME ms (non-bloquant) ---
  if (servoOpen && (millis() - tServo >= SERVO_TIME)) {
    servo.write(0);      // position fermée
    servoOpen = false;
    affiche("Present badge", "ou code Morse");
  }

  // --- Lecture bouton avec debounce logiciel ---
  bool lecture = !digitalRead(BOUTON); // inversion car INPUT_PULLUP
  if (lecture != btnPrev) tDeb = millis(); // détecte un changement

  if (millis() - tDeb >= DEBOUNCE && lecture != btnState) {
    btnState = lecture;

    if (btnState) {
      tPress = millis(); // mémorise le début de l'appui
    } else {
      // Mesure la durée de l'appui pour distinguer point et tiret
      unsigned long duree = millis() - tPress;
      tRelease = millis(); // END_TIME compte depuis ce relâchement

      if (duree >= DOT_TIME && duree < DASH_TIME && codeLen < MAX_CODE_LEN) {
        code[codeLen++] = '.';
        Serial.print(".");
      } else if (duree >= DASH_TIME && codeLen < MAX_CODE_LEN) {
        code[codeLen++] = '-';
        Serial.print("-");
      }
    }
  }
  btnPrev = lecture;

  // --- Validation du code Morse après END_TIME ms de silence ---
  if (!btnState && codeLen > 0 && tRelease > 0 &&
      (millis() - tRelease >= END_TIME)) {

    Serial.print("\nCode Morse saisi : ");
    Serial.println(code);

    if (strcmp(code, expected) == 0) { // comparaison des chaînes C
      Serial.println("CODE CORRECT");
      affiche("Code Morse", "Correct !");
      openDoor();
    } else {
      Serial.println("CODE INCORRECT");
      affiche("Code Morse", "Incorrect !");
      delay(1500);
      affiche("Present badge", "ou code Morse");
    }
    resetCode();
  }

  // --- Lecture badge RFID via SPI ---
  if (!servoOpen && rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    if (badgeOK(rfid.uid.uidByte, rfid.uid.size)) {
      Serial.println("Badge OK");
      affiche("Acces", "Autorise");
      openDoor();
    } else {
      Serial.println("Badge refuse");
      affiche("Acces", "Refuse");
      delay(1500);
      affiche("Present badge", "ou code Morse");
    }
    rfid.PICC_HaltA();       // stoppe la communication avec la carte
    rfid.PCD_StopCrypto1();  // désactive le chiffrement RFID
  }
}
