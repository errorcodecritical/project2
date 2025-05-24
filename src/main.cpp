#include <Arduino.h>
#include <Wire.h>
#include <STM32LoRaWAN.h>

// API_KEY: NNSXS.WCVTIKEVJQPGRXXMY3C2MYTSLYW5HMVLLHATCQA.5FOEZGYS6TMCQT5YPLBQ7UNPFUSGWLPOOAHUQCYC7CC65Z4ID23A

#define NODE_ID 0
#define MAX_DEVICES 16

static const unsigned long TX_INTERVAL = 60000; /* ms */
unsigned long last_tx = 0;

STM32LoRaWAN modem;

enum regs {
    REG_RESET = 0xAB,
    REG_READY = 0xCD,
    REG_ADDRESS = 0xEF,
    REG_HUMIDITY = 0x33,
    REG_PUMP = 0x34
};

struct dev {
    bool active;
    uint8_t humidity;
    uint8_t pump;
};

dev devices[MAX_DEVICES] = {};
uint8_t num_devices = 0;
uint8_t has_unconfigured = 0;

/*
    FORMAT: | NODE (4b) | N_PER (4b) | PER1 (7b) (1b) | PER2 (7b) (1b) | ... |
    ERROR_CODES: PER1 (7b) = [humidity %], with a value greater than 100;
*/
void uplink() {
    uint8_t payload[0x1 + MAX_DEVICES] = {0};

    payload[0] = (NODE_ID << 4) | (MAX_DEVICES & 0x0F);

    for (int i = 0; i < MAX_DEVICES; i++) {
        // payload[i + 1] = (humidity[i] << 1) | (pump_state[i] & 0x01);
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

uint8_t device_write(uint8_t dev, uint8_t reg, uint8_t& val) {
    // Write value to device register.
    Wire.beginTransmission(dev);
    Wire.write((uint8_t) reg);
    Wire.write((uint8_t) val);
    int i2c_error = Wire.endTransmission();
    
    if (i2c_error != 0) {
        Serial.printf("Failed to write to device %d, register %d, value %d.\n", dev, reg, val);
        return -1;
    }

    return 0;
}

uint8_t device_read(uint8_t dev, uint8_t reg, uint8_t& val) {
    // Read value from device register.
    Wire.beginTransmission(dev);
    Wire.write((uint8_t) reg);
    int i2c_error = Wire.endTransmission();

    if (i2c_error != 0) {
        Serial.printf("Failed to read from device %d, register %d, error %d.\n", dev, reg, i2c_error);
        return -2;
    }

    delay(10);

    // Request data from device.
    Wire.requestFrom((uint8_t) dev, 1);

    while (Wire.available()) {
        val = Wire.read();
    }

    return 0;
}

uint8_t update() {
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].active) {
            // Request device data
            device_read(i, REG_HUMIDITY, devices[i].humidity);
            device_read(i, REG_PUMP, devices[i].pump);

            Serial.printf("(Device %d) - Humidity: %d%%, Pump: %s.\n", i, devices[i].humidity, devices[i].pump ? "on" : "off");
        }
    }

    return 0;
}

uint8_t next_free_address() {
    static uint8_t last_addr = 0x00;
    return ++last_addr % MAX_DEVICES;
}

uint8_t device_discovery() {
    uint8_t count = 0;

    for (int i = 0; i < MAX_DEVICES; i++) {
        Wire.beginTransmission(i);
        int i2c_error = Wire.endTransmission();

        if (i2c_error == 0) {
            Serial.print("Found device: ");
            Serial.print(i, DEC);
            Serial.print(" (0x");
            Serial.print(i, HEX);
            Serial.println(")");
            devices[i].active = true;
            count++;
        } else {
            devices[i].active = false;
        }
    }

    Wire.beginTransmission(0x13);
    int i2c_error = Wire.endTransmission();
    has_unconfigured = (i2c_error == 0);

    if (i2c_error == 0) {
        Serial.println("Found unconfigured device(s) at broadcast address: 0x13.");
    }

    if (count == 0) {
        Serial.println("No configured devices found.");
    }

    return count;
}

uint8_t device_configure() {
    int i2c_error = 0;
    uint8_t addr = 0;
    uint8_t val = 1;

    if (has_unconfigured == 0) {
        return 0;
    }
        
    if (num_devices >= MAX_DEVICES) {
        return -1;
    }

    // Discover connected devices by transmitting to predefined broadcast address (0x13)
    i2c_error = device_write(0x13, REG_RESET, val);

    if (i2c_error != 0) {
        Serial.println("No unconfigured device(s) found.");
        return -2;
    }

    // Connected devices will perform handshake using random backoff, I2C addresses are attributed to devices as first-come-first-served.
    i2c_error = device_read(0x13, REG_READY, val);
    
    delay(1000);

    if (i2c_error != 0 || val != 0x01) {
        Serial.println("Device(s) not ready.");
        return -3;
    }

    val = next_free_address();

    // Set device address.
    i2c_error = device_write(0x13, REG_ADDRESS, val);

    if (i2c_error != 0) {
        Serial.println("Unable to set address.");
        return -4;
    }

    Serial.printf("Assigned address : %d\n", val);

    return val;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Start");
    modem.begin(EU868);

    Wire.begin(); // Init as master
    Wire.setClock(100000);

    while (1) {
        num_devices = device_discovery();
        delay(2000);
        uint8_t new_addr = device_configure();
        delay(2000);
        update();
        delay(2000);
    }


    // // Configure join method by (un)commenting the right method
    // // call, and fill in credentials in that method call.
    // bool connected = modem.joinOTAA(
    //     /* AppEui */
    //     "0000000000000000",
    //     /* AppKey */
    //     "DA8057CC3648CEEB74B37437A835A8A1",
    //     /* DevEui */
    //     "70B3D57ED007025F");
    // // bool connected = modem.joinABP(/* DevAddr */ "00000000", /* NwkSKey */ "00000000000000000000000000000000", /* AppSKey */ "00000000000000000000000000000000");

    // if (connected) {
    //     Serial.println("Joined");
    // } else {
    //     Serial.println("Join failed");
    //     while (1);
    // }
}

void loop() {
    // if (!last_tx || millis() - last_tx > TX_INTERVAL) {
    //     update();
    //     uplink();
    //     last_tx = millis();
    // }
}