#include <stdio.h>
#include <bitset>
#include <iostream>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <vector>

/* The code makes use of some bitwise operations, and assumes that the pins are in consecutive order
do not change these pins*/

#define IHB0_EN_PIN 0
#define IHB0_DIR_PIN 1
#define IHB1_EN_PIN 2
#define IHB1_DIR_PIN 3
#define SENSE_RST_PIN 4

#define ADDR_X0_PIN 5
#define ADDR_X1_PIN 6
#define ADDR_X2_PIN 7
#define ADDR_X3_PIN 8
#define ADDR_Y0_PIN 9
#define ADDR_Y1_PIN 10
#define ADDR_Y2_PIN 11
#define ADDR_Y3_PIN 12

#define X_EN_PIN 13
#define X_DIR_PIN 14
#define Y_EN_PIN 15
#define Y_DIR_PIN 16
#define DEBUG_EVENT_PIN 17

#define SENSE0_DATA_PIN 18
#define SENSE1_DATA_PIN 19

#define DELAY_100NS_TO_CYCLES(delay) (delay * 20)

void set_reset_latch(bool state) {
    gpio_put(SENSE_RST_PIN, state);
}

void set_address(uint8_t addr) {
    gpio_put_masked(
        (1 << ADDR_X0_PIN) | (1 << ADDR_X1_PIN) | (1 << ADDR_X2_PIN) | (1 << ADDR_X3_PIN) | 
        (1 << ADDR_Y0_PIN) | (1 << ADDR_Y1_PIN) | (1 << ADDR_Y2_PIN) | (1 << ADDR_Y3_PIN), addr << ADDR_X0_PIN);
}

enum MosfetBridgeState {
    NONE_CONDUCT_2 = 0b00,
    CONDUCT_DIR_2 = 0b01,
    NONE_CONDUCT = 0b10,
    CONDUCT_DIR_1 = 0b11
};

void set_x_drv(MosfetBridgeState state) {
    gpio_put_masked((1 << X_DIR_PIN) | (1 << X_EN_PIN), state << X_EN_PIN);
}

void set_y_drv(MosfetBridgeState state) {
    gpio_put_masked((1 << Y_DIR_PIN) | (1 << Y_EN_PIN), state << Y_EN_PIN);
}

void set_ihb0(MosfetBridgeState state) {
    gpio_put_masked((1 << IHB0_DIR_PIN) | (1 << IHB0_EN_PIN), state << IHB0_EN_PIN);
}

void set_ihb1(MosfetBridgeState state) {
    gpio_put_masked((1 << IHB1_DIR_PIN) | (1 << IHB1_EN_PIN), state << IHB1_EN_PIN);
}


// Generate the waveforms which will write either a 1 to selected cores, or write a 0 to selected cores
// You will need to call this twice to write for example 0b01
void write_memory_waveform(uint8_t address, bool dir, uint8_t enable_mask, bool reset_latch) {
    set_address(address);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(2));

    // Our cores are orientated in 2 possible ways
    uint8_t xAddress = address & 0xF;
    uint8_t yAddress = (address >> 4) & 0xF;

    bool invertX = ((xAddress + yAddress) % 2 != 0); // if the sum of xAdress and yAddress is ODD, then we invert
    
    // Trigger the reset latch if required
    if (reset_latch) {
        set_reset_latch(false);
    }

    // turn on the X drive
    bool tempVal = dir;

    // We could just use XOR
    if (invertX) {
        tempVal = !tempVal;
    }
    
    // Turn on the X drives and inhibit drives
    if (tempVal) {
        set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    } else {
        set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    }

    //busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));
    set_reset_latch(true); // allow data to come in

    // inhibit should cancel the X drive currents
    bool invertInhibit = yAddress % 2 == 0;
    if(dir) {
        invertInhibit = !invertInhibit;
    }
    
    if (!(enable_mask & 0b01)) {
        if (invertInhibit) {
            set_ihb0(MosfetBridgeState::CONDUCT_DIR_2);
        } else {
            set_ihb0(MosfetBridgeState::CONDUCT_DIR_1);
        }
    }

    if (!(enable_mask & 0b10)) {
        if (invertInhibit) {
            set_ihb1(MosfetBridgeState::CONDUCT_DIR_2);
        } else {
            set_ihb1(MosfetBridgeState::CONDUCT_DIR_1);
        }
    }
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Next, Turn on the Y drives
    if (dir) {
        set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    } else {
        set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    }
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10)); // Allow time for core to fully saturate

    // Turn off the Y drives
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));
    
    // Turn off the X and inhibit drives
    set_ihb0(MosfetBridgeState::NONE_CONDUCT);
    set_ihb1(MosfetBridgeState::NONE_CONDUCT);
    
    //busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(2));

    set_x_drv(MosfetBridgeState::NONE_CONDUCT);

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // This is required, so that our current limiting resistors will not overheat
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
}

