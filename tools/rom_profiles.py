#!/usr/bin/env python3
"""rom_profiles.py - Regional ROM definitions and hook generator for Shadowgreat.

Supports:
- Shadowgate (USA)
- Shadowgate (Europe)
- Shadowgate (Sweden)
- Shadowgate (France)
- Shadowgate (Germany)
"""
import hashlib
import sys
from pathlib import Path

PROFILES = {
    "e7869c683bd93275cde4536cd2ddf37f230dd8d0": {
        "region": "USA",
        "name": "Shadowgate (USA)",
        "msg": 0xC49E,
        "msg_crc": 0x9B4D6BA3,
        "msg_cont": 0xC494,
        "msg_cont_crc": 0x31E0841C,
        "text_a": 0xC7F4,
        "text_a_crc": 0x2448687D,
        "page_a": 0xC9F9,
        "page_a_crc": 0xE15116D7,
        "code2tile": 0xC7AD,
    },
    "0efc8ba8a306c423b32853385274208f42626e2e": {
        "region": "EUROPE",
        "name": "Shadowgate (Europe)",
        "msg": 0xC49E,
        "msg_crc": 0x9B4D6BA3,
        "msg_cont": 0xC494,
        "msg_cont_crc": 0x31E0841C,
        "text_a": 0xC7F4,
        "text_a_crc": 0x2448687D,
        "page_a": 0xC9F9,
        "page_a_crc": 0xE15116D7,
        "code2tile": 0xC7AD,
    },
    "757ab53fbff0adf1c2c2e05a35ab861365021057": {
        "region": "SWEDEN",
        "name": "Shadowgate (Sweden)",
        "msg": 0xC494,
        "msg_crc": 0x04E484DC,
        "msg_cont": 0xC48A,
        "msg_cont_crc": 0x5D712822,
        "text_a": 0xC759,
        "text_a_crc": 0x5F3CF065,
        "page_a": 0xC92C,
        "page_a_crc": 0x571B150D,
        "code2tile": 0xC712,
        "huffman": 0xE173,
    },
    "149eca5642642bed5fd98b12d682235c02def342": {
        "region": "FRANCE",
        "name": "Shadowgate (France)",
        "msg": 0xC494,
        "msg_crc": 0x80AE8A26,
        "msg_cont": 0xC48A,
        "msg_cont_crc": 0x5D712822,
        "text_a": 0xC75D,
        "text_a_crc": 0x631EFDAB,
        "page_a": 0xC930,
        "page_a_crc": 0x1DEFA468,
        "code2tile": 0xC716,
        "huffman": 0xE204,
    },
    "08625db56ba9a2a66e636590c51044c6726c5b26": {
        "region": "GERMANY",
        "name": "Shadowgate (Germany)",
        "msg": 0xC494,
        "msg_crc": 0x80AE8A26,
        "msg_cont": 0xC48A,
        "msg_cont_crc": 0x5D712822,
        "text_a": 0xC75D,
        "text_a_crc": 0x631EFDAB,
        "page_a": 0xC930,
        "page_a_crc": 0x1DEFA468,
        "code2tile": 0xC716,
        "huffman": 0xE1D8,
    },
}


def get_rom_sha1_body(rom_path: Path) -> str:
    data = rom_path.read_bytes()
    if len(data) <= 16:
        return ""
    return hashlib.sha1(data[16:]).hexdigest()


def generate_game_toml(p: dict) -> str:
    return f"""[game]
output_prefix = "shadowgate"
name = "Shadowgate"
cycle_accurate = true

# Hooks automatically generated for {p['name']}
[[mod_function_hook]]
id     = "shadowgate.narration.message"
addr   = 0x{p['msg']:04X}
length = 16
crc32  = 0x{p['msg_crc']:08X}

[[mod_function_hook]]
id     = "shadowgate.narration.message-continue"
addr   = 0x{p['msg_cont']:04X}
length = 16
crc32  = 0x{p['msg_cont_crc']:08X}

[[mod_function_hook]]
id     = "shadowgate.mouse.text-a"
addr   = 0x{p['text_a']:04X}
length = 16
crc32  = 0x{p['text_a_crc']:08X}

[[mod_function_hook]]
id     = "shadowgate.mouse.page-a"
addr   = 0x{p['page_a']:04X}
length = 16
crc32  = 0x{p['page_a_crc']:08X}
"""


def main():
    if len(sys.argv) < 2:
        print("Usage: rom_profiles.py <path_to_rom> [--write-toml <out_path>]", file=sys.stderr)
        sys.exit(1)

    rom_path = Path(sys.argv[1])
    if not rom_path.is_file():
        print(f"Error: ROM file not found: {rom_path}", file=sys.stderr)
        sys.exit(1)

    sha1 = get_rom_sha1_body(rom_path)
    profile = PROFILES.get(sha1)
    if not profile:
        print(f"Error: Unknown or unsupported ROM (body SHA-1: {sha1})", file=sys.stderr)
        sys.exit(2)

    if "--write-toml" in sys.argv:
        idx = sys.argv.index("--write-toml")
        out_path = Path(sys.argv[idx + 1])
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(generate_game_toml(profile), encoding="utf-8")

    # Output CMake-friendly variables (VAR=VALUE per line)
    print(f"ROM_REGION={profile['region']}")
    print(f"ROM_NAME={profile['name']}")
    print(f"ROM_SHA1={sha1}")
    print(f"SITE_TEXT_A=0x{profile['text_a']:04X}")
    print(f"SITE_PAGE_A=0x{profile['page_a']:04X}")
    print(f"SG_CODE_PRINT_MESSAGE=0x{profile['msg']:04X}")
    print(f"SG_CODE_PRINT_MESSAGE_2=0x{profile['msg_cont']:04X}")
    print(f"SG_TABLE_CODE_TO_TILE=0x{profile['code2tile']:04X}")
    if "huffman" in profile:
        print(f"SG_TABLE_HUFFMAN=0x{profile['huffman']:04X}")


if __name__ == "__main__":
    main()
