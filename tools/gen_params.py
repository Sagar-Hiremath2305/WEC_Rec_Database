import hashlib
import struct

def generate_params(roll_number):
    h = hashlib.sha256(roll_number.encode('utf-8')).digest()
    
    # HASH_SEED: bytes 8-15 as a 64-bit little-endian integer
    hash_seed = struct.unpack('<Q', h[8:16])[0]
    
    # PROBE_CAP: 8 + (byte[16] % 9)
    probe_cap = 8 + (h[16] % 9)
    
    # EVICT_SAMPLE: 3 + (byte[17] % 6)
    evict_sample = 3 + (h[17] % 6)
    
    # MAGIC: "RDB" followed by the first 5 hex characters of the hash
    magic = f"RDB{h.hex()[:5]}"
    
    with open('params.txt', 'w') as f:
        f.write(f"HASH_SEED={hash_seed}\n")
        f.write(f"PROBE_CAP={probe_cap}\n")
        f.write(f"EVICT_SAMPLE={evict_sample}\n")
        f.write(f"MAGIC={magic}\n")
    
    print(f"Generated params.txt for roll number: {roll_number}")
    print(f"HASH_SEED: {hash_seed}")
    print(f"PROBE_CAP: {probe_cap}")
    print(f"EVICT_SAMPLE: {evict_sample}")
    print(f"MAGIC: {magic}")

if __name__ == "__main__":
    generate_params("12345678")