void write_memory(uint8_t address, uint8_t value) {
    write_memory_waveform(address, false, 0b11, false);
    //if(value & 0b01){ // Temporary override inhibit is now not used
    //   write_memory_waveform(address, true, 0b11, false);
    //}

    write_memory_waveform(address, true, value, false);
}

uint8_t read_memory(uint8_t address) {
    write_memory_waveform(address, false, 0b11, true);
    uint8_t value = (gpio_get(SENSE1_DATA_PIN) << 1) | gpio_get(SENSE0_DATA_PIN);
    
    // Restore the value after read, since reading is destructive
    write_memory_waveform(address, true, value, false);

    return value;
}

void basic_core_response_test() {
    gpio_put(DEBUG_EVENT_PIN, 1);

    busy_wait_us(10);

    gpio_put(DEBUG_EVENT_PIN, 0);

    set_address(0);

    // Turn on the X and Y drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
    // Turn on the X and Y drive in the opposite direction
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
}

void half_current_core_response_test() {
    gpio_put(DEBUG_EVENT_PIN, 1);

    busy_wait_us(10);

    gpio_put(DEBUG_EVENT_PIN, 0);

    set_address(0);

    // Send current in direction A
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Repeatedly send half current in the opposite direction B
    for(int i=0; i < 1024; i++) {
        // Send current in direction A
        // Turn on the Y drive
        set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
        busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(12));

        // Turn off the Y drive
        set_y_drv(MosfetBridgeState::NONE_CONDUCT);
        busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
    }
    
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(30));

    // Send current in the same A:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(30));

    // Send currrent in opposite B:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Send currrent in same opposite direction B:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
}

void core_response_test() {
    gpio_put(DEBUG_EVENT_PIN, 1);

    busy_wait_us(10);

    gpio_put(DEBUG_EVENT_PIN, 0);

    set_address(0);

    // Send current in direction A
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Send current in the same A:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
    // Send current in opposite B:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
    // Send curent in same opposite direction B:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
}

void core_response_with_inhibit_test() {
    gpio_put(DEBUG_EVENT_PIN, 1);

    busy_wait_us(10);

    gpio_put(DEBUG_EVENT_PIN, 0);

    set_address(0);

    // Send current in direction A
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Send current in opposite direction B (but inhibited): 
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    set_ihb0(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the inhibit drive
    set_ihb0(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));


    // Send current in direction A (you should observe little response, as the previous operation was inhibited)
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));

    // Send currrent in same opposite direction B:
    // Turn on the X drive
    set_x_drv(MosfetBridgeState::CONDUCT_DIR_2);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn on the Y drive
    set_y_drv(MosfetBridgeState::CONDUCT_DIR_1);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(10));

    // Turn off the Y drive
    set_y_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(1));

    // Turn off the X drive
    set_x_drv(MosfetBridgeState::NONE_CONDUCT);
    busy_wait_at_least_cycles(DELAY_100NS_TO_CYCLES(5));
}

void write_all(bool value) {
    uint8_t full_value = 0;
    if(value) {
        full_value = 0b11;
    }

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            write_memory(address, full_value);
        }
    }
}

void dump_memory() {
    std::cout << "Memory contents:" << std::endl;

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        std::vector<uint8_t> values(16);

        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            values[xAddress] = read_memory(address);
        }

        // First row: check bit 0
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            if (values[xAddress] & 0x01)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        // Second row: check bit 1
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            if (values[xAddress] & 0b10)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        std::cout << std::endl;
    }

    std::cout << "End of memory contents" << std::endl;
}


const uint8_t smiley_16x16[16][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0},
    {0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0},
    {0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0},
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0},
    {0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 0},
    {0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0},
    {0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

const uint8_t blocky[16][16] = {
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {1,1,1,1,0,0,0,0,1,1,1,1,0,0,0,0},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1},
};

