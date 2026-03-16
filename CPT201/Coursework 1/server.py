from socket import *
import json
import os
import zlib
from os.path import join, getsize
import hashlib
import argparse
from threading import Thread
import time
import logging
from logging.handlers import TimedRotatingFileHandler
import base64
import uuid
import math
import shutil
import struct
import threading
import hashlib
from Crypto.Cipher import AES
from Crypto.Random import get_random_bytes

MAX_PACKET_SIZE = 20480

# Const Value
OP_SAVE, OP_DELETE, OP_GET, OP_UPLOAD, OP_DOWNLOAD, OP_BYE, OP_LOGIN, OP_ERROR = 'SAVE', 'DELETE', 'GET', 'UPLOAD', 'DOWNLOAD', 'BYE', 'LOGIN', "ERROR"
TYPE_FILE, TYPE_DATA, TYPE_AUTH, DIR_EARTH = 'FILE', 'DATA', 'AUTH', 'EARTH'
FIELD_OPERATION, FIELD_DIRECTION, FIELD_TYPE, FIELD_USERNAME, FIELD_PASSWORD, FIELD_TOKEN = 'operation', 'direction', 'type', 'username', 'password', 'token'
FIELD_KEY, FIELD_SIZE, FIELD_TOTAL_BLOCK, FIELD_MD5, FIELD_BLOCK_SIZE = 'key', 'size', 'total_block', 'md5', 'block_size'
FIELD_STATUS, FIELD_STATUS_MSG, FIELD_BLOCK_INDEX = 'status', 'status_msg', 'block_index'
DIR_REQUEST, DIR_RESPONSE = 'REQUEST', 'RESPONSE'

logger = logging.getLogger('')

FILE_LOG_LOCKS = {}
FILE_LOG_LOCKS_LOCK = threading.Lock()

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
            
            logger.info(f"[ENCRYPT] Success: {input_path} -> {output_path}")
            return True
            
        except Exception as e:
            logger.error(f"[ENCRYPT] Failed: {input_path} -> {e}")
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
            
            logger.info(f"[DECRYPT] Success: {input_path} -> {output_path}")
            return True
            
        except Exception as e:
            logger.error(f"[DECRYPT] Failed: {input_path} -> {e}")
            return False

encryptor = AESEncryptor()

# decrypt
def decrypt_received_file(encrypted_path: str) -> tuple:

    try:
        logger.info(f"[DECRYPT] Step 1: Starting decryption of {encrypted_path}")
        file_size = os.path.getsize(encrypted_path)
        logger.info(f"[DECRYPT] Step 2: Encrypted file size: {file_size} bytes")

        with open(encrypted_path, 'rb') as f:
            nonce = f.read(16)
            tag = f.read(16)
            ciphertext = f.read()
        
        logger.info(f"[DECRYPT] Step 3: Decrypting data...")
        cipher = AES.new(encryptor.key, AES.MODE_GCM, nonce=nonce)
        decrypted_data = cipher.decrypt_and_verify(ciphertext, tag)
        
        logger.info(f"[DECRYPT] SUCCESS Decryption successful!")
        logger.info(f"[DECRYPT]   Encrypted size: {file_size} bytes")
        logger.info(f"[DECRYPT]   Decrypted size: {len(decrypted_data)} bytes")
        return decrypted_data, True
        
    except Exception as e:
        logger.error(f"[DECRYPT] FAILED Decryption failed: {e}")
        return None, False
    
# Encapsulation lock
def get_file_lock(username, key):
    lock_key = (username, key)
    with FILE_LOG_LOCKS_LOCK:
        if lock_key not in FILE_LOG_LOCKS:
            FILE_LOG_LOCKS[lock_key] = threading.Lock()
        return FILE_LOG_LOCKS[lock_key]

def get_file_md5(filename):
    m = hashlib.md5()
    with open(filename, 'rb') as fid:
        while True:
            d = fid.read(2048)
            if not d:
                break
            m.update(d)
    return m.hexdigest()

def decompress_file_lz78_huffman(input_path: str, output_path: str) -> bool:
    try:
        with open(input_path, 'rb') as f:
            b85 = f.read()
        lz_bytes = base64.b85decode(b85)
        raw = zlib.decompress(lz_bytes)
        with open(output_path, 'wb') as f:
            f.write(raw)
        return True
    except Exception as e:
        logger.error(f"[DECOMPRESS] Failed to decompress {input_path}: {e}")
        return False

