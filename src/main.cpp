#include <Arduino.h>
#include <STM32LoRaWAN.h>

// API_KEY: NNSXS.WCVTIKEVJQPGRXXMY3C2MYTSLYW5HMVLLHATCQA.5FOEZGYS6TMCQT5YPLBQ7UNPFUSGWLPOOAHUQCYC7CC65Z4ID23A

#define NODE_ID 0
#define NUM_DEVICES 1

static const unsigned long TX_INTERVAL = 60000; /* ms */
unsigned long last_tx = 0;

STM32LoRaWAN modem;
uint8_t humidity[NUM_DEVICES] = {33};
uint8_t pump_state[NUM_DEVICES] = {0};

/*
    FORMAT: | NODE (4b) | N_PER (4b) | PER1 (7b) (1b) | PER2 (7b) (1b) | ... |
    ERROR_CODES: PER1 (7b) = [humidity %], with a value greater than 100;
*/
void uplink() {
    uint8_t payload[0x1 + NUM_DEVICES] = {0};

    payload[0] = (NODE_ID << 4) | (NUM_DEVICES & 0x0F);

    for (int i = 0; i < NUM_DEVICES; i++) {
        payload[i + 1] = (humidity[i] << 1) | (pump_state[i] & 0x01);
    }

    modem.setPort(10);
    modem.beginPacket();
    modem.write(payload, sizeof(payload));

    if (modem.endPacket() == sizeof(payload)) {
        Serial.println("Sent packet");
    } else {
        Serial.println("Failed to send packet");
    }
}

void downlink() {
    if (modem.available()) {
        Serial.print("Received packet on port ");
        Serial.print(modem.getDownlinkPort());
        Serial.print(":");

        while (modem.available()) {
            uint8_t b = modem.read();
            Serial.print(" ");
            Serial.print(b >> 4, HEX);
            Serial.print(b & 0xF, HEX);
        }

        Serial.println();
    }
}

void update() {
    humidity[0] = humidity[0] + 1 % 100;
    pump_state[0] = !pump_state[0];
}

void setup() {
    Serial.begin(115200);
    Serial.println("Start");
    modem.begin(EU868);

    // Configure join method by (un)commenting the right method
    // call, and fill in credentials in that method call.
    bool connected = modem.joinOTAA(
        /* AppEui */
        "0000000000000000",
        /* AppKey */
        "DA8057CC3648CEEB74B37437A835A8A1",
        /* DevEui */
        "70B3D57ED007025F");
    // bool connected = modem.joinABP(/* DevAddr */ "00000000", /* NwkSKey */ "00000000000000000000000000000000", /* AppSKey */ "00000000000000000000000000000000");

    if (connected) {
        Serial.println("Joined");
    } else {
        Serial.println("Join failed");
        while (1);
    }
}

void loop() {
    if (!last_tx || millis() - last_tx > TX_INTERVAL) {
        update();
        uplink();
        last_tx = millis();
    }
}