void dump_memory_compare_smiley() {
    std::cout << "Memory contents (compare smiley left test):" << std::endl;

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        std::vector<uint8_t> values(16);

        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            values[xAddress] = read_memory(address);

            if((values[xAddress] & 0x01) != smiley_16x16[yAddress][xAddress]) {
                gpio_put(DEBUG_EVENT_PIN, 1);

                busy_wait_us(10);

                gpio_put(DEBUG_EVENT_PIN, 0);

                std::cout << "[err ->]";
            }

            if (values[xAddress] & 0x01)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        // Second row: check bit 1
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            if (values[xAddress] & 0b10)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        std::cout << std::endl;
    }

    std::cout << "End of memory contents" << std::endl;
}


void dump_memory_debug_setpoint() {
    std::cout << "Memory contents (compare smiley left test):" << std::endl;

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        std::vector<uint8_t> values(16);

        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            values[xAddress] = read_memory(address);

            if(xAddress == 0 && yAddress == 14) {
                gpio_put(DEBUG_EVENT_PIN, 1);

                busy_wait_us(10);

                gpio_put(DEBUG_EVENT_PIN, 0);

                std::cout << "[set point->]";
            }

            if (values[xAddress] & 0x01)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        // Second row: check bit 1
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            if (values[xAddress] & 0b10)
                std::cout << "# ";
            else
                std::cout << "  ";
        }

        std::cout << std::endl;
    }

    std::cout << "End of memory contents" << std::endl;
}


void write_blocky(bool right) {
    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            uint8_t v = read_memory(address);
            if(right) {
                write_memory(address, (blocky[yAddress][xAddress] << 1) | ( v & 0b01));
            } else {
                write_memory(address, blocky[yAddress][xAddress] | (v & 0b10));
            }
        }
    }
}


void write_smiley(bool right) {
    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;
            uint8_t v = read_memory(address);
            if(right) {
                write_memory(address, (smiley_16x16[yAddress][xAddress] << 1) | ( v & 0b01));
            } else {
                write_memory(address, smiley_16x16[yAddress][xAddress] | (v & 0b10));
            }
        }
    }
}

int mem_test_gallop_internal(uint8_t test_address, uint8_t default_pattern, uint8_t bit_pattern) {
    int failures = 0;
    write_all(default_pattern);
    write_memory(test_address, bit_pattern);

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            uint8_t address = (yAddress << 4) | xAddress;
            uint8_t actual = read_memory(address);

            uint8_t expected = (address == test_address) ? bit_pattern : default_pattern;
            if (actual != expected) {
                failures++;
            }
        }
    }

    write_memory(test_address, 0);

    return failures;
} 

int mem_test_gallop(uint8_t default_pattern, uint8_t bit_pattern) {
    int failures = 0;

    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;

            failures += mem_test_gallop_internal(address, default_pattern, bit_pattern);
        }
    }

    return failures;
}

int mem_test_half_current_internal(uint8_t test_address) {
    int failures = 0;
    
    uint8_t default_pattern = 0b00;
    uint8_t bit_pattern = 0b01;

    // Half current threshold stress test
    write_all(default_pattern);

    // Repeatedly write 1 to a single core,
    // the test checks if such repeated writes affect other cores, ie. you should observe only a single core being 1
    for(int j=0; j<2048; j++) {
        write_memory_waveform(test_address, true, bit_pattern, false);
        //write_memory((7 << 4) | 7, bit_pattern); don't use this, as this also writes a zero
    }

   for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            uint8_t address = (yAddress << 4) | xAddress;
            
            uint8_t actual = read_memory(address);

            uint8_t expected = (address == test_address) ? bit_pattern : default_pattern;
            if (actual != expected) {
                failures++;
            }
        }
    }

    return failures;
}

int mem_test_half_current() {
    int failures = 0;
    
    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            int address = (yAddress << 4) | xAddress;

            failures += mem_test_half_current_internal(address);
        }
    }

    return failures;
}


const uint8_t smiley_8x8[8][8] = { 
    {0,1,1,1,1,1,1,0},
    {1,0,0,0,0,0,0,1},
    {1,0,1,0,0,1,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,1,0,0,1,0,1},
    {1,0,0,1,1,0,0,1},
    {0,1,0,0,0,0,1,0},
    {0,0,1,1,1,1,0,0},
};

const uint8_t stripey_8x8[8][8] = { 
    {1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1},
    {1,1,0,0,1,1,0,0},
    {0,0,1,1,0,0,1,1},
};

