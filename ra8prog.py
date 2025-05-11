#!/usr/bin/env python3

import serial
import time
import argparse
from intelhex import IntelHex

# FYI: dmesg | grep -i tty
# ==== 設定値 ====
# PORT = '/dev/ttyACM0'
# BAUD = 9600
# HEX_FILE = './build/Debug/RA8E1_prj.ihex'
# CHUNK_SIZE = 128
# usage :./ra8prog.py --port /dev/ttyACM0 --file ./build/Debug/RA8E1_prj.ihex --baud 9600
# ==== 共通関数 ====
def checksum(data):
    return (256 - sum(data)) & 0xFF

def send_command(ser, cmd_id, start_addr, end_addr):
    packet = bytearray()
    packet.append(0x01)
    packet.extend([0x00, 0x09])
    packet.append(cmd_id)
    packet.extend(start_addr.to_bytes(4, 'big'))
    packet.extend(end_addr.to_bytes(4, 'big'))
    packet.append(checksum(packet[1:]))
    packet.append(0x03)
    print(f"[→] CMD: {packet.hex()}")
    ser.write(packet)
    time.sleep(0.1)
    resp = ser.read_all()
    print(f"[←] RES: {resp.hex()}")
    if len(resp) >= 4:
        return resp[3]
    return None

def send_data_packet(ser, data):
    packet = bytearray()
    packet.append(0x81)
    size = len(data) + 1
    packet.extend(divmod(size, 256))
    packet.append(0x13)
    packet.extend(data)
    packet.append(checksum(packet[1:]))
    packet.append(0x03)
    print(f"[→] DATA: len={len(data)}")
    ser.write(packet)
    time.sleep(0.15)
    resp = ser.read_all()
    if len(resp) >= 4 and resp[3] == 0x13:
        return True
    print(f"[!] Error: RES = {hex(resp[3]) if len(resp) >= 4 else 'Unknown'}")
    return False

# ==== アドレス連続ブロック ====
def split_continuous_blocks(addresses):
    blocks = []
    if not addresses:
        return blocks
    block = [addresses[0]]
    for i in range(1, len(addresses)):
        if addresses[i] == addresses[i - 1] + 1:
            block.append(addresses[i])
        else:
            blocks.append(block)
            block = [addresses[i]]
    blocks.append(block)
    return blocks

# ==== メイン処理 ====
def main():
    parser = argparse.ArgumentParser(description="Hex writer tool for RA8E1")
    parser.add_argument('--port', required=True, help='COMポート名 (例: COM3 または /dev/ttyACM0)')
    parser.add_argument('--file', required=True, help='HEXファイルパス')
    parser.add_argument('--baud', default=9600, type=int, help='ボーレート (デフォルト: 9600)')
    args = parser.parse_args()
    # ==== 有効な書き込み範囲 ====
    FLASH_START = 0x02000000
    FLASH_END = 0x020FFFFF
    CHUNK_SIZE = 128

    ih = IntelHex()
    ih.loadhex(args.file)
    all_addrs = sorted(ih.addresses())

    # Flash領域に限定
    valid_addrs = all_addrs #[addr for addr in all_addrs if FLASH_START <= addr <= FLASH_END]
    blocks = split_continuous_blocks(valid_addrs)

    with serial.Serial(args.port, args.baud, timeout=1) as ser:
        print("[*] 初期同期...")
        for _ in range(3): ser.write(b'\x00'); time.sleep(0.05)
        resp = ser.read()
        print(f"[←] 応答{resp.hex()}")
        time.sleep(3.0)

        ser.write(b'\x55')
        print(f"[→] boot code:")
        resp = ser.read()
        print(f"[←] 応答{resp.hex()}")

        print(f"[→] イレースコマンド発行中")
        status = send_command(ser, 0x12, FLASH_START, FLASH_END)
        time.sleep(0.5)
        resp = ser.read_all()
        print(f"[←] 応答:{resp.hex()}")
        time.sleep(5)

        print("[*] 書き込み中...")
        for block in blocks:
            for i in range(0, len(block), CHUNK_SIZE):
                chunk_addrs = block[i:i+CHUNK_SIZE]
                start = chunk_addrs[0]
                end = chunk_addrs[-1]
                chunk_data = bytearray([ih[addr] for addr in chunk_addrs])

                # パディング処理（必要に応じてWAUサイズに合わせて）
                if len(chunk_data) % CHUNK_SIZE != 0:
                    padding = CHUNK_SIZE - (len(chunk_data) % CHUNK_SIZE)
                    chunk_data.extend([0xFF] * padding)
                    end = start + len(chunk_data) - 1

                print(f"[{i:#06x}] Write: {start:#010x} - {end:#010x}")
                status = send_command(ser, 0x13, start, end)
                if status != 0x13:
                    print("[!] 書き込みコマンド失敗、スキップ")
                    continue

                if not send_data_packet(ser, chunk_data):
                    print("[!] 書き込み失敗、停止")
                    break

        print("[✓] 書き込み完了")

if __name__ == "__main__":
    main()