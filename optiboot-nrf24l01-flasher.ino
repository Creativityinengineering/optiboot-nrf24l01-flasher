#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <EEPROM.h>

#define FLASH_TOOL_MODE
#define MAX_PKT_SIZE 32
#define SEQN

//RF24 radio(9, 10); // CE, CSN Pins (GPIO 9, 10)
RF24 radio(17, 5); // CE, CSN Pins (GPIO 17, 5)

static void flasher_setup(void);
static void flasher_rx_handle(void);
static void flasher_tx_handle(void);

static uint8_t tx_fifo[MAX_PKT_SIZE];
static uint8_t tx_fifo_start = 0;
static uint8_t tx_fifo_len = 0;

static void handle_input(char ch) {
    tx_fifo[(tx_fifo_start + tx_fifo_len++) % MAX_PKT_SIZE] = ch;
}

static uint8_t eeprom_read(uint16_t addr) {
    return EEPROM.read(addr);
}

void setup() {
    Serial.begin(115200);
    radio.begin();
    radio.setChannel(76); // Beispielkanal
    radio.setPALevel(RF24_PA_HIGH);
    radio.setDataRate(RF24_1MBPS);
    radio.openReadingPipe(1, 0xF0F0F0F0E1LL);
    radio.openWritingPipe(0xF0F0F0F0D2LL);
    radio.startListening();

    Serial.println("Setup complete");
}

void loop() {
    static uint8_t tx_cnt;

    if (radio.available()) {
        uint8_t pkt_len;
        uint8_t pkt_buf[32]; // Hier deklariert
        radio.read(pkt_buf, pkt_len); // pkt_len ist jetzt korrekt

#ifdef SEQN
        static uint8_t seqn = 0xff;
#endif

#ifdef FLASH_TOOL_MODE
        flasher_rx_handle();
#endif

#ifdef SEQN
        if (pkt_buf[0] != seqn) {
            seqn = pkt_buf[0];
            for (uint8_t i = 1; i < pkt_len; i++)
                Serial.write(pkt_buf[i]);
        }
#else
        for (uint8_t i = 0; i < pkt_len; i++)
            Serial.write(pkt_buf[i]);
#endif

        tx_cnt = 0;
    }

    if (tx_fifo_len) {
        // Typkonflikt bei min() beheben
        uint8_t pkt_len = min(static_cast<uint8_t>(tx_fifo_len), static_cast<uint8_t>(MAX_PKT_SIZE));
#ifdef SEQN
        static uint8_t seqn = 0x00;
        uint8_t count = 128;
        uint8_t pkt_buf[32]; // Hier deklariert
        pkt_buf[0] = seqn++;
#else
        uint8_t count = 2;
#endif

#ifdef FLASH_TOOL_MODE
        flasher_tx_handle();
#endif

        tx_cnt++;

        noInterrupts();
        pkt_len = min(static_cast<uint8_t>(tx_fifo_len), static_cast<uint8_t>(MAX_PKT_SIZE));
        tx_fifo_len -= pkt_len;
        tx_fifo_start += pkt_len;
        interrupts();

        while (--count) {
            delay(4);

            radio.stopListening();
            radio.write(pkt_buf, pkt_len);
            radio.startListening();
        }
    }
}

static void flasher_setup(void) {
}

static uint32_t prev_txrx_ts = 0;

static void flasher_rx_handle(void) {
    prev_txrx_ts = millis();
}

static void flasher_tx_handle(void) {
    static uint8_t first_tx = 1;

    if (millis() - prev_txrx_ts > 1000)
        first_tx = 1;

    if (first_tx) {
        uint8_t ch = 0xff;
        radio.stopListening();
        radio.write(&ch, 1);
        radio.startListening();

        delay(100);

        uint8_t pkt[4];
        pkt[0] = eeprom_read(0);
        pkt[1] = eeprom_read(1);
        pkt[2] = eeprom_read(2);
        pkt[3] = MAX_PKT_SIZE;
        radio.stopListening();
        radio.write(pkt, 4);
        radio.startListening();

        first_tx = 0;
    }

    prev_txrx_ts = millis();
}