const uint8_t triangular_8x8[8][8] = { 
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,0},
    {1,1,1,1,1,1,0,0},
    {1,1,1,1,1,0,0,0},
    {1,1,1,1,0,0,0,0},
    {1,1,1,0,0,0,0,0},
    {1,1,0,0,0,0,0,0},
    {1,0,0,0,0,0,0,0},
};

const uint8_t cross_8x8[8][8] = { 
    {0,0,0,1,1,0,0,0},
    {0,0,0,1,1,0,0,0},
    {0,0,0,1,1,0,0,0},
    {1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},
    {0,0,0,1,1,0,0,0},
    {0,0,0,1,1,0,0,0},
    {0,0,0,1,1,0,0,0},
};

// Draw a image on bit 0 (group 0)
void draw_image_8x8(uint8_t startX, uint8_t startY, const uint8_t img[8][8]) {
    for (int yAddress = 0; yAddress < 8; ++yAddress) {
        for (int xAddress = 0; xAddress < 8; ++xAddress) {
            int address = ((yAddress + startY) << 4) | (xAddress + startX);
            uint8_t v = read_memory(address);
            write_memory(address, img[yAddress][xAddress] | ( v & 0b10));
        }
    }
}


int mem_test_image_internal() {
    int failures = 0;

    for(int i=0; i<32; i++) {
        // Repeatedly write/read only a 8x8 image, but not touch any other values
        draw_image_8x8(0, 0, smiley_8x8);
    }

    // Now lets read all bits and check for correctness
    for (int yAddress = 0; yAddress < 16; ++yAddress) {
        for (int xAddress = 0; xAddress < 16; ++xAddress) {
            uint8_t address = (yAddress << 4) | xAddress;
            
            uint8_t actual = read_memory(address);

            uint8_t expected = smiley_16x16[yAddress][xAddress] << 1;

            if(xAddress < 8 && yAddress < 8) {
                expected |= smiley_8x8[yAddress][xAddress];
            }

            if(xAddress >= 8 && yAddress < 8) {
                expected |= stripey_8x8[yAddress][xAddress - 8];
            }

            if(xAddress < 8 && yAddress >= 8) {
                expected |= triangular_8x8[yAddress-8][xAddress];
            }

            if(xAddress >= 8 && yAddress >= 8) {
                expected |= cross_8x8[yAddress-8][xAddress-8];
            }

            if (actual != expected) {
                failures++;
            }
        }
    }

    return failures;
}

int mem_test_image() {
    int failures = 0;
    
    write_all(0);

    // Write a full set of images to bit 0 (group 0)
    draw_image_8x8(0, 0, smiley_8x8);
    draw_image_8x8(8, 0, stripey_8x8);
    draw_image_8x8(0, 8, triangular_8x8);
    draw_image_8x8(8, 8, cross_8x8);
    // Also write a 16x16 image to bit 1 (group 1)
    write_smiley(true);

    for(int i=0; i<128; i++) {
        failures += mem_test_image_internal();
    }

    return failures;
}

