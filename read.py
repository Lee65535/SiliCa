import nfc

COMMAND_READ = 0x06


def read_system_block(tag, timeout=1.0) -> bytes:
    cmd_data = bytearray([1, 0xFF, 0xFF, 2, 0x80, 0xE0, 0x80, 0xE1])
    return tag.send_cmd_recv_rsp(COMMAND_READ, bytes(cmd_data), timeout)[1:]

def read_block(tag, block_num, timeout=1.0) -> bytes:
    cmd_data = bytearray([1, 0xFF, 0xFF, 1, 0x80, block_num])
    return tag.send_cmd_recv_rsp(COMMAND_READ, bytes(cmd_data), timeout)[1:]

def main(argv):
    if len(argv) >= 4:
        print(f'Usage: {argv[0]} err | block_num_hex')
        return 1

    with nfc.ContactlessFrontend("tty") as clf:
        print("Waiting for a FeliCa...")
        tag = clf.connect(
            rdwr={"targets": ["212F"], 'on-connect': lambda tag: False})
        print("Tag found:", tag)

        if (argv[1] == 'err'):
            try:
                data = read_system_block(tag)
            except nfc.tag.tt3.Type3TagCommandError:
                print("Unable to read system block. The tag might not be a SiliCa.")
                return 1

            length = data[0]

            print("Last Error Command:", data[1:length].hex(' ').upper())
        
        elif argv[1] in ('dfc', 'ID'):
            cmd_data = bytearray([1, 0x00, 0x00, 1, 0x80, 0x82])
            data = tag.send_cmd_recv_rsp(COMMAND_READ, bytes(cmd_data), 1)[1:]
            print("block 0x82 (ID) data:", data.hex(' ').upper())

        elif argv[1] in ('mac', 'MAC', 'MAC_A'):
            block_num = int(argv[2], 16)
            cmd_data = bytearray([1, 0xFF, 0xFF, 2, 0x80, block_num, 0x80, 0x91])
            data = tag.send_cmd_recv_rsp(COMMAND_READ, bytes(cmd_data), 1)[1:]
            print("block", argv[2], "data:\t", data[:16].hex(' ').upper())
            print("block", "MAC_A", "data:\t", data[16:].hex(' ').upper())
            
        else:
            block_num = int(argv[1], 16)
            data = read_block(tag, block_num)
            print("block", argv[1], "data:", data.hex(' ').upper())


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv))
