#!/usr/bin/env python3
"""Write an NDEF Text record to a NFC Forum Type 4 Tag via PC/SC.

Tested protocol flow:
  SELECT NDEF application -> SELECT CC -> READ CC
  SELECT NDEF file -> UPDATE BINARY

The tag is expected to accept writable NDEF updates, as the nRF54L15
application does through nfc_t4t_ndef_rwpayload_set().
"""

import argparse
import sys
import time
from typing import Iterable

from smartcard.Exceptions import CardConnectionException, NoCardException
from smartcard.System import readers


NDEF_APPLICATION = bytes.fromhex("D2760000850101")
CC_FILE_ID = bytes.fromhex("E103")
DEFAULT_CC_SIZE = 15


def apdu(connection, command: Iterable[int], description: str) -> bytes:
    response, sw1, sw2 = connection.transmit(list(command))
    status = (sw1 << 8) | sw2

    if status != 0x9000:
        raise RuntimeError(
            f"{description} failed: SW={sw1:02X}{sw2:02X}, "
            f"response={bytes(response).hex().upper()}"
        )

    return bytes(response)


def select_file(connection, file_id: bytes) -> None:
    description = f"SELECT file {file_id.hex().upper()}"
    command = [0x00, 0xA4, 0x00, 0x0C, len(file_id), *file_id]
    response, sw1, sw2 = connection.transmit(command)
    status = (sw1 << 8) | sw2

    if status == 0x9000:
        return

    # Some PC/SC contactless readers require Le=00 for SELECT by DF name,
    # while Type 4 Tag examples commonly omit Le for SELECT by file ID.
    response, sw1, sw2 = connection.transmit(command + [0x00])
    status = (sw1 << 8) | sw2
    if status != 0x9000:
        raise RuntimeError(
            f"{description} failed: SW={sw1:02X}{sw2:02X}, "
            f"response={bytes(response).hex().upper()}"
        )


def read_binary(connection, offset: int, length: int) -> bytes:
    if not 0 <= offset <= 0xFFFF:
        raise ValueError("READ BINARY offset is out of range")
    if not 1 <= length <= 256:
        raise ValueError("READ BINARY length must be 1..256")

    response, sw1, sw2 = connection.transmit(
        [0x00, 0xB0, offset >> 8, offset & 0xFF, length & 0xFF]
    )

    if sw1 == 0x6C:
        response, sw1, sw2 = connection.transmit(
            [0x00, 0xB0, offset >> 8, offset & 0xFF, sw2]
        )

    if (sw1 << 8) | sw2 != 0x9000:
        raise RuntimeError(
            f"READ BINARY failed: SW={sw1:02X}{sw2:02X}, "
            f"offset=0x{offset:04X}"
        )

    return bytes(response)


def update_binary(connection, offset: int, data: bytes) -> None:
    if not 0 <= offset <= 0xFFFF:
        raise ValueError("UPDATE BINARY offset is out of range")
    if not data:
        return
    if len(data) > 255:
        raise ValueError("UPDATE BINARY chunk is larger than 255 bytes")

    apdu(
        connection,
        [0x00, 0xD6, offset >> 8, offset & 0xFF, len(data), *data],
        f"UPDATE BINARY at 0x{offset:04X}",
    )


def encode_ndef_text(text: str, language: str = "en") -> bytes:
    language_bytes = language.encode("ascii")
    text_bytes = text.encode("utf-8")

    if not 1 <= len(language_bytes) <= 63:
        raise ValueError("language must contain 1..63 ASCII bytes")

    payload = bytes([len(language_bytes)]) + language_bytes + text_bytes
    if len(payload) > 255:
        raise ValueError("NDEF Text payload is too large for a short record")

    # MB=1, ME=1, SR=1, TNF=Well-Known (0x01); type is "T".
    record = bytes([0xD1, 0x01, len(payload), 0x54]) + payload
    return record


def parse_cc(cc: bytes) -> tuple[bytes, int, int]:
    if len(cc) < 7:
        raise RuntimeError(
            f"Capability Container is too short: received {len(cc)} bytes"
        )

    cc_length = int.from_bytes(cc[0:2], "big")
    if cc_length < 7:
        raise RuntimeError(
            f"Capability Container has an invalid CCLEN value: {cc_length}"
        )
    if cc_length > len(cc):
        raise RuntimeError(
            f"Capability Container is truncated: CCLEN={cc_length}, "
            f"received={len(cc)}"
        )

    # Bytes 0..6 are the Type 4 Tag CC header. The TLV area starts after
    # CCLEN, mapping version, MLe, and MLc.
    index = 7
    end = cc_length
    while index + 2 <= end:
        tag = cc[index]
        if tag == 0x00:
            index += 1
            continue
        if tag == 0xFE:
            break

        length = cc[index + 1]
        index += 2
        if length == 0xFF:
            if index + 2 > end:
                raise RuntimeError("Capability Container contains a truncated TLV")
            length = int.from_bytes(cc[index:index + 2], "big")
            index += 2
        if index + length > end:
            raise RuntimeError("Capability Container contains a truncated TLV")

        value = cc[index:index + length]
        index += length

        # NDEF File Control TLV.
        if tag == 0x04 and length >= 6:
            file_id = value[0:2]
            max_size = int.from_bytes(value[2:4], "big")
            read_access = value[4]
            write_access = value[5]
            if write_access == 0xFF:
                raise PermissionError("The NDEF file is read-only")
            if max_size < 2:
                raise RuntimeError("The NDEF file is too small")
            return file_id, max_size, read_access

    raise RuntimeError("No NDEF File Control TLV found in Capability Container")