def get_time_based_filename(ext, prefix='', t=None):
    ext = ext.replace('.', '')
    if t is None:
        t = time.time()
    if t > 4102464500:
        t = t / 1000
    return time.strftime(f"{prefix}%Y%m%d%H%M%S." + ext, time.localtime(t))

def set_logger(logger_name):
    logger_ = logging.getLogger(logger_name)
    logger_.setLevel(logging.INFO)

    formatter = logging.Formatter(
        '\033[0;34m%s\033[0m' % '%(asctime)s-%(name)s[%(levelname)s] %(message)s @ %(filename)s[%(lineno)d]',
        datefmt='%Y-%m-%d %H:%M:%S')

    # LOG FILE
    logger_file_name = get_time_based_filename('log')
    os.makedirs(f'log/{logger_name}', exist_ok=True)

    fh = TimedRotatingFileHandler(filename=f'log/{logger_name}/log', when='D', interval=1, backupCount=1)
    fh.setFormatter(formatter)

    fh.setLevel(logging.INFO)

    # SCREEN DISPLAY
    ch = logging.StreamHandler()
    ch.setLevel(logging.INFO)
    ch.setFormatter(formatter)

    logger_.propagate = False
    logger_.addHandler(ch)
    logger_.addHandler(fh)
    return logger_

def _argparse():
    parse = argparse.ArgumentParser()
    parse.add_argument("--ip", default='', action='store', required=False, dest="ip",
                       help="The IP address bind to the server. Default bind all IP.")
    parse.add_argument("--port", default='1379', action='store', required=False, dest="port",
                       help="The port that server listen on. Default is 1379.")
    return parse.parse_args()

def make_packet(json_data, bin_data=None):
    j = json.dumps(dict(json_data), ensure_ascii=False)
    j_len = len(j)
    if bin_data is None:
        return struct.pack('!II', j_len, 0) + j.encode()
    else:
        return struct.pack('!II', j_len, len(bin_data)) + j.encode() + bin_data

def make_response_packet(operation, status_code, data_type, status_msg, json_data, bin_data=None):
    json_data[FIELD_OPERATION] = operation
    json_data[FIELD_DIRECTION] = DIR_RESPONSE
    json_data[FIELD_STATUS] = status_code
    json_data[FIELD_STATUS_MSG] = status_msg
    json_data[FIELD_TYPE] = data_type
    return make_packet(json_data, bin_data)

def get_tcp_packet(conn):
    bin_data = b''
    while len(bin_data) < 16:
        data_rec = conn.recv(8)
        if data_rec == b'':
            time.sleep(0.01)
        if data_rec == b'':
            return None, None
        bin_data += data_rec
    data = bin_data[:8]
    bin_data = bin_data[8:]
    j_len, b_len = struct.unpack('!II', data)
    while len(bin_data) < j_len:
        data_rec = conn.recv(j_len)
        if data_rec == b'':
            time.sleep(0.01)
        if data_rec == b'':
            return None, None
        bin_data += data_rec
    j_bin = bin_data[:j_len]

    try:
        json_data = json.loads(j_bin.decode())
    except Exception as ex:
        return None, None

    bin_data = bin_data[j_len:]
    while len(bin_data) < b_len:
        data_rec = conn.recv(b_len)
        if data_rec == b'':
            time.sleep(0.01)
        if data_rec == b'':
            return None, None
        bin_data += data_rec
    return json_data, bin_data

