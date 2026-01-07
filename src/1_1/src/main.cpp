// Implementation of the application layer for
// JIS X 6319-4 compatible card "SiliCa"

#include <stdio.h>
#include <string.h>
#include <avr/eeprom.h>
#include "silica.h"

static constexpr int BLOCK_MAX = 12;
static constexpr int SYSTEM_MAX = 4;
static constexpr int SERVICE_MAX = 4;

constexpr int LAST_ERROR_SIZE = 2;

static uint8_t idm[8];
static uint8_t pmm[8];

static uint8_t EEMEM idm_eep[8];
static uint8_t EEMEM pmm_eep[8];

static uint8_t service_code[2 * SERVICE_MAX];
static uint8_t system_code[2 * SYSTEM_MAX];

static uint8_t EEMEM service_code_eep[2 * SERVICE_MAX];
static uint8_t EEMEM system_code_eep[2 * SYSTEM_MAX];

static uint8_t EEMEM block_data_eep[16 * BLOCK_MAX];

static const int ERROR_BLOCK = 0xE0;
static uint8_t EEMEM last_error_eep[16 * LAST_ERROR_SIZE];

static uint8_t response[0xFF] = {};

void initialize()
{
    // read parameters from EEPROM
    eeprom_read_block(idm, idm_eep, 8);
    eeprom_read_block(pmm, pmm_eep, 8);
    eeprom_read_block(service_code, service_code_eep, 2 * SERVICE_MAX);
    eeprom_read_block(system_code, system_code_eep, 2 * SYSTEM_MAX);
}

bool polling(packet_t command)
{
    // find system code
    int system_index = -1;
    for (int i = 0; i < SYSTEM_MAX; i++)
    {
        uint8_t sc1 = system_code[2 * i];
        uint8_t sc2 = system_code[2 * i + 1];

        if (sc1 == 0 && sc2 == 0)
            break;

        if ((command[2] == sc1 || command[2] == 0xFF) && (command[3] == sc2 || command[3] == 0xFF))
        {
            system_index = i;
            break;
        }
    }

    // avoid bricking cards
    if (command[2] == 0xFF && command[3] == 0xFF)
    {
        system_index = 0;
    }

    if (system_index == -1)
        return false;

    uint8_t request_code = command[4];
    if (request_code == 0x00)
        response[0] = 18;
    else
        response[0] = 20;

    if (request_code > 0x02)
        return false;

    // response code
    response[1] = 0x01;

    // time slot (unused)
    int n = command[5];

    memcpy(response + 2, idm, 8);
    memcpy(response + 10, pmm, 8);

    if (system_index > 0)
    {
        // update the top nibble of IDm with the system index
        response[2] = (system_index << 4) | (response[2] & 0x0F);
    }

    // system code request
    if (request_code == 0x01)
    {
        memcpy(response + 18, system_code + 2 * system_index, 2);
    }
    // communication performance request
    if (request_code == 0x02)
    {
        response[18] = 0x00; // reserved
        response[19] = 0x01; // only 212kbps communication is supported
    }

    return true;
}

bool request_service(packet_t command)
{
    if (command[0] < 11)
        return false;

    // number of nodes
    int n = command[10];
    if (!(1 <= n && n <= 32))
        return false;

    response[0] = 11 + 2 * n;

    response[10] = n;

    // always return key version 0
    for (int i = 0; i < n; i++)
    {
        response[11 + 2 * i] = 0x00;
        response[12 + 2 * i] = 0x00;
    }

    return true;
}

int parse_block_list(int n, const uint8_t *block_list, uint8_t *block_nums)
{
    int j = 0;
    for (int i = 0; i < n; i++)
    {
        if (block_list[j] == 0x80)
        {
            // 2-byte block list element
            block_nums[i] = block_list[j + 1];
            j += 2;
        }
        else if (block_list[j] == 0x00)
        {
            // 3-byte block list element
            if (block_list[j + 2] != 0x00)
                return 0;
            block_nums[i] = block_list[j + 1];
            j += 3;
        }
        else
        {
            return 0;
        }
    }
    // size of block list
    return j;
}

