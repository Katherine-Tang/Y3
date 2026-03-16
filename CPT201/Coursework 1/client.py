import argparse
import hashlib
import json
import os
import struct
import threading
import time
from socket import *
import csv
from datetime import datetime
import zlib
import base64
import hashlib
from Crypto.Cipher import AES
from Crypto.Random import get_random_bytes

# Const Value
OP_SAVE, OP_DELETE, OP_GET, OP_UPLOAD, OP_DOWNLOAD, OP_LOGIN = 'SAVE', 'DELETE', 'GET', 'UPLOAD', 'DOWNLOAD', 'LOGIN'
TYPE_FILE, TYPE_DATA, TYPE_AUTH, DIR_EARTH = 'FILE', 'DATA', 'AUTH', 'EARTH'

MAX_PACKET_SIZE = 20480
SERVER_IP = '127.0.0.1'
SERVER_PORT = 1379
ID = None
TOKEN = None
FILE = None

LOCK = threading.Lock()
MAX_THREAD = 16
# Adaptive-threading: user override and cached CSV mapping
USER_SELECTED_THREAD = None
_BIN_THREAD_MAPPING = None  # list of tuples: (lower_mb, upper_mb, best_thread, label)

# debug flags
DEBUG_CORRUPT = False
DEBUG_MULTI_THREAD = False
DEBUG_TIMER = False

FORCE = False

# AES encryptor
class AESEncryptor:
    def __init__(self, key=None):
        if key is None:
            self.key = hashlib.sha256(b"default_server_key_2024").digest()
        else:
            if len(key) != 32:
                raise ValueError("Key must be 32 bytes long for AES-256")
            self.key = key

    def encrypt_file(self, input_path: str, output_path: str) -> bool:
        try:
            with open(input_path, 'rb') as f:
                plaintext = f.read()

            nonce = get_random_bytes(16)
            cipher = AES.new(self.key, AES.MODE_GCM, nonce=nonce)
            ciphertext, tag = cipher.encrypt_and_digest(plaintext)

            with open(output_path, 'wb') as f:
                f.write(nonce)
                f.write(tag)
                f.write(ciphertext)

            print(f" [ENCRYPT] Success: {os.path.basename(input_path)} -> {os.path.basename(output_path)}")
            return True

        except Exception as e:
            print(f" [ENCRYPT] Failed: {os.path.basename(input_path)} -> {e}")
            return False

    def decrypt_file(self, input_path: str, output_path: str) -> bool:
        try:
            with open(input_path, 'rb') as f:
                nonce = f.read(16)
                tag = f.read(16)
                ciphertext = f.read()

            cipher = AES.new(self.key, AES.MODE_GCM, nonce=nonce)
            plaintext = cipher.decrypt_and_verify(ciphertext, tag)

            with open(output_path, 'wb') as f:
                f.write(plaintext)
            print(f" [DECRYPT] Success: {os.path.basename(input_path)} -> {os.path.basename(output_path)}")
            return True

        except Exception as e:
            print(f" [DECRYPT] Failed: {os.path.basename(input_path)} -> {e}")
            return False


encryptor = AESEncryptor()


def encrypt_compressed_file(compressed_path: str) -> tuple:
    try:
        print(f" [ENCRYPT] Step 1: Reading compressed file: {os.path.basename(compressed_path)}")
        compressed_size = os.path.getsize(compressed_path)
        print(f" [ENCRYPT] Step 2: Compressed file size: {compressed_size} bytes")
        encrypted_path = compressed_path + ".enc"
        print(f" [ENCRYPT] Step 3: Starting encryption...")

        if encryptor.encrypt_file(compressed_path, encrypted_path):
            encrypted_size = os.path.getsize(encrypted_path)
            print(f" [ENCRYPT] ✓ Encryption completed successfully!")
            print(f"   Compressed size: {compressed_size} bytes")
            print(f"   Encrypted size: {encrypted_size} bytes")
            print(f"   Encryption overhead: {((encrypted_size - compressed_size) / compressed_size * 100):+.2f}%")
            print()
            return encrypted_path, encrypted_size, True
        else:
            print(f" [ENCRYPT] ✗ Encryption failed!")
            print()
            return compressed_path, compressed_size, False

    except Exception as e:
        print(f" [ENCRYPT] ✗ Encryption error: {e}")
        return compressed_path, os.path.getsize(compressed_path), False