def data_process(username, request_operation, json_data, connection_socket):
    global logger
    if request_operation == OP_GET:
        if FIELD_KEY not in json_data.keys():
            logger.info(f'<-- Get data without key.')
            logger.error(f'<-- Field "key" is missing for DATA GET.')
            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_DATA, f'Field "key" is missing for DATA GET.', {}))
            return
        logger.info(f'--> Get data {json_data[FIELD_KEY]}')
        if os.path.exists(join('data', username, json_data[FIELD_KEY])) is False:
            logger.error(f'<-- The key {json_data[FIELD_KEY]} is not existing.')
            connection_socket.send(
                make_response_packet(OP_GET, 404, TYPE_DATA, f'The key {json_data[FIELD_KEY]} is not existing.', {}))
            return
        try:
            with open(join('data', username, json_data[FIELD_KEY]), 'r') as fid:
                data_from_file = json.load(fid)
                logger.info(f'<-- Find the data and return to client.')
                connection_socket.send(
                    make_response_packet(OP_GET, 200, TYPE_DATA, f'OK', data_from_file))
        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

    if request_operation == OP_SAVE:
        key = str(uuid.uuid4())
        if FIELD_KEY in json_data.keys():
            key = json_data[FIELD_KEY]
        logger.info(f'--> Save data with key "{key}"')
        if os.path.exists(join('data', username, key)) is True:
            logger.error(f'<-- This key "{key}" is existing.')
            connection_socket.send(make_response_packet(OP_SAVE, 402, TYPE_DATA, f'This key "{key}" is existing.', {}))
            return
        try:
            with open(join('data', username, key), 'w') as fid:
                json.dump(json_data, fid)
                logger.info(f'<-- Data is saved with key "{key}"')
                connection_socket.send(
                    make_response_packet(OP_SAVE, 200, TYPE_DATA, f'Data is saved with key "{key}"', {FIELD_KEY: key}))
        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

    if request_operation == OP_DELETE:
        if FIELD_KEY not in json_data.keys():
            logger.info(f'--> Delete data without any key.')
            logger.error(f'<-- Field "key" is missing for DATA delete.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 410, TYPE_DATA, f'Field "key" is missing for DATA delete.', {}))
            return
        if os.path.exists(join('data', username, json_data[FIELD_KEY])) is False:
            logger.error(f'<-- The "key" {json_data[FIELD_KEY]} is not existing.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 404, TYPE_DATA, f'The "key" {json_data[FIELD_KEY]} is not existing.',
                                     {}))
            return
        try:
            os.remove(join('data', username, json_data[FIELD_KEY]))
            logger.error(f'<-- The "key" {json_data[FIELD_KEY]} is deleted.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 200, TYPE_DATA, f'The "key" {json_data[FIELD_KEY]} is deleted.',
                                     {FIELD_KEY: json_data[FIELD_KEY]}))
        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