def choose_reader(reader_index: int | None):
    available = readers()
    if not available:
        raise RuntimeError("No PC/SC reader found. Check the OMNIKEY 5022 driver.")

    if reader_index is not None:
        if not 0 <= reader_index < len(available):
            raise ValueError(f"reader index must be 0..{len(available) - 1}")
        return available[reader_index]

    for index, reader in enumerate(available):
        if "OMNIKEY 5022" in str(reader).upper():
            return reader

    if len(available) == 1:
        return available[0]

    names = "\n".join(f"  [{i}] {reader}" for i, reader in enumerate(available))
    raise RuntimeError(
        "Several PC/SC readers found; select one with --reader-index:\n" + names
    )


def select_ndef_application(connection) -> None:
    apdu(
        connection,
        [0x00, 0xA4, 0x04, 0x00, len(NDEF_APPLICATION), *NDEF_APPLICATION, 0x00],
        "SELECT NDEF application",
    )


def read_ndef(connection) -> None:
    select_ndef_application(connection)
    select_file(connection, CC_FILE_ID)
    cc = read_binary(connection, 0, DEFAULT_CC_SIZE)
    ndef_file_id, max_size, _ = parse_cc(cc)

    select_file(connection, ndef_file_id)
    nlen = int.from_bytes(read_binary(connection, 0, 2), "big")
    if nlen > max_size - 2:
        raise RuntimeError(
            f"Tag returned invalid NLEN {nlen} (maximum {max_size - 2})"
        )

    actual = read_binary(connection, 2, nlen) if nlen else b""
    print(f"NDEF file: {ndef_file_id.hex().upper()}")
    print(f"NLEN: {nlen}")
    print(f"NDEF length: {len(actual)}")
    print(f"NDEF hex: {actual.hex(' ').upper()}")
    print(f"NDEF text: {actual.decode('utf-8', errors='replace')}")


def write_tag(
    connection,
    ndef_record: bytes,
    verify: bool,
) -> None:
    apdu(
        connection,
        # ISO/IEC 7816-4 SELECT by DF name. Le=00 asks for the response
        # data and is required by some PC/SC contactless readers.
        [0x00, 0xA4, 0x04, 0x00, len(NDEF_APPLICATION), *NDEF_APPLICATION, 0x00],
        "SELECT NDEF application",
    )

    select_file(connection, CC_FILE_ID)
    cc = read_binary(connection, 0, DEFAULT_CC_SIZE)
    ndef_file_id, max_size, _ = parse_cc(cc)

    if len(ndef_record) + 2 > max_size:
        raise ValueError(
            f"NDEF record needs {len(ndef_record) + 2} bytes, "
            f"but the tag allows {max_size}"
        )

    select_file(connection, ndef_file_id)

    # Set NLEN to zero first, write the payload, then publish the final
    # length. This is the Type 4 Tag safe-update sequence.
    update_binary(connection, 0, b"\x00\x00")

    # MLc is commonly 255 for Type 4 tags. Keep chunks conservative for
    # readers that reject extended APDUs.
    chunk_size = 200
    for offset in range(0, len(ndef_record), chunk_size):
        update_binary(
            connection,
            2 + offset,
            ndef_record[offset:offset + chunk_size],
        )
    update_binary(connection, 0, len(ndef_record).to_bytes(2, "big"))

    if verify:
        time.sleep(0.05)
        select_file(connection, ndef_file_id)
        nlen = int.from_bytes(read_binary(connection, 0, 2), "big")
        actual = read_binary(connection, 2, nlen) if nlen else b""
        print(f"Readback NLEN: {nlen}")
        print(f"Readback length: {len(actual)}")
        print(f"Readback hex: {actual.hex(' ').upper()}")
        print(f"Readback text: {actual.decode('utf-8', errors='replace')}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Write an NDEF Text record using an OMNIKEY 5022 PC/SC reader."
    )
    parser.add_argument("--text", help="Text to write as an NDEF Text record")
    parser.add_argument("--file", help="UTF-8 text file containing the NDEF text")
    parser.add_argument("--language", default="en", help="NDEF language code")
    parser.add_argument("--reader-index", type=int)
    parser.add_argument("--list-readers", action="store_true")
    parser.add_argument(
        "--read",
        action="store_true",
        help="Read and display the current NDEF message without writing",
    )
    parser.add_argument("--no-verify", action="store_true")
    args = parser.parse_args()

    available = readers()
    if args.list_readers:
        for index, reader in enumerate(available):
            print(f"[{index}] {reader}")
        return 0

    if args.read and (args.text or args.file):
        parser.error("--read cannot be combined with --text or --file")

    if not args.read and bool(args.text) == bool(args.file):
        parser.error("specify exactly one of --text or --file")

    reader = choose_reader(args.reader_index)
    print(f"Using reader: {reader}")

    connection = reader.createConnection()
    try:
        connection.connect()
        if args.read:
            read_ndef(connection)
        else:
            text = args.text
            if args.file:
                with open(args.file, "r", encoding="utf-8") as input_file:
                    text = input_file.read().rstrip("\r\n")

            assert text is not None
            record = encode_ndef_text(text, args.language)
            write_tag(connection, record, not args.no_verify)
    except (CardConnectionException, NoCardException) as exc:
        print(f"PC/SC error: {exc}", file=sys.stderr)
        print("Keep the tag on the OMNIKEY 5022 during the whole operation.", file=sys.stderr)
        return 2
    except (PermissionError, RuntimeError, ValueError) as exc:
        print(f"Write failed: {exc}", file=sys.stderr)
        return 1
    finally:
        connection.disconnect()

    if args.read:
        print("NDEF read completed successfully.")
    else:
        print("NDEF write completed successfully.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