int main()
{      
    // Set cpu clock to 200MHz
    set_sys_clock_khz(200000, true);

    stdio_init_all();

    gpio_init(IHB0_EN_PIN);
    gpio_init(IHB0_DIR_PIN);
    gpio_init(IHB1_EN_PIN);
    gpio_init(IHB1_DIR_PIN);
    gpio_init(SENSE_RST_PIN);
    gpio_init(ADDR_X0_PIN);
    gpio_init(ADDR_X1_PIN);
    gpio_init(ADDR_X2_PIN);
    gpio_init(ADDR_X3_PIN);
    gpio_init(ADDR_Y0_PIN);
    gpio_init(ADDR_Y1_PIN);
    gpio_init(ADDR_Y2_PIN);
    gpio_init(ADDR_Y3_PIN);
    gpio_init(X_EN_PIN);
    gpio_init(X_DIR_PIN);
    gpio_init(Y_EN_PIN);
    gpio_init(Y_DIR_PIN);
    gpio_init(DEBUG_EVENT_PIN);

    gpio_init(SENSE0_DATA_PIN);
    gpio_init(SENSE1_DATA_PIN);

    gpio_put(IHB0_EN_PIN, false);
    gpio_put(IHB0_DIR_PIN, false);
    gpio_put(IHB1_EN_PIN, false);
    gpio_put(IHB1_DIR_PIN, false);
    gpio_put(SENSE_RST_PIN, false);
    gpio_put(ADDR_X0_PIN, false);
    gpio_put(ADDR_X1_PIN, false);
    gpio_put(ADDR_X2_PIN, false);
    gpio_put(ADDR_X3_PIN, false);
    gpio_put(ADDR_Y0_PIN, false);
    gpio_put(ADDR_Y1_PIN, false);
    gpio_put(ADDR_Y2_PIN, false);
    gpio_put(ADDR_Y3_PIN, false);
    gpio_put(X_EN_PIN, false);
    gpio_put(X_DIR_PIN, false);
    gpio_put(Y_EN_PIN, false);
    gpio_put(Y_DIR_PIN, false);
    gpio_put(DEBUG_EVENT_PIN, false);

    gpio_set_dir(IHB0_EN_PIN, GPIO_OUT);
    gpio_set_dir(IHB0_DIR_PIN, GPIO_OUT);
    gpio_set_dir(IHB1_EN_PIN, GPIO_OUT);
    gpio_set_dir(IHB1_DIR_PIN, GPIO_OUT);
    gpio_set_dir(SENSE_RST_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_X0_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_X1_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_X2_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_X3_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_Y0_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_Y1_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_Y2_PIN, GPIO_OUT);
    gpio_set_dir(ADDR_Y3_PIN, GPIO_OUT);
    gpio_set_dir(X_EN_PIN, GPIO_OUT);
    gpio_set_dir(X_DIR_PIN, GPIO_OUT);
    gpio_set_dir(Y_EN_PIN, GPIO_OUT);
    gpio_set_dir(Y_DIR_PIN, GPIO_OUT);
    gpio_set_dir(DEBUG_EVENT_PIN, GPIO_OUT);


    gpio_set_dir(SENSE0_DATA_PIN, GPIO_IN);
    gpio_set_dir(SENSE1_DATA_PIN, GPIO_IN);

    
    //std::bitset<8> x1(*val1);
    //std::cout << x1 << '\n';

    int total_test_cnt = 0;
    int gallop_test_fail_cnt = 0;
    int full_current_test_fail_cnt = 0;
    int image_test_fail_cnt = 0;

    while (true) {
        printf("Perfoming the test\n");
        total_test_cnt++;
        //core_response_test();
        //half_current_core_response_test();
        //sleep_ms(1);
        //core_response_with_inhibit_test();
        //basic_core_response_test();
        
        //write_all(1);
        //dump_memory();

        //write_all(0);
        //dump_memory();
        /*write_smiley(true);
        dump_memory();

        write_blocky(false);
        dump_memory();

        write_smiley(false);
        dump_memory();

        dump_memory_debug_setpoint();

        //dump_memory_compare_smiley();

        write_blocky(true);
        dump_memory();

         
        sleep_ms(3000);*/

        int failures = 0;
        int total_reads = 256*256*8;
        
        failures += mem_test_gallop(0b00, 0b00);
        failures += mem_test_gallop(0b00, 0b01);
        failures += mem_test_gallop(0b00, 0b10);
        failures += mem_test_gallop(0b00, 0b11);
        failures += mem_test_gallop(0b11, 0b00);
        failures += mem_test_gallop(0b11, 0b01);
        failures += mem_test_gallop(0b11, 0b10);
        failures += mem_test_gallop(0b11, 0b11);
        
        if(failures > 0) gallop_test_fail_cnt++;
        std::cout << "Full gallop test complete, num failures: " << failures << " out of " << total_reads << " reads \n";

        int failures2 = 0;
        failures2 += mem_test_half_current();
        int total_reads2 = 256*256;
        if(failures2 > 0) full_current_test_fail_cnt++;
        std::cout << "Full half current test complete, num failures: " << failures2 << " out of " << total_reads2 << " reads \n";

        int failures3  = 0;
        failures3 += mem_test_image();
        int total_reads3 = 256*128;
        if(failures3 > 0) image_test_fail_cnt++;
        std::cout << "Image test complete, num failures: " << failures3 << " out of " << total_reads3 << " reads \n";

        std::cout << '\n';
        std::cout << "Summary: \n";
        std::cout << "total test performed: " << total_test_cnt << "\n";
        std::cout << "gallop_test_fail_cnt: " << gallop_test_fail_cnt << "\n";
        std::cout << "full_current_test_fail_cnt: " << full_current_test_fail_cnt << "\n";
        std::cout << "image_test_fail_cnt: " << image_test_fail_cnt << "\n";
        
        std::cout << '\n';
        std::cout << '\n';
    }
}