def file_process(username, request_operation, json_data, bin_data, connection_socket):
    key = json_data.get(FIELD_KEY)
    block_index = json_data.get(FIELD_BLOCK_INDEX)
    tmp_path = os.path.join('tmp', username, key)

    os.makedirs(os.path.dirname(tmp_path), exist_ok=True)
    global logger
    if request_operation == OP_GET:
        if FIELD_KEY not in json_data.keys():
            logger.info(f'--> Plan to download file {json_data[FIELD_KEY]}')

            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_FILE, f'Field "key" is missing for DATA GET.', {}))
            return
        logger.info(f'--> Plan to download file with "key" {json_data[FIELD_KEY]}')
        if os.path.exists(join('file', username, json_data[FIELD_KEY])) is False and os.path.exists(
                join('tmp', username, json_data[FIELD_KEY])) is False:
            logger.error(f'<-- The key {json_data[FIELD_KEY]} is not existing.')
            connection_socket.send(
                make_response_packet(OP_GET, 404, TYPE_FILE, f'The key {json_data[FIELD_KEY]} is not existing.', {}))
            return

        if os.path.exists(join('file', username, json_data[FIELD_KEY])) is False and os.path.exists(
                join('tmp', username, json_data[FIELD_KEY])) is True:
            logger.error(f'<-- The key {json_data[FIELD_KEY]} is not completely uploaded.')
            connection_socket.send(
                make_response_packet(OP_GET, 404, TYPE_FILE,
                                     f'The key {json_data[FIELD_KEY]} is not completely uploaded.', {}))
            return

        file_path = join('file', username, json_data[FIELD_KEY])
        file_size = getsize(file_path)
        block_size = MAX_PACKET_SIZE
        total_block = math.ceil(file_size / block_size)
        md5 = get_file_md5(file_path)
        # Download Plan
        rval = {
            FIELD_KEY: json_data[FIELD_KEY],
            FIELD_SIZE: file_size,
            FIELD_TOTAL_BLOCK: total_block,
            FIELD_BLOCK_SIZE: block_size,
            FIELD_MD5: md5
        }
        logger.info(f'<-- Plan: file size {file_size}, total block number {FIELD_TOTAL_BLOCK}.')
        connection_socket.send(
            make_response_packet(OP_GET, 200, TYPE_FILE, f'OK. This is the download plan.', rval))
        return

    if request_operation == OP_SAVE:
        key = str(uuid.uuid4())
        if FIELD_KEY in json_data.keys():
            key = json_data[FIELD_KEY]
        logger.info(f'--> Plan to save/upload a file with key "{key}"')
        if os.path.exists(join('file', username, key)) is True:
            logger.error(f'<-- This key "{key}" is existing.')
            connection_socket.send(make_response_packet(OP_SAVE, 402, TYPE_FILE, f'This "key" {key} is existing.', {}))
            return
        if FIELD_SIZE not in json_data.keys():
            logger.error(f'<-- This file "size" has to be included.')
            connection_socket.send(
                make_response_packet(OP_SAVE, 402, TYPE_FILE, f'This file "size" has to be included', {}))
            return
        file_size = json_data[FIELD_SIZE]
        block_size = MAX_PACKET_SIZE
        total_block = math.ceil(file_size / block_size)
        try:
            rval = {
                FIELD_KEY: key,
                FIELD_SIZE: file_size,
                FIELD_TOTAL_BLOCK: total_block,
                FIELD_BLOCK_SIZE: block_size,
            }
            # Write a tmp file
            with open(join('tmp', username, key), 'wb+') as fid:
                fid.seek(file_size - 1)
                fid.write(b'\0')

            fid = open(join('tmp', username, key + '.log'), 'w')
            fid.close()

            logger.error(f'<-- Upload plan: key {key}, total block number {total_block}, block size {block_size}.')
            connection_socket.send(
                make_response_packet(OP_SAVE, 200, TYPE_FILE, f'This is the upload plan.', rval))
        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

    if request_operation == OP_DELETE:
        if FIELD_KEY not in json_data.keys():
            logger.info(f'--> Delete file without any key.')
            logger.error(f'<-- Field "key" is missing for FILE delete.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 410, TYPE_FILE, f'Field "key" is missing for FILE delete.', {}))
            return

        if os.path.exists(join('file', username, json_data[FIELD_KEY])) is False:
            if os.path.exists(join('tmp', username, json_data[FIELD_KEY])) is True:
                try:
                    os.remove(join('tmp', username, json_data[FIELD_KEY]))
                    os.remove(join('tmp', username, json_data[FIELD_KEY]) + '.log')
                except Exception as ex:
                    logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')
                logger.error(
                    f'<-- The "key" {json_data[FIELD_KEY]} is not completely uploaded. The tmp files are deleted.')
                connection_socket.send(
                    make_response_packet(OP_DELETE, 404, TYPE_FILE,
                                         f'The "key" {json_data[FIELD_KEY]} is not completely uploaded. '
                                         f'The tmp files are deleted.',
                                         {}))
                return
            logger.error(f'<-- The "key" {json_data[FIELD_KEY]} is not existing.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 404, TYPE_FILE, f'The "key" {json_data[FIELD_KEY]} is not existing.', {}))
            return
        try:
            os.remove(join('file', username, json_data[FIELD_KEY]))
            logger.error(f'<-- The "key" {json_data[FIELD_KEY]} is deleted.')
            connection_socket.send(
                make_response_packet(OP_DELETE, 200, TYPE_FILE, f'The "key" {json_data[FIELD_KEY]} is deleted.',
                                     {FIELD_KEY: json_data[FIELD_KEY]}))
        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

    if request_operation == OP_UPLOAD:
        key = json_data.get(FIELD_KEY)
        if not key:
            connection_socket.send(make_response_packet(OP_UPLOAD, 410, TYPE_FILE, 'Missing key.', {}))
            return

        file_path = join('tmp', username, key)
        log_path = file_path + '.log'

        if os.path.exists(join('file', username, key)):
            connection_socket.send(make_response_packet(OP_UPLOAD, 408, TYPE_FILE, 'File already complete.', {}))
            return

        if not os.path.exists(file_path):
            connection_socket.send(make_response_packet(OP_UPLOAD, 408, TYPE_FILE, 'Upload not started.', {}))
            return

        block_index = json_data.get(FIELD_BLOCK_INDEX)
        if block_index is None:
            connection_socket.send(make_response_packet(OP_UPLOAD, 410, TYPE_FILE, 'Missing block_index.', {}))
            return

        file_size = getsize(file_path)
        total_block = math.ceil(file_size / MAX_PACKET_SIZE)
        if block_index >= total_block or block_index < 0:
            connection_socket.send(make_response_packet(OP_UPLOAD, 405, TYPE_FILE, 'Invalid block_index.', {}))
            return

        try:
            with open(file_path, 'rb+') as f:
                f.seek(MAX_PACKET_SIZE * block_index)
                f.write(bin_data)
        except Exception as e:
            logger.error(f'Write failed: {e}')
            connection_socket.send(make_response_packet(OP_UPLOAD, 500, TYPE_FILE, 'Write failed.', {}))
            return

        file_lock = get_file_lock(username, key)
        with file_lock:
            received = set()
            if os.path.exists(log_path):
                with open(log_path, 'r') as f:
                    for line in f:
                        line = line.strip()
                        if line.isdigit():
                            received.add(int(line))

            if block_index not in received:
                with open(log_path, 'a') as f:
                    f.write(f'{block_index}\n')
                    f.flush()
                received.add(block_index)

            is_complete = len(received) == total_block
            rval = {FIELD_KEY: key, FIELD_BLOCK_INDEX: block_index}

            if is_complete:
                try:
                    target_path = join('file', username, key)
                    os.makedirs(os.path.dirname(target_path), exist_ok=True)
                    shutil.move(file_path, target_path)
                    logger.info(f"[UPLOAD] File moved to {target_path}")
                    tmp_out = target_path + ".tmp"
                    
                    logger.info(f"[PROCESS] Starting file processing: {key}")
                    logger.info(f"[PROCESS] Step 1: Auto-detecting file type...")

                    decrypted_data, decrypt_success = decrypt_received_file(target_path)
                    
                    if decrypt_success:
                        logger.info(f"[PROCESS] SUCCESS: Auto-detected: Encrypted file")
                        logger.info(f"[PROCESS] Step 2: Processing encrypted file...")

                        temp_decrypted_path = target_path + ".decrypted"
                        with open(temp_decrypted_path, 'wb') as f:
                            f.write(decrypted_data)
                        logger.info(f"[PROCESS] Step 3: Decrypted data written to temporary file")
                        logger.info(f"[PROCESS] Step 4: Detecting file type after decryption...")

                        def is_likely_compressed_file(file_path: str) -> bool:
                            try:
                                with open(file_path, 'rb') as f:
                                    header = f.read(100)
                                    base85_chars = b'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~'
                                    base85_count = sum(1 for byte in header if byte in base85_chars)
                                    return base85_count > 80
                            except:
                                return False

                        if is_likely_compressed_file(temp_decrypted_path):
                            logger.info(f"[PROCESS] Detected compressed file by content analysis, starting decompression...")
                            if decompress_file_lz78_huffman(temp_decrypted_path, tmp_out):
                                try:
                                    os.replace(tmp_out, target_path)
                                    logger.info(f"[PROCESS] SUCCESS: Encrypted file successfully decrypted and decompressed: {target_path}")
                                except Exception as e:
                                    logger.error(f"[PROCESS] FAILED: Replace failed: {e}")
                                    try:
                                        if os.path.exists(tmp_out):
                                            os.remove(tmp_out)
                                    except:
                                        pass
                            else:
                                logger.error(f"[PROCESS] FAILED: Decompression failed after decryption")
                        else:
                            logger.info(f"[PROCESS] Detected original file by content analysis, moving decrypted file...")
                            try:
                                os.replace(temp_decrypted_path, target_path)
                                logger.info(f"[PROCESS] SUCCESS: Encrypted file successfully decrypted: {target_path}")
                            except Exception as e:
                                logger.error(f"[PROCESS] FAILED: Replace failed: {e}")

                        try:
                            if os.path.exists(temp_decrypted_path):
                                os.remove(temp_decrypted_path)
                                logger.info(f"[CLEAN] Temporary decrypted file removed")
                        except Exception as e:
                            logger.warning(f"[CLEAN] Failed to clean temp file: {e}")

                    md5 = get_file_md5(target_path)
                    rval[FIELD_MD5] = md5
                    logger.info(f"[PROCESS] SUCCESS MD5 calculated: {md5}")

                except Exception as e:
                    logger.error(f'Move/Decompress failed: {e}')
                    is_complete = False
                    

            status_msg = 'File complete.' if is_complete else f'Block {block_index} saved.'
            connection_socket.send(make_response_packet(OP_UPLOAD, 200, TYPE_FILE, status_msg, rval))
        return

    if request_operation == OP_DOWNLOAD:
        if FIELD_KEY not in json_data.keys():
            logger.info(f'--> Download file/block without any key.')
            logger.error(f'<-- Field "key" is missing for FILE block downloading.')
            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_FILE, f'Field "key" is missing for FILE downloading.', {}))
            return
        logger.info(f'--> Download file/block of "key" {json_data[FIELD_KEY]}.')

        if os.path.exists(join('file', username, json_data[FIELD_KEY])) is False:
            if os.path.exists(join('tmp', username, json_data[FIELD_KEY])) is True:
                logger.error(
                    f'<-- The "key" {json_data[FIELD_KEY]} is not completely uploaded. Please upload it first.')
                connection_socket.send(
                    make_response_packet(OP_GET, 404, TYPE_FILE,
                                         f'The "key" {json_data[FIELD_KEY]} is not completely uploaded. '
                                         f'Please upload it first',
                                         {}))
                return
            logger.error(f'<-- The "key" {json_data[FIELD_KEY]} is not existing.')
            connection_socket.send(
                make_response_packet(OP_GET, 404, TYPE_FILE, f'The "key" {json_data[FIELD_KEY]} is not existing.', {}))
            return

        if FIELD_BLOCK_INDEX not in json_data.keys():
            logger.error(f'<-- The "block_index" is compulsory.')
            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_FILE, f'The "block_index" is compulsory.', {}))
            return
        file_path = join('file', username, json_data[FIELD_KEY])
        file_size = getsize(file_path)
        block_size = MAX_PACKET_SIZE
        total_block = math.ceil(file_size / block_size)
        block_index = json_data[FIELD_BLOCK_INDEX]
        if block_index >= total_block:
            logger.error(f'<-- The "block_index" exceed the max index.')
            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_FILE, f'The "block_index" exceed the max index.', {}))
            return
        if block_index < 0:
            logger.error(f'<-- The "block_index" should >= 0.')
            connection_socket.send(
                make_response_packet(OP_GET, 410, TYPE_FILE, f'The "block_index" should >= 0.', {}))
            return

        with open(file_path, 'rb') as fid:
            fid.seek(block_size * block_index)
            if block_size * (block_index + 1) < file_size:
                bin_data = fid.read(block_size)
            else:
                bin_data = fid.read(file_size - block_size * block_index)

            rval = {
                FIELD_BLOCK_INDEX: block_index,
                FIELD_KEY: json_data[FIELD_KEY],
                FIELD_SIZE: len(bin_data)
            }
            logger.info(f'<-- Return block {block_index}({len(bin_data)}bytes) of "key" {json_data[FIELD_KEY]} >= 0.')

            connection_socket.send(make_response_packet(OP_DOWNLOAD, 200, TYPE_FILE,
                                                        'An available block.', rval, bin_data))

