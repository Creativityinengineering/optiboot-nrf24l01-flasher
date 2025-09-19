#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <EEPROM.h>

#define FLASH_TOOL_MODE
#define MAX_PKT_SIZE 32
#define SEQN

RF24 radio(17, 5); // CE, CSN Pins (GPIO 17, 5)

// FIFO-Puffer für die zu sendenden Daten
static uint8_t tx_fifo[MAX_PKT_SIZE];
static uint8_t tx_fifo_start = 0; // Startindex des Puffers
static uint8_t tx_fifo_len = 0; // Aktuelle Länge des Puffers

// Funktion zum Hinzufügen eines Zeichens zum FIFO-Puffer
static void handle_input(char ch) {
    tx_fifo[(tx_fifo_start + tx_fifo_len++) % MAX_PKT_SIZE] = ch;
}

// Funktion zum Lesen eines Bytes aus dem EEPROM
static uint8_t eeprom_read(uint16_t addr) {
    return EEPROM.read(addr);
}

// Setup-Funktion, die einmal beim Start aufgerufen wird
void setup() {
    Serial.begin(115200); // Initialisiere die serielle Kommunikation

    // Initialisiere nRF24L01
    radio.begin();

    // CE und CSN Pins als Ausgänge setzen
    pinMode(17, OUTPUT); // CE Pin auf GPIO 17
    pinMode(5, OUTPUT);  // CSN Pin auf GPIO 5

    // Setze die nRF24L01 Konfiguration
    radio.setChannel(42); // Setze den Kanal auf 42
    radio.setPALevel(RF24_PA_HIGH); // Maximale Sendeleistung
    radio.setDataRate(RF24_250KBPS); // 250 kbps Datenrate
    
    // Öffne die Lese- und Schreib-Pipes mit benutzerdefinierten Adressen
    radio.openReadingPipe(1, 0xF0F0F0F0); // Beispieladresse für Lese-Pipe
    radio.openWritingPipe(0xF0F0F0F1); // Beispieladresse für Schreib-Pipe

    radio.startListening(); // In den Empfangsmodus wechseln

    Serial.println("Setup complete");
}

// Hauptschleife, die kontinuierlich ausgeführt wird
void loop() {
    static uint8_t tx_cnt; // Zähler für aufeinanderfolgende Tx-Pakete

    // Überprüfe, ob Daten im Empfangspuffer verfügbar sind
    if (radio.available()) {
        uint8_t pkt_len;
        uint8_t pkt_buf[32]; // Puffer für empfangene Daten
        radio.read(pkt_buf, pkt_len); // Lese die empfangenen Daten

#ifdef SEQN
        static uint8_t seqn = 0xff; // Sequenznummer
#endif

#ifdef FLASH_TOOL_MODE
        flasher_rx_handle(); // RX-bezogene Aufgaben für den Flasher
#endif

#ifdef SEQN
        // Überprüfe die Sequenznummer
        if (pkt_buf[0] != seqn) {
            seqn = pkt_buf[0]; // Aktualisiere die Sequenznummer
            for (uint8_t i = 1; i < pkt_len; i++)
                Serial.write(pkt_buf[i]); // Sende die empfangenen Daten über die serielle Schnittstelle
        }
#else
        // Sende alle empfangenen Daten über die serielle Schnittstelle
        for (uint8_t i = 0; i < pkt_len; i++)
            Serial.write(pkt_buf[i]);
#endif

        tx_cnt = 0; // Setze den Zähler zurück
    }

    // Überprüfe, ob Daten im FIFO-Puffer vorhanden sind
    if (tx_fifo_len) {
        // Bestimme die Länge des Pakets
        uint8_t pkt_len = min(static_cast<uint8_t>(tx_fifo_len), static_cast<uint8_t>(MAX_PKT_SIZE));
#ifdef SEQN
        static uint8_t seqn = 0x00; // Sequenznummer für TX
        uint8_t count = 128; // Anzahl der Versuche
        uint8_t pkt_buf[32]; // Puffer für das zu sendende Paket
        pkt_buf[0] = seqn++; // Setze die Sequenznummer
#else
        uint8_t count = 2; // Anzahl der Versuche
#endif

#ifdef FLASH_TOOL_MODE
        flasher_tx_handle(); // TX-bezogene Aufgaben für den Flasher
#endif

        tx_cnt++; // Erhöhe den Zähler

        // Atomare Operationen für den Zugriff auf den FIFO-Puffer
        noInterrupts();
        pkt_len = min(static_cast<uint8_t>(tx_fifo_len), static_cast<uint8_t>(MAX_PKT_SIZE));
        tx_fifo_len -= pkt_len; // Reduziere die Länge des Puffers
        tx_fifo_start += pkt_len; // Aktualisiere den Startindex
        interrupts();

        // Sende das Paket
        while (--count) {
            delay(4); // Wartezeit

            radio.stopListening(); // Wechsel in den TX-Modus
            radio.write(pkt_buf, pkt_len); // Sende das Paket
            radio.startListening(); // Zurück in den RX-Modus
        }
    }
}

// Funktion für den Flasher-Setup
static void flasher_setup(void) {
}

// Zeitstempel für die letzte RX/TX-Aktion
static uint32_t prev_txrx_ts = 0;

// RX-bezogene Aufgaben für den Flasher
static void flasher_rx_handle(void) {
    prev_txrx_ts = millis(); // Aktualisiere den Zeitstempel
}

// TX-bezogene Aufgaben für den Flasher
static void flasher_tx_handle(void) {
    static uint8_t first_tx = 1; // Flag für den ersten TX

    // Wenn mehr als eine Sekunde seit der letzten Kommunikation vergangen ist
    if (millis() - prev_txrx_ts > 1000)
        first_tx = 1; // Setze das Flag zurück

    if (first_tx) {
        uint8_t ch = 0xff; // Sende 0xff zum Zurücksetzen des Boards
        radio.stopListening(); // Wechsel in den TX-Modus
        radio.write(&ch, 1); // Sende das Byte
        radio.startListening(); // Zurück in den RX-Modus

        delay(100); // Wartezeit für den Bootloader

        uint8_t pkt[4]; // Puffer für die Adresse und Paketgröße
        pkt[0] = eeprom_read(0); // Lese die Adresse aus dem EEPROM
        pkt[1] = eeprom_read(1); // Lese die Adresse aus dem EEPROM
        pkt[2] = eeprom_read(2); // Lese die Adresse aus dem EEPROM
        pkt[3] = MAX_PKT_SIZE; // Setze die maximale Paketgröße
        radio.stopListening(); // Wechsel in den TX-Modus
        radio.write(pkt, 4); // Sende die Adresse und Paketgröße
        radio.startListening(); // Zurück in den RX-Modus

        first_tx = 0; // Setze das Flag zurück
    }

    prev_txrx_ts = millis(); // Aktualisiere den Zeitstempel
}