bool read_without_encryption(packet_t command)
{
    // check length
    if (command[0] < 16)
        return false;

    // number of services
    int m = command[10];

    if (m != 1)
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA1; // status flag 2

        return true;
    }

    uint16_t target_service_code = command[11] | (command[12] << 8);

    // number of blocks
    int n = command[13];

    // find service code
    bool service_found = false;
    for (int i = 0; i < SERVICE_MAX; i++)
    {
        uint16_t sc = service_code[2 * i] | (service_code[2 * i + 1] << 8);
        // if (sc == 0)
        //     continue;

        if (target_service_code == sc)
        {
            service_found = true;
            break;
        }
    }

    if (target_service_code == 0xFFFF)
    {
        service_found = true;
    }

    if (!service_found)
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA6; // status flag 2

        return true;
    }

    if (!(1 <= n && n <= BLOCK_MAX))
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA2; // status flag 2

        return true;
    }

    uint8_t block_nums[BLOCK_MAX];
    if (parse_block_list(n, command + 14, block_nums) == 0)
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA6; // status flag 2
        return true;
    }

    // load block data from EEPROM
    for (int i = 0; i < n; i++)
    {
        int block_num = block_nums[i];

        bool valid_block = false;

        uint8_t *dst = response + 13 + 16 * i;

        if (block_num < BLOCK_MAX)
        {
            valid_block = true;
            eeprom_read_block(dst, block_data_eep + 16 * block_num, 16);
        }
        else if (BLOCK_MAX <= block_num && block_num <= 0xF)
        {
            // we don't have space in EEPROM, so all 0 it is!
            valid_block = true;
            memset(dst, 0x00, 16);
        }
        else if (ERROR_BLOCK <= block_num && block_num < ERROR_BLOCK + LAST_ERROR_SIZE)
        {
            valid_block = true;
            eeprom_read_block(dst, last_error_eep + (block_num - ERROR_BLOCK) * 16, 16);
        }
        else if (0x81 <= block_num && block_num <= 0x92 && block_num != 0x89)
        {
            valid_block = true;
            switch (block_num)
            {
                case 0x81: // MAC
                    memset(dst, 0, 16);
                    break;

                case 0x82: // ID
                    memcpy(dst, idm, 8);
                    // Aime Amusement IC DFC
                    *(dst+8) = 0x00;
                    *(dst+9) = 0x78;
                    memset(dst + 10, 0x00, 6);
                    break;

                case 0x83: // D_ID
                    memcpy(dst, idm, 8);
                    memcpy(dst + 8, pmm, 8);
                    break;

                case 0x84: // SER_C
                    memcpy(dst, service_code, 2 * SERVICE_MAX);
                    memset(dst + 2 * SERVICE_MAX, 0x00, 16 - 2 * SERVICE_MAX);
                    break;

                case 0x85: // SYS_C
                    memcpy(dst, system_code, 2 * SYSTEM_MAX);
                    memset(dst + 2 * SYSTEM_MAX, 0x00, 16 - 2 * SYSTEM_MAX);
                    break;

                case 0x86: // CKV (used in MAC_A authentication)
                case 0x87: // CK
                    memset(dst, 0, 16);
                    break;

                case 0x88: // MC
                    memset(dst, 0xFF, 3); // access permission
                    // Since AIC uses 0x00, we can't make NDEF work :(
                    *(dst + 3) = 0x00; // NDEF compatability
                    *(dst + 4) = 0xFF; // RF parameter
                    memset(dst + 5, 0x00, 11); // memory config
                    break;

                case 0x90: // WCNT (used in MAC_A authentication)
                case 0x91: // MAC_A
                case 0x92: // STATE (used in MAC_A authentication)
                    memset(dst, 0x00, 16);
                    break;                
            }
        }

        if (!valid_block)
        {
            response[0] = 12;    // length
            response[10] = 0xFF; // status flag 1
            response[11] = 0xA8; // status flag 2
            return true;
        }
    }

    response[0] = 13 + 16 * n; // length

    response[10] = 0x00; // status flag 1
    response[11] = 0x00; // status flag 2

    response[12] = n; // number of blocks

    return true;
}

bool write_without_encryption(packet_t command)
{
    int len = command[0];
    int m = command[10]; // number of services
    uint16_t target_service_code = command[11] | (command[12] << 8);
    int n = command[13]; // number of blocks

    if (len < 32)
        return false;

    if (m != 1)
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA1; // status flag 2

        return true;
    }

    if (!(1 <= n && n <= BLOCK_MAX))
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA2; // status flag 2
        return true;
    }

    uint8_t block_nums[BLOCK_MAX];
    int N = parse_block_list(n, command + 14, block_nums);

    if (N == 0)
    {
        response[0] = 12;    // length
        response[10] = 0xFF; // status flag 1
        response[11] = 0xA6; // status flag 2
        return true;
    }

    // check length
    if (len != 14 + N + 16 * n)
        return false;

    // write block data to EEPROM
    for (int i = 0; i < n; i++)
    {
        int block_num = block_nums[i];

        bool valid_block = false;

        if (block_num < BLOCK_MAX)
        {
            valid_block = true;
            eeprom_update_block(command + 14 + N + 16 * i, block_data_eep + 16 * block_num, 16);
        }
        
        // On Mutual Authentication (refer to Felica Lite-S User Manual 5.4.2)
        // tl;dr: SEGA game server & card calculate MAC_A based on card-specific shared
        // key and session-specific random challenge; identical result == legit card
        // No replay attacks & copying cards
        // Eavesdropping is possible
        // "Security by Obsecurity"
        
        // In the process, the reader does the following:
        // - Write to RC (0x80)
        // - Read ID, WCNT and MAC_A simultaneously
        // - Write to STATE and MAC_A simultaneously
        // - Read data blocks and MAC_A simultaneously

        // For bootleg servers: The values don't matter at all, just respond anything
        // to these commands.
        // For official servers: Because there is currently no way to acquire the key
        // used in the MAC_A authentication process, SiliCa and similar emulation devices
        // will never work properly on SEGA arcades unless the keys were leaked.

        // RC
        if (block_num == 0x80)
        {
            valid_block = true;
        }

        // D_ID
        if (n == 1 && block_num == 0x83)
        {
            valid_block = true;

            // Update IDm
            memcpy(idm, command + 16, 8);
            eeprom_update_block(idm, idm_eep, 8);

            // Update PMm
            memcpy(pmm, command + 24, 8);
            eeprom_update_block(pmm, pmm_eep, 8);
        }

        // SER_C
        if (n == 1 && block_num == 0x84)
        {
            valid_block = true;

            memcpy(service_code, command + 16, 2 * SERVICE_MAX);
            eeprom_update_block(service_code, service_code_eep, 2 * SERVICE_MAX);
        }

        // SYS_C
        if (n == 1 && block_num == 0x85)
        {
            valid_block = true;

            memcpy(system_code, command + 16, 2 * SYSTEM_MAX);
            eeprom_update_block(system_code, system_code_eep, 2 * SYSTEM_MAX);
        }

        // STATE
        if (block_num == 0x90)
        {
            valid_block = true;
        }

        // MAC_A
        if (block_num == 0x91)
        {
            valid_block = true;
        }

        if (!valid_block)
        {
            response[0] = 12;    // length
            response[10] = 0xFF; // status flag 1
            response[11] = 0xA8; // status flag 2
            return true;
        }
    }

    response[0] = 12; // length

    response[10] = 0x00; // status flag 1
    response[11] = 0x00; // status flag 2

    return true;
}