def STEP_service(connection_socket, addr):
    global logger
    while True:
        json_data, bin_data = get_tcp_packet(connection_socket)
        json_data: dict
        if json_data is None:
            logger.warning('Connection is closed by client.')
            break

        if FIELD_DIRECTION in json_data:
            if json_data[FIELD_DIRECTION] == DIR_EARTH:
                connection_socket.send(
                    make_response_packet('3BODY', 333, 'DANGEROUS', f'DO NOT ANSWER! DO NOT ANSWER! DO NOT ANSWER!', {}))
                continue

        compulsory_fields = [FIELD_OPERATION, FIELD_DIRECTION, FIELD_TYPE]

        check_ok = True
        for _compulsory_fields in compulsory_fields:
            if _compulsory_fields not in list(json_data.keys()):
                connection_socket.send(
                    make_response_packet(OP_ERROR, 400, 'ERROR', f'Compulsory field {_compulsory_fields} is missing.',
                                         {}))
                check_ok = False
                break
        if check_ok is False:
            continue

        request_type = json_data[FIELD_TYPE]
        request_operation = json_data[FIELD_OPERATION]
        request_direction = json_data[FIELD_DIRECTION]

        if request_direction != DIR_REQUEST:
            connection_socket.send(
                make_response_packet(OP_ERROR, 407, 'ERROR', f'Wrong direction. Should be "REQUEST"', {}))
            continue

        if request_operation not in [OP_SAVE, OP_DELETE, OP_GET, OP_UPLOAD, OP_DOWNLOAD, OP_BYE, OP_LOGIN]:
            connection_socket.send(
                make_response_packet(OP_ERROR, 408, 'ERROR', f'Operation {request_operation} is not allowed', {}))
            continue

        if request_type not in [TYPE_FILE, TYPE_DATA, TYPE_AUTH]:
            connection_socket.send(
                make_response_packet(OP_ERROR, 409, 'ERROR', f'Type {request_type} is not allowed', {}))
            continue

        if request_operation == OP_LOGIN:
            if request_type != TYPE_AUTH:
                connection_socket.send(
                    make_response_packet(OP_LOGIN, 409, TYPE_AUTH, f'Type of LOGIN has to be AUTH.', {}))
                continue
            else:
                if FIELD_USERNAME not in json_data.keys():
                    connection_socket.send(
                        make_response_packet(OP_LOGIN, 410, TYPE_AUTH, f'"username" has to be a field for LOGIN', {}))
                    continue
                if FIELD_PASSWORD not in json_data.keys():
                    connection_socket.send(
                        make_response_packet(OP_LOGIN, 410, TYPE_AUTH, f'"password" has to be a field for LOGIN', {}))
                    continue

                # Check the username and password
                if hashlib.md5(json_data[FIELD_USERNAME].encode()).hexdigest().lower() != json_data['password'].lower():
                    connection_socket.send(
                        make_response_packet(OP_LOGIN, 401, TYPE_AUTH, f'"Password error for login.', {}))
                    continue
                else:
                    # Login successful
                    user_str = f'{json_data[FIELD_USERNAME].replace(".", "_")}.' \
                               f'{get_time_based_filename("login")}'
                    md5_auth_str = hashlib.md5(f'{user_str}kjh20)*(1'.encode()).hexdigest()
                    connection_socket.send(
                        make_response_packet(OP_LOGIN, 200, TYPE_AUTH, f'Login successfully', {
                            FIELD_TOKEN: base64.b64encode(f'{user_str}.{md5_auth_str}'.encode()).decode()
                        }))
                    continue

        if FIELD_TOKEN not in json_data.keys():
            connection_socket.send(
                make_response_packet(request_operation, 403, TYPE_AUTH, f'No token.', {}))
            continue

        token = json_data[FIELD_TOKEN]
        token = base64.b64decode(token).decode()
        token: str

        if len(token.split('.')) != 4:
            connection_socket.send(
                make_response_packet(request_operation, 403, TYPE_AUTH, f'Token format is wrong.', {}))
            continue

        user_str = ".".join(token.split('.')[:3])
        md5_auth_str = token.split('.')[3]
        if hashlib.md5(f'{user_str}kjh20)*(1'.encode()).hexdigest().lower() != md5_auth_str.lower():
            connection_socket.send(
                make_response_packet(request_operation, 403, TYPE_AUTH, f'Token is wrong.', {}))
            continue

        username = token.split('.')[0]

        os.makedirs(join('data', username), exist_ok=True)
        os.makedirs(join('file', username), exist_ok=True)
        os.makedirs(join('tmp', username), exist_ok=True)

        if request_type == TYPE_DATA:
            data_process(username, request_operation, json_data, connection_socket)
            continue

        if request_type == TYPE_FILE:
            file_process(username, request_operation, json_data, bin_data, connection_socket)
            continue

    connection_socket.close()
    logger.info(f'Connection close. {addr}')

def tcp_listener(server_ip, server_port):
    global logger
    server_socket = socket(AF_INET, SOCK_STREAM)
    server_socket.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)
    server_socket.bind((server_ip, int(server_port)))
    server_socket.listen(20)
    logger.info('Server is ready!')
    logger.info(
        f'Start the TCP service, listing {server_port} on IP {"All available" if server_ip == "" else server_ip}')
    while True:
        try:
            connection_socket, addr = server_socket.accept()
            logger.info(f'--> New connection from {addr[0]} on {addr[1]}')
            th = Thread(target=STEP_service, args=(connection_socket, addr))
            th.daemon = True
            th.start()

        except Exception as ex:
            logger.error(f'{str(ex)}@{ex.__traceback__.tb_lineno}')

def main():
    global logger
    logger = set_logger('STEP')
    parser = _argparse()
    server_ip = parser.ip
    server_port = parser.port

    os.makedirs('data', exist_ok=True)
    os.makedirs('file', exist_ok=True)

    tcp_listener(server_ip, server_port)

if __name__ == '__main__':
    main()
