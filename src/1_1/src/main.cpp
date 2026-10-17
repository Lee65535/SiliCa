// Implementation of the application layer for
// JIS X 6319-4 compatible card "SiliCa"

#include <stddef.h>
#include <string.h>
#include <avr/eeprom.h>
#include "silica.h"

static constexpr int BLOCK_MAX = 12;
static constexpr int SYSTEM_MAX = 4;
static constexpr int SERVICE_MAX = 4;

constexpr int LAST_ERROR_SIZE = 2;

static uint8_t idm[8];
static uint8_t pmm[8];


static uint8_t service_code[2 * SERVICE_MAX];
static uint8_t system_code[2 * SYSTEM_MAX];



static const int ERROR_BLOCK = 0xE0;

// Layout of the EEPROM.
// The addresses of separate EEMEM variables are chosen by the compiler and
// change between builds, which would scramble the data of cards programmed
// with another firmware. A single struct keeps the layout fixed. Do not
// reorder or resize the members.
struct EepromLayout
{
    uint8_t system_code[2 * SYSTEM_MAX];     // 0x00
    uint8_t service_code[2 * SERVICE_MAX];   // 0x08
    uint8_t pmm[8];                          // 0x10
    uint8_t idm[8];                          // 0x18
    uint8_t block_data[16 * BLOCK_MAX];      // 0x20
    uint8_t last_error[16 * LAST_ERROR_SIZE]; // 0xE0
};
static_assert(sizeof(EepromLayout) == 0x100, "unexpected EEPROM layout");
static_assert(offsetof(EepromLayout, system_code) == 0x00 && offsetof(EepromLayout, idm) == 0x18 &&
                  offsetof(EepromLayout, block_data) == 0x20 && offsetof(EepromLayout, last_error) == 0xE0,
              "unexpected EEPROM layout");

static EepromLayout EEMEM eeprom_layout;

static uint8_t response[0xFF] = {};

void initialize()
{
    // read parameters from EEPROM
    eeprom_read_block(idm, eeprom_layout.idm, 8);
    eeprom_read_block(pmm, eeprom_layout.pmm, 8);
    eeprom_read_block(service_code, eeprom_layout.service_code, 2 * SERVICE_MAX);
    eeprom_read_block(system_code, eeprom_layout.system_code, 2 * SYSTEM_MAX);
}

bool polling(packet_t command)
{
    // LEN, command code, system code (2), request code, time slot
    if (command[0] < 6)
        return false;

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
    if (request_code > 0x02)
        return false;

    if (request_code == 0x00)
        response[0] = 18;
    else
        response[0] = 20;

    // response code
    response[1] = 0x01;

    // time slot (unused)

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

    // the command must contain all n node codes
    if (command[0] < 11 + 2 * n)
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

// set the status flags of an error (or success) response
static void set_status(uint8_t flag1, uint8_t flag2)
{
    response[0] = 12; // length
    response[10] = flag1;
    response[11] = flag2;
}

// Parse a block list of n elements, available bytes at most max_size.
// Returns the size of the block list, or 0 if it is malformed.
int parse_block_list(int n, const uint8_t *block_list, int max_size, uint8_t *block_nums)
{
    int j = 0;
    for (int i = 0; i < n; i++)
    {
        if (j + 2 > max_size)
            return 0;

        if (block_list[j] == 0x80)
        {
            // 2-byte block list element
            block_nums[i] = block_list[j + 1];
            j += 2;
        }
        else if (block_list[j] == 0x00)
        {
            // 3-byte block list element
            if (j + 3 > max_size || block_list[j + 2] != 0x00)
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
    if (parse_block_list(n, command + 14, command[0] - 14, block_nums) == 0)
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
            eeprom_read_block(dst, eeprom_layout.block_data + 16 * block_num, 16);
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
            eeprom_read_block(dst, eeprom_layout.last_error + (block_num - ERROR_BLOCK) * 16, 16);
        }
        else
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

                default: // 0x80 (RC), 0x89-0x8F and others are not readable
                    valid_block = false;
                    break;
            }
        }

        if (!valid_block)
        {
            // status flag 1 indicates the position of the invalid block
            set_status(i < 8 ? (1 << i) : 0xFF, 0xA8);
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

    if (len < 32)
        return false;

    int m = command[10]; // number of services
    int n = command[13]; // number of blocks

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
    int N = parse_block_list(n, command + 14, len - 14, block_nums);

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

        // block data follows the block list
        const uint8_t *data = command + 14 + N + 16 * i;

        if (block_num < BLOCK_MAX)
        {
            valid_block = true;
            eeprom_update_block(data, eeprom_layout.block_data + 16 * block_num, 16);
        }
        else if (block_num <= 0xF)
        {
            // reads of these blocks return all 0, so accept and discard writes
            valid_block = true;
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
            memcpy(idm, data, 8);
            eeprom_update_block(idm, eeprom_layout.idm, 8);

            // Update PMm
            memcpy(pmm, data + 8, 8);
            eeprom_update_block(pmm, eeprom_layout.pmm, 8);
        }

        // SER_C
        if (n == 1 && block_num == 0x84)
        {
            valid_block = true;

            memcpy(service_code, data, 2 * SERVICE_MAX);
            eeprom_update_block(service_code, eeprom_layout.service_code, 2 * SERVICE_MAX);
        }

        // SYS_C
        if (n == 1 && block_num == 0x85)
        {
            valid_block = true;

            memcpy(system_code, data, 2 * SYSTEM_MAX);
            eeprom_update_block(system_code, eeprom_layout.system_code, 2 * SYSTEM_MAX);
        }

        // STATE (0x92). 0x90 (WCNT) is also accepted and ignored, as before.
        if (block_num == 0x92 || block_num == 0x90)
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
            // status flag 1 indicates the position of the invalid block
            set_status(i < 8 ? (1 << i) : 0xFF, 0xA8);
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
            LOG("Read failed");
            LOG_PACKET(command);
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
        // Only commands addressed to this card are logged, see silica.h.
        LOG("Unsupported command");
        LOG_PACKET(command);
        return nullptr;
    }

    return response;
}

#ifdef DEBUG_LOG
void save_error(packet_t command)
{
    size_t len = command[0];
    if (len > sizeof(eeprom_layout.last_error))
        len = sizeof(eeprom_layout.last_error);

    eeprom_update_block(command, eeprom_layout.last_error, len);
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
        Serial_print_hex(packet[i]);
        if (i != len - 1)
            Serial_print(" ");
    }
    Serial_println("");
}
#endif // DEBUG_LOG