def encrypt_original_file(original_path: str) -> tuple:
    try:
        print(f" [ENCRYPT] Step 1: Reading original file: {os.path.basename(original_path)}")
        original_size = os.path.getsize(original_path)
        print(f" [ENCRYPT] Step 2: Original file size: {original_size} bytes")
        encrypted_path = original_path + ".enc"
        print(f" [ENCRYPT] Step 3: Starting encryption...")

        if encryptor.encrypt_file(original_path, encrypted_path):
            encrypted_size = os.path.getsize(encrypted_path)
            print(f" [ENCRYPT] ✓ Encryption completed successfully!")
            print(f"   Original size: {original_size} bytes")
            print(f"   Encrypted size: {encrypted_size} bytes")
            print(f"   Encryption overhead: {((encrypted_size - original_size) / original_size * 100):+.2f}%")
            return encrypted_path, encrypted_size, True
        else:
            print(f" [ENCRYPT] ✗ Encryption failed!")
            return original_path, original_size, False

    except Exception as e:
        print(f" [ENCRYPT] ✗ Encryption error: {e}")
        return original_path, os.path.getsize(original_path), False


# already highly compressed suffix
ALREADY_COMPRESSED_EXTS = {
    ".mp3", ".aac", ".wav", ".flac", ".mp4", ".mkv", ".mov",
    ".zip", ".rar", ".7z", ".gz", ".bz2", ".xz",
    ".pdf", ".pptx", ".docx", ".xlsx"
}


def _ext(path: str) -> str:
    return os.path.splitext(path)[1].lower()


def should_try_compress(input_path: str) -> bool:
    return _ext(input_path) not in ALREADY_COMPRESSED_EXTS


# compression
def compress_file_lz78_huffman(input_path: str,
                               benefit_threshold: float = 0.05,
                               force: bool = False,
                               include_exts: set = None):
    with open(input_path, 'rb') as f:
        raw = f.read()
    original_size = len(raw)

    ext = _ext(input_path)
    try_compress = force or (include_exts and ext in include_exts) or should_try_compress(input_path)

    if not try_compress:
        print(f"[Notice] File '{os.path.basename(input_path)}' skipped compression (already compressed type).")
        return input_path, original_size, original_size, False

    # judge the size of files
    lz_bytes = zlib.compress(raw, level=9)
    b85 = base64.b85encode(lz_bytes)
    compressed_size = len(b85)
    gain = (original_size - compressed_size) / max(1, original_size)

    if (not force) and (gain < benefit_threshold):
        print(
            f"[Notice] File '{os.path.basename(input_path)}' skipped compression (gain {gain * 100:.2f}% < threshold).")
        return input_path, original_size, original_size, False

    out_path = input_path + ".cmp"
    with open(out_path, 'wb') as f:
        f.write(b85)

    print("\n [Compression Report] ")
    print(f" Original file: {os.path.basename(input_path)}")
    print(f" Original size: {original_size / 1024:.2f} KB")
    print(f" Compressed size: {compressed_size / 1024:.2f} KB")
    ratio = (1 - compressed_size / original_size) * 100 if original_size else 0
    print(f" Compression ratio: {ratio:.2f}% smaller\n")
    return out_path, original_size, compressed_size, True


# decompression
def decompress_file_lz78_huffman(input_path: str, output_path: str) -> str:
    with open(input_path, 'rb') as f:
        b85 = f.read()
    received_size = len(b85)

    lz_bytes = base64.b85decode(b85)
    raw = zlib.decompress(lz_bytes)

    with open(output_path, 'wb') as f:
        f.write(raw)

    restored_size = len(raw)
    print("\n[Decompression Report]")
    print(f"Received file: {os.path.basename(input_path)}")
    print(f"Received (compressed) size: {received_size / 1024:.2f} KB")
    print(f"Decompressed (restored) size: {restored_size / 1024:.2f} KB")
    print(f"Size increased by: {(restored_size - received_size) / 1024:.2f} KB\n")
    return output_path


def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)


# ASCII Table Printer
def _stringify_matrix(headers, rows):
    hdrs = [str(h) for h in headers]
    body = [["" if c is None else str(c) for c in row] for row in rows]
    return hdrs, body


def _column_widths(headers, rows):
    widths = [len(h) for h in headers]
    for row in rows:
        for i, cell in enumerate(row):
            if i < len(widths):
                widths[i] = max(widths[i], len(cell))
            else:
                widths.append(len(cell))
    return widths


def print_table(headers, rows):
    headers, rows = _stringify_matrix(headers, rows)
    if not headers:
        return
    widths = _column_widths(headers, rows)

    def hline():
        return "+" + "+".join("-" * (w + 2) for w in widths) + "+"

    def fmt_row(row):
        cells = []
        for i, w in enumerate(widths):
            val = row[i] if i < len(row) else ""
            cells.append(" " + val.ljust(w) + " ")
        return "|" + "|".join(cells) + "|"

    print(hline())
    print(fmt_row(headers))
    print(hline())
    for r in rows:
        print(fmt_row(r))
    print(hline())


