import nfc
import random

COMMAND_READ = 0x06
COMMAND_WRITE = 0x08
MAX_BLOCK = 12

D_ID = 0x83
SER_C = 0x84
SYS_C = 0x85


def check(tag, timeout=1.0):
    for block_num in list(range(MAX_BLOCK)) + [D_ID, SER_C, SYS_C]:
        data = random.randbytes(16)
        cmd_write = bytearray([1, 0xFF, 0xFF, 1, 0x80, block_num]) + data

        tag.send_cmd_recv_rsp(COMMAND_WRITE, bytes(cmd_write), timeout)

        if block_num == D_ID:  # Update IDm if written
            tag.idm = data[0:8]

        cmd_read = bytearray([1, 0xFF, 0xFF, 1, 0x80, block_num])
        ret = tag.send_cmd_recv_rsp(
            COMMAND_READ, bytes(cmd_read), timeout)[1:]

        if block_num < MAX_BLOCK or block_num == D_ID:
            assert ret == data, f"Data mismatch in block {block_num}"
        else:
            assert ret[0:8] == data[0:8], \
                f"Data mismatch in system block {block_num}"


def main():
    with nfc.ContactlessFrontend("tty") as clf:
        print("Waiting for a FeliCa...")
        tag = clf.connect(
            rdwr={"targets": ["212F"], 'on-connect': lambda tag: False})
        print("Tag found:", tag)

        check(tag)
        check(tag)  # run twice to be sure

        print("check succeeded")
        return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