bool search_service_code(int index)
{
    response[0] = 12;

    if (index < 0 || index >= SERVICE_MAX)
    {
        response[10] = 0xFF;
        response[11] = 0xFF;
        return true;
    }

    uint8_t sc1 = service_code[2 * index];
    uint8_t sc2 = service_code[2 * index + 1];

    if (sc1 == 0x00 && sc2 == 0x00)
    {
        response[10] = 0xFF;
        response[11] = 0xFF;
        return true;
    }

    response[10] = sc1;
    response[11] = sc2;

    return true;
}

bool request_system_code()
{
    int n = 0;

    for (int i = 0; i < SYSTEM_MAX; i++)
    {
        uint8_t sc1 = system_code[2 * i];
        uint8_t sc2 = system_code[2 * i + 1];

        if (sc1 == 0x00 && sc2 == 0x00)
            break;

        response[11 + 2 * i] = sc1;
        response[12 + 2 * i] = sc2;
        n++;
    }

    response[0] = 11 + 2 * n;
    response[10] = n;
    return n != 0;
}

// process application layer command and generate response
packet_t process(packet_t command)
{
    if (command == nullptr)
        return nullptr;

    const int len = command[0];
    const uint8_t command_code = command[1];

    // Polling
    if (command_code == 0x00)
    {
        if (polling(command))
            return response;
        else
            return nullptr;
    }

    // Echo
    if (command[1] == 0xF0 && command[2] == 0x00)
    {
        memcpy(response, command, len);
        return response;
    }

    // verify the tail of IDm matches
    if ((command[2] & 0x0F) != (idm[0] & 0x0F))
        return nullptr;
    if (memcmp(command + 3, idm + 1, 7) != 0)
        return nullptr;

    // check command code
    if (command_code % 2 != 0)
        return nullptr;

    // set response code
    response[1] = command_code + 1;

    // copy IDm from command to response
    memcpy(response + 2, command + 2, 8);

    switch (command_code)
    {
    case 0x02: // Request Service
        if (!request_service(command))
            return nullptr;
        break;
    case 0x04: // Request Response
        if (len != 10)
            return nullptr;

        response[0] = 11;
        response[10] = 0x00;

        break;
    case 0x06: // Read Without Encryption
        if (!read_without_encryption(command))
            return nullptr;
        // status flag 1
        if (response[10] != 0x00)
        {
            save_error(command);
            Serial_println("Read failed");
            print_packet(command);
        }
        break;
    case 0x08: // Write Without Encryption
        if (!write_without_encryption(command))
            return nullptr;
        break;
    case 0x0A: // Search Service Code
    {
        // Define in switch is prohibited in C++
        // use a block here
        if (len != 12)
            return nullptr;

        int index = command[10] | (command[11] << 8);
        search_service_code(index);
    }
    break;
    case 0x0C: // Request System Code
        if (len != 10)
            return nullptr;
        if (!request_system_code())
            return nullptr;
        break;
    case 0x10: // Authentication1
    // pass through for unsupported commands
    default:
        return nullptr;
    }

    return response;
}

void save_error(packet_t command)
{
    int len = command[0];
    if (len > sizeof(last_error_eep))
        len = sizeof(last_error_eep);

    eeprom_update_block(command, last_error_eep, len);
}

// Debug: print packet to serial
void print_packet(packet_t packet)
{
    int len = packet[0];
    if (len == 0)
    {
        Serial_println("<empty>");
        return;
    }

    for (int i = 1; i < len; i++)
    {
        char hex_str[5];
        sprintf(hex_str, "%02X", packet[i]);
        Serial_print(hex_str);
        if (i != len - 1)
            Serial_print(" ");
    }
    Serial_println("");
}