# Adaptive Threading
def _parse_size_bin(label: str):
    s = label.strip().upper().replace("MB", "")
    try:
        if s.startswith("<"):
            upper = float(s[1:])
            return 0.0, upper
        if s.startswith(">"):
            lower = float(s[1:])
            return lower, float("inf")
        if "-" in s:
            parts = s.split("-", 1)
            lower = float(parts[0])
            upper = float(parts[1])
            return lower, upper
    except Exception:
        return None
    return None


def _load_best_threads_by_sizebin(csv_path: str = "thread_test_results_per_file.csv"):
    global _BIN_THREAD_MAPPING
    if _BIN_THREAD_MAPPING is not None:
        return _BIN_THREAD_MAPPING

    mapping = []
    try:
        if not os.path.exists(csv_path):
            _BIN_THREAD_MAPPING = []
            return _BIN_THREAD_MAPPING

        by_bin = {}
        with open(csv_path, newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                size_bin = row.get('SizeBin') or row.get('size_bin') or row.get('sizebin')
                is_best = str(row.get('IsBest', '')).strip().upper() == 'TRUE'
                try:
                    thread_val = int(row.get('thread'))
                except Exception:
                    continue
                if not size_bin:
                    continue
                if is_best:
                    by_bin.setdefault(size_bin, {})
                    by_bin[size_bin][thread_val] = by_bin[size_bin].get(thread_val, 0) + 1

        for label, counter in by_bin.items():
            if not counter:
                continue
            best_thread = sorted(counter.items(), key=lambda kv: (-kv[1], kv[0]))[0][0]
            rng = _parse_size_bin(label)
            if rng is None:
                continue
            lower, upper = rng
            mapping.append((lower, upper, int(best_thread), label))

        mapping.sort(key=lambda x: (x[0], x[1]))
    except Exception:
        mapping = []

    _BIN_THREAD_MAPPING = mapping
    return _BIN_THREAD_MAPPING


# thread
def _auto_thread_count(file_size_bytes):
    size_mb = file_size_bytes / (1024 * 1024) if file_size_bytes else 0.0
    bin_map = _load_best_threads_by_sizebin()
    if bin_map:
        for lower, upper, best_thread, _label in bin_map:
            if (size_mb >= lower) and (size_mb < upper or upper == float('inf')):
                return max(1, min(int(best_thread), 8))
    if size_mb <= 5:
        return 1
    if size_mb <= 20:
        return 2
    if size_mb <= 100:
        return 4
    if size_mb <= 300:
        return 6
    return 8


def resolve_thread_count(file_size_bytes, total_block=None):
    if USER_SELECTED_THREAD is not None:
        threads = max(1, USER_SELECTED_THREAD)
    else:
        threads = _auto_thread_count(file_size_bytes)
    if total_block is not None:
        threads = min(threads, max(1, total_block))
    return max(1, min(threads, 8))


def append_record(user_id, record):
    ensure_dir('data')

    user_data_dir = os.path.join("data", str(user_id))
    os.makedirs(user_data_dir, exist_ok=True)
    record_path = os.path.join(user_data_dir, "records.csv")

    headers = [
        "datetime", "user_id", "filename", "size_B",
        "upload_time_s", "throughput_MBps", "avg_delay_ms",
        "packet_loss_%", "md5_match", "status"
    ]

    write_header = not os.path.exists(record_path)
    with open(record_path, 'a', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        if write_header:
            writer.writeheader()
        writer.writerow(record)


def show_history(user_id, num=10):
    csv_path = os.path.join("data", str(user_id), "records.csv")

    if not os.path.exists(csv_path):
        print(f"No history found for user {user_id}.")
        return

    with open(csv_path, newline='', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    if not rows:
        print(f"No records found in {csv_path}.")
        return

    for r in rows:
        r.pop("datetime", None)

    for r in rows:
        r.pop("user_id", None)

    rows = rows[-num:]

    header_map = {
        "filename": "File",
        "size_B": "Size",
        "upload_time_s": "Time",
        "throughput_MBps": "Throughput",
        "avg_delay_ms": "Delay",
        "packet_loss_%": "Loss",
        "md5_match": "MD5",
        "status": "Status"
    }

    short_rows = []
    for r in rows:
        short_row = {header_map.get(k, k): v for k, v in r.items() if k in header_map}
        short_rows.append(short_row)

    print(f"\nRecent {len(rows)} upload records for user {user_id}:\n")
    if short_rows:
        headers = list(short_rows[0].keys())
        rows_ = [[r.get(h, "") for h in headers] for r in short_rows]
        print_table(headers, rows_)
    else:
        print("(no records)")


# display the file structure of the folder
def show_folder_status(folder='upload_folder'):
    if not os.path.exists(folder):
        print(f"Folder '{folder}' not found.")
        return

    file_list = []
    for dirpath, _, filenames in os.walk(folder):
        for f in filenames:
            full_path = os.path.join(dirpath, f)
            rel_path = os.path.relpath(full_path, folder)
            size_mb = os.path.getsize(full_path) / 1024 / 1024
            mtime = datetime.fromtimestamp(os.path.getmtime(full_path)).strftime("%Y-%m-%d %H:%M:%S")
            file_list.append([rel_path.replace("\\", "/"), f"{size_mb:.2f} MB", mtime])

    if not file_list:
        print(f"No files found in '{folder}'.")
        return

    print(f"\nFile Structure under '{folder}':")
    print_table(["Relative Path", "Size", "Modified Time"], file_list)


def get_file_md5(filename):
    m = hashlib.md5()
    with open(filename, 'rb') as fid:
        while True:
            d = fid.read(2048)
            if not d:
                break
            m.update(d)
    return m.hexdigest()


def get_sock():
    sock = socket(AF_INET, SOCK_STREAM)
    sock.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    sock.connect((SERVER_IP, SERVER_PORT))
    return sock


# make the message according to the STEP-protocol
def make_message(op_type, op, json_data: json, bin_data=None):
    json_data['operation'] = op
    json_data['type'] = op_type
    if 'direction' not in json_data:
        json_data['direction'] = 'REQUEST'
    # if TOKEN is not None:
    json_data['token'] = TOKEN

    json_dump = json.dumps(json_data)
    json_len = len(json_dump)

    if bin_data is None:
        message = struct.pack('!II', json_len, 0) + json_dump.encode()
    else:
        bin_len = len(bin_data)
        message = struct.pack('!II', json_len, bin_len) + json_dump.encode() + bin_data
    return message


# parse the data from server
def parse_message_from_server(raw_data: bytes):
    # make sure raw message is not empty
    if len(raw_data) > 8:
        json_len, bin_len = struct.unpack('!II', raw_data[:8])
        json_data = json.loads(raw_data[8:8 + json_len].decode())
        bin_data = raw_data[8 + json_len:]
        return json_data, bin_data

    print(f'Warning: received message length is {len(raw_data)}, message is not complete!')
    return None, None


# user login
def login(username: str):
    # make the login packet
    password = hashlib.md5(username.encode()).hexdigest()
    login_json = {
        'username': username,
        'password': password
    }
    message = make_message(TYPE_AUTH, OP_LOGIN, login_json)

    # send packet
    sock = get_sock()
    sock.send(message)
    recv_data = sock.recv(MAX_PACKET_SIZE)
    sock.close()

    # parse received packet
    json_data, _ = parse_message_from_server(recv_data)

    global TOKEN
    try:
        TOKEN = json_data['token']
        print('Login successful!')
        print(f'Token: {TOKEN}')
        if TOKEN is None:
            raise Exception
    except:
        print('Login failed!')
        print('Token Not Found!')


def save_file(file_path, key_name, force_flag=False, include_exts=None):
    if TOKEN is None:
        login(ID)

    print(f"\n [PROCESS] Starting file processing: {os.path.basename(file_path)}")
    print(f" [PROCESS] Step 1: Compression phase...")
    used_path, original_bytes, used_bytes, actually_compressed = compress_file_lz78_huffman(
        file_path,
        benefit_threshold=0.05,
        force=force_flag,
        include_exts=include_exts
    )
    final_path = used_path
    final_size = used_bytes
    is_encrypted = False
    encryption_success = False

    print(f" [PROCESS] Step 2: Encryption phase...")
    # judge if the file is compressed
    if actually_compressed:
        print(f" [PROCESS] Encrypting compressed file...")
        print()
        encrypted_path, encrypted_size, encryption_success = encrypt_compressed_file(used_path)
    else:
        print(f" [PROCESS] Encrypting original file...")
        print()
        encrypted_path, encrypted_size, encryption_success = encrypt_original_file(file_path)

    # judge if the file is encrypted
    if encryption_success:
        final_path = encrypted_path
        final_size = encrypted_size
        is_encrypted = True

        if actually_compressed:
            try:
                os.remove(used_path)
                print(f" [CLEAN] Temporary compression file removed: {os.path.basename(used_path)}")
            except Exception as e:
                print(f" [WARNING] Failed to remove temp file: {e}")
    else:
        print(f" [PROCESS] Encryption failed, using original file")
        is_encrypted = False

    print(f" [PROCESS] Final file ready for upload: {os.path.basename(final_path)}")
    print(f"   Original size: {original_bytes} bytes")
    print(f"   Final size: {final_size} bytes")
    print(f"   Compressed: {'Yes' if actually_compressed else 'No'}")
    print(f"   Encrypted: {'Yes' if is_encrypted else 'No'}")
    print()

    file_size = final_size

    sock = get_sock()

    msg = make_message(TYPE_FILE, OP_SAVE, {'size': file_size, 'key': key_name})
    sock.send(msg)
    recv = sock.recv(MAX_PACKET_SIZE)
    plan, _ = parse_message_from_server(recv)

    if plan['status'] == 200:
        print(f"Upload plan: {plan['total_block']} blocks, key: '{plan['key']}'")

    # ask whether to replace the same file
    elif plan['status'] == 402 and 'existing' in plan.get('status_msg', '').lower():
        if FORCE:
            print(f"[FORCE] File '{key_name}' exists on server. Deleting remote copy and re-creating upload plan.")

            del_msg = make_message(TYPE_FILE, OP_DELETE, {'key': key_name})
            sock.send(del_msg)
            del_recv = sock.recv(MAX_PACKET_SIZE)
            del_resp, _ = parse_message_from_server(del_recv)

            if del_resp is None:
                print(f"Failed to parse server response for DELETE of {key_name}. Aborting.")
                sock.close()
                return

            if del_resp.get('status') not in (200, 404):
                print(f"Failed to delete remote file {key_name}: {del_resp.get('status_msg', '')}")
                sock.close()
                return

            msg = make_message(TYPE_FILE, OP_SAVE, {'size': file_size, 'key': key_name})
            sock.send(msg)
            recv = sock.recv(MAX_PACKET_SIZE)
            plan, _ = parse_message_from_server(recv)

            if plan is None or plan.get('status') != 200:
                print(
                    f"Failed to get upload plan for {key_name} after delete: {plan.get('status_msg', '') if plan else 'no plan'}")
                sock.close()
                return

            print(f"Upload plan: {plan['total_block']} blocks, key: '{plan['key']}'")

        else:
            # ask the user
            while True:
                try:
                    choice = input(f"File '{key_name}' already exists on server. Overwrite? [y/n]: ").strip().lower()
                except Exception:
                    choice = ''
                if choice in ('y', 'n'):
                    break
                print("Invalid input. Please enter 'y' or 'n'.")
                print()

            if choice == 'n':
                print(f"File '{key_name}' skipped.")
                print()
                sock.close()
                return

            # users agree to replace
            del_msg = make_message(TYPE_FILE, OP_DELETE, {'key': key_name})
            sock.send(del_msg)
            del_recv = sock.recv(MAX_PACKET_SIZE)
            del_resp, _ = parse_message_from_server(del_recv)

            if del_resp is None:
                print(f"Failed to parse server response for DELETE of {key_name}. Aborting.")
                sock.close()
                return

            if del_resp.get('status') not in (200, 404):
                print(f"Failed to delete remote file {key_name}: {del_resp.get('status_msg', '')}")
                sock.close()
                return

            msg = make_message(TYPE_FILE, OP_SAVE, {'size': file_size, 'key': key_name})
            sock.send(msg)
            recv = sock.recv(MAX_PACKET_SIZE)
            plan, _ = parse_message_from_server(recv)

            if plan is None or plan.get('status') != 200:
                print(
                    f"Failed to get upload plan for {key_name} after delete: {plan.get('status_msg', '') if plan else 'no plan'}")
                sock.close()
                return

            print(f"Upload plan: {plan['total_block']} blocks, key: '{plan['key']}'")

    else:
        print(f"Failed to get upload plan for {key_name}: {plan.get('status_msg', '')}")
        sock.close()
        return

    # local state
    start_time = time.time()
    total_block = plan['total_block']
    upload_result = [None] * total_block
    block_counter = 0
    threads = []

    # threads
    block_counter = [0]
    threads = []

    # Decide threads
    thread_count = resolve_thread_count(file_size, total_block)
    info_suffix = ""
    if USER_SELECTED_THREAD is None:
        desired = _auto_thread_count(file_size)
        if thread_count < desired:
            info_suffix = f" (auto, limited by {total_block} blocks)"
        else:
            info_suffix = " (auto)"
    else:
        requested = max(1, USER_SELECTED_THREAD)
        if thread_count < min(requested, 8):
            info_suffix = f" (requested {requested}, capped)"
    print(f"Using Thread: {thread_count}{info_suffix}")

    if thread_count <= 1:
        upload_single(final_path, sock, plan, upload_result, start_time, total_block)
    else:
        for _ in range(thread_count):
            s = get_sock()
            t = threading.Thread(
                target=upload_multi,
                args=(final_path, plan['key'], s,
                      upload_result, total_block, start_time,
                      block_counter)
            )
            threads.append(t)
            t.start()
        for t in threads:
            t.join()

    end_time = time.time()
    duration = end_time - start_time
    throughput = (file_size / 1024 / 1024) / duration if duration > 0 else 0

    avg_delay = duration / total_block * 1000 if total_block else 0

    success_blocks = sum(1 for r in upload_result
                         if r and isinstance(r, dict) and r.get('status') == 200)
    packet_loss = (1 - success_blocks / total_block) * 100 if total_block else 0

    md5_match = 'No'
    for r in upload_result:
        if r and 'md5' in r:
            local_md5 = get_file_md5(file_path)
            md5_match = 'Yes' if local_md5 == r['md5'] else 'No'
            break

    print(f"Upload complete: {key_name}")
    print(f"  Throughput: {throughput:.2f} MB/s")
    print(f"  Avg Delay: {avg_delay:.2f} ms/block")
    print(f"  Packet Loss Rate: {packet_loss:.2f}%")
    print(f"  MD5 Match: {md5_match}")
    print()

    sock.close()

    # clean the temporary file
    try:
        if is_encrypted and final_path.endswith(".enc") and os.path.exists(final_path):
            os.remove(final_path)
            print(f" [CLEAN] Temporary encrypted file removed: {os.path.basename(final_path)}")
        elif actually_compressed and final_path.endswith(".cmp") and os.path.exists(final_path):
            os.remove(final_path)
            print(f" [CLEAN] Temporary compression file removed: {os.path.basename(final_path)}")
    except Exception as e:
        print(f" [WARNING] Failed to clean up temporary file {final_path}: {e}")

    return {
        "Filename": key_name,
        "Size(MB)": f"{original_bytes / 1024 / 1024:.2f}",
        "Transmitted(MB)": f"{final_size / 1024 / 1024:.2f}",
        "Compressed?": "Yes" if actually_compressed else "No",
        "Encrypted?": "Yes" if is_encrypted else "No",
        "EncryptionSuccess": "Yes" if encryption_success else "No",
        "Time(s)": f"{duration:.2f}",
        "Throughput": f"{throughput:.2f}",
        "Delay(ms)": f"{avg_delay:.2f}",
        "Loss(%)": f"{packet_loss:.2f}",
        "MD5": md5_match
    }


def upload_single(file_path, sock, plan, upload_result, start_time, total_block):
    key = plan['key']
    with open(file_path, 'rb') as f:
        for i in range(total_block):
            bin_data = f.read(MAX_PACKET_SIZE)

            if i == total_block - 1:
                expected = os.path.getsize(file_path) - i * MAX_PACKET_SIZE
                bin_data = bin_data[:expected]

            msg = make_message(TYPE_FILE, OP_UPLOAD,
                               {'key': key, 'block_index': i}, bin_data)
            sock.send(msg)
            recv = sock.recv(MAX_PACKET_SIZE * 2)
            result, _ = parse_message_from_server(recv)
            upload_result[i] = result if result else {'status': 500}
            update_progress_bar(i, total_block, start_time)
    print()


def update_progress_bar(curr_block, total_block, start_time):
    bar = ''
    uploaded = int(curr_block / total_block * 10) + 1
    for _ in range(uploaded * 2):
        bar += '='
    for _ in range(20 - uploaded * 2):
        bar += '-'
    speed = (curr_block + 1) * MAX_PACKET_SIZE / (time.time() - start_time) / 1024 / 1024
    eta = (total_block - (curr_block + 1)) * (time.time() - start_time) / (curr_block + 1) \
        if curr_block + 1 != 0 else 0
    print(f'\rUploading block: {curr_block}/{total_block} [{bar}]'
          f'{(curr_block + 1) / total_block * 100:.1f}% '
          f'Avg.Speed: {speed:.1f}MB/s ETA: {eta:.2f}s',
          end='', flush=True)


# change the original path
def upload_multi(file_path, key_name, sock,
                 upload_result, total_block, start_time,
                 block_counter_ref):
    counter = block_counter_ref

    while True:
        # preempt block
        with LOCK:
            if counter[0] >= total_block:
                break
            idx = counter[0]
            counter[0] += 1

        try:
            with open(file_path, 'rb') as f:
                f.seek(idx * MAX_PACKET_SIZE)
                bin_data = f.read(MAX_PACKET_SIZE)
                if idx == total_block - 1:
                    expected = os.path.getsize(file_path) - idx * MAX_PACKET_SIZE
                    bin_data = bin_data[:expected]

            msg = make_message(TYPE_FILE, OP_UPLOAD,
                               {'key': key_name, 'block_index': idx}, bin_data)
            sock.send(msg)
            recv = sock.recv(MAX_PACKET_SIZE * 2)
            result, _ = parse_message_from_server(recv)

            upload_result[idx] = result if result else {'status': 500}
            update_progress_bar(idx, total_block, start_time)
        except Exception as e:
            upload_result[idx] = {'status': 500, 'status_msg': str(e)}
    sock.close()

def get_all_files(root_dir='upload_folder'):
    file_list = []
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for f in filenames:
            full_path = os.path.join(dirpath, f)
            rel_path = os.path.relpath(full_path, root_dir)
            file_list.append((full_path, rel_path.replace('\\', '/')))
    return file_list


def _argparse():
    parser = argparse.ArgumentParser(description='STEP Client: Upload files or manage local records')

    # Match past/client.py primary args order and semantics
    parser.add_argument('--server_ip', dest='ip', help='server ip (required only for upload)')
    parser.add_argument('--id', required=True, help='username / user ID')
    parser.add_argument('--f', dest='file', help='Single file to upload (optional)')
    parser.add_argument('--thread', type=int, help='number of upload threads (default: auto by file size)')
    parser.add_argument('--force', action='store_true', help='Force overwrite existing files on server (optional)')
    parser.add_argument('--folder', default='upload_folder', help='Folder to upload (default: upload_folder/)')

    # Keep extended features from current client
    parser.add_argument('--force_compress', action='store_true',
                        help='Force compression even for jpg/pdf/docx files.')
    parser.add_argument('--include_exts', type=str, default='',
                        help='Comma-separated file extensions to include (.jpg,.pdf,.docx)')

    # Local utilities
    parser.add_argument('--history', action='store_true', help='Show recent upload history for this user')
    parser.add_argument('--num', type=int, default=10, help='Number of recent records to show (default: 10)')
    parser.add_argument('--inspect', action='store_true', help='Inspect folder structure')

    # Download helper preserved
    parser.add_argument('--download_key',
                        help='Download a file by its key from server into ./downloads, then decompress it for report')
    return parser.parse_args()


def main():
    global SERVER_IP, ID, MAX_THREAD, FORCE, USER_SELECTED_THREAD
    args = _argparse()
    ID = args.id
    USER_SELECTED_THREAD = args.thread

    # local operations
    if args.history:
        show_history(ID, args.num)
        return

    if args.inspect:
        show_folder_status(args.folder)
        return

    if args.download_key:
        if not args.ip:
            print("Error: --server_ip is required when downloading files.")
            return

        # Set the server IP and obtain the token
        SERVER_IP = args.ip
        login(ID)

        key = args.download_key
        os.makedirs("downloads", exist_ok=True)

        sock = get_sock()
        plan_req = make_message(TYPE_FILE, OP_GET, {'key': key})
        sock.send(plan_req)
        plan_raw = sock.recv(MAX_PACKET_SIZE * 2)
        plan, _ = parse_message_from_server(plan_raw)
        sock.close()

        if not plan or plan.get('status') != 200:
            print(f"Failed to get download plan for {key}: {plan.get('status_msg', '') if plan else 'no plan'}")
            return

        total_block = plan['total_block']
        block_size = plan['block_size']
        total_size = plan['size']

        save_path = os.path.join("downloads", key.replace('/', '_') + ".cmp")
        with open(save_path, 'wb') as f:
            f.truncate(total_size)

        for i in range(total_block):
            sock = get_sock()
            req = make_message(TYPE_FILE, OP_DOWNLOAD, {'key': key, 'block_index': i})
            sock.send(req)
            data_raw = sock.recv(MAX_PACKET_SIZE + 1024)
            resp, bin_data = parse_message_from_server(data_raw)
            sock.close()

            if not resp or resp.get('status') != 200:
                print(f"Download block {i} failed: {resp.get('status_msg', '') if resp else 'no resp'}")
                return

            with open(save_path, 'rb+') as f:
                f.seek(i * block_size)
                f.write(bin_data)

        print(f"Downloaded compressed file to: {save_path}")

        try:
            out_path = save_path + ".restored"
            decompress_file_lz78_huffman(save_path, out_path)
        except Exception as e:
            sz = os.path.getsize(save_path)
            print("\n[Decompression Report]")
            print(f"Received file: {os.path.basename(save_path)}")
            print(f"Received size: {sz / 1024:.2f} KB")
            print("[Notice]: Server stores *already-decompressed* original file. Skipping client-side decompression.\n")
        return

    if not args.ip:
        print("Error: --server_ip is required when uploading files.")
        return

    SERVER_IP = args.ip
    # Adopt adaptive-thread semantics and ensure FORCE/folder are set
    if args.thread is None:
        USER_SELECTED_THREAD = None
    else:
        USER_SELECTED_THREAD = max(1, args.thread)
    FORCE = args.force
    upload_dir = args.folder
    MAX_THREAD = USER_SELECTED_THREAD or 1
    FORCE = args.force
    upload_dir = args.folder

    login(ID)

    # If a single file is specified, upload only that file
    if hasattr(args, 'file') and args.file:
        full_path = args.file
        if not os.path.isfile(full_path):
            print(f"Error: file not found: {full_path}")
            return
        try:
            rel_path = os.path.relpath(os.path.abspath(full_path), os.path.abspath(upload_dir))
            if rel_path.startswith('..'):
                rel_path = os.path.basename(full_path)
            rel_path = rel_path.replace('\\', '/')
        except Exception:
            rel_path = os.path.basename(full_path)

        print(f"Uploading: {rel_path} ({os.path.getsize(full_path)} bytes)")
        include_exts = {e.strip().lower() for e in args.include_exts.split(',') if e.strip()} or None
        result = save_file(full_path, rel_path, force_flag=args.force_compress, include_exts=include_exts)

        if result:
            record = {
                "datetime": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "user_id": ID,
                "filename": rel_path,
                "size_B": os.path.getsize(full_path),
                "upload_time_s": float(result["Time(s)"]),
                "throughput_MBps": float(result["Throughput"]),
                "avg_delay_ms": float(result["Delay(ms)"]),
                "packet_loss_%": float(result["Loss(%)"]),
                "md5_match": result["MD5"],
                "status": "Success" if result["MD5"] == "Yes" else "Failed"
            }
            append_record(ID, record)

            headers = ["Filename", "Size", "Time", "Throughput", "Delay", "Loss", "MD5", "Status"]
            table_data = [[
                record["filename"],
                f'{record["size_B"] / 1024 / 1024:.2f} MB',
                f'{record["upload_time_s"]:.2f} s',
                f'{record["throughput_MBps"]:.2f}',
                f'{record["avg_delay_ms"]:.2f}',
                f'{record["packet_loss_%"]:.2f}%', 'Yes' if result['MD5'] == 'Yes' else 'No',
                record["status"]
            ]]
            print("\nUpload Summary Table:")
            print_table(headers, table_data)
        return

    if not os.path.exists(upload_dir):
        print(f"Error: '{upload_dir}' directory not found!")
        return

    files_to_upload = get_all_files(upload_dir)
    if not files_to_upload:
        print(f"No files found in '{upload_dir}'")
        return

    print(f"Found {len(files_to_upload)} files to upload from '{upload_dir}'")

    table_data = []

    for full_path, rel_path in files_to_upload:
        print(f"Uploading: {rel_path} ({os.path.getsize(full_path)} bytes)")
        include_exts = {e.strip().lower() for e in args.include_exts.split(',') if e.strip()} or None
        result = save_file(full_path, rel_path, force_flag=args.force_compress, include_exts=include_exts)

        if result:
            record = {
                "datetime": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "user_id": ID,
                "filename": rel_path,
                "size_B": os.path.getsize(full_path),
                "upload_time_s": float(result["Time(s)"]),
                "throughput_MBps": float(result["Throughput"]),
                "avg_delay_ms": float(result["Delay(ms)"]),
                "packet_loss_%": float(result["Loss(%)"]),
                "md5_match": result["MD5"],
                "status": "Success" if result["MD5"] == "Yes" else "Failed"
            }
            append_record(ID, record)
            table_data.append([
                record["filename"],
                f'{record["size_B"] / 1024 / 1024:.2f} MB',
                result["Compressed?"],
                result["Encrypted?"],
                result["EncryptionSuccess"],
                f'{record["upload_time_s"]:.2f} s',
                f'{record["throughput_MBps"]:.2f}',
                f'{record["avg_delay_ms"]:.2f}',
                f'{record["packet_loss_%"]:.2f}%',
                record["md5_match"],
                record["status"]
            ])

    if table_data:
        headers = [
            "Filename", "Size", "Compressed?", "Encrypted?", "EncryptSuccess",
            "Time", "Throughput", "Delay", "Loss", "MD5", "Status"
        ]
        print_table(headers, table_data)

if __name__ == '__main__':
    main()
