#!/usr/bin/env python3
"""
Dolphin Memory Capture for Wind Waker Process Tree Analysis

Connects to a running Dolphin instance via dolphin-memory-engine and dumps
the game's framework process tree, birth queue, SDA globals, and key objects.

Usage:
  1. Launch Dolphin with Wind Waker: Dolphin.exe -e ww.iso
  2. Wait for title screen (camera panning over ocean)
  3. Pause emulation (Dolphin menu: Emulation > Pause)
  4. Run: python tools/dolphin_dump.py > dolphin_capture.json

Requires: pip install dolphin-memory-engine
"""

import json
import struct
import sys
import time

try:
    import dolphin_memory_engine as dme
except ImportError:
    print("ERROR: dolphin-memory-engine not installed.", file=sys.stderr)
    print("  pip install dolphin-memory-engine", file=sys.stderr)
    sys.exit(1)


# ============================================================================
# Constants from the recomp project's memory map
# ============================================================================

R13 = 0x803FE0E0  # SDA base (_SDA_BASE_)

# Process tree (fapGm process list)
PROCESS_TREE_ROOT = 0x803726A0  # +0=head, +4=tail(?), +8=count

# Birth/ready queue (populated by constructors, drained by per-frame func_8003FF00)
BIRTH_QUEUE = 0x803BCEC8

# Creation request queue (fopMsgM)
CREATE_QUEUE = 0x803A72C0  # +36=pending head, +44=count

# Handler vtable
HANDLER_VTABLE = 0x80371D58

# Process vtable
PROCESS_VTABLE = 0x80395070

# Framework dispatch
FW_DISPATCH = 0x803950D8

# fapGm global
FAPGM_GLOBAL = 0x803A5778

# fapGm table
FAPGM_TABLE = 0x803A22F8

# Framework state
FW_STATE = 0x803B9A00

# JKR mount list
JKR_MOUNT_LIST = 0x803ED77C

# SDA offsets (subtracted from R13)
SDA_OFFSETS = {
    "dvd_request_queue":     26400,   # r13(-26400)
    "timing_obj":            26600,   # r13(-26600)
    "root_heap":             27060,   # r13(-27060)
    "current_heap":          27056,   # r13(-27056)
    "scene_manager":         27984,   # r13(-27984)
    "descriptor_head":       28120,   # r13(-28120)
    "descriptor_tail":       28116,   # r13(-28116)
    "root_scene":            30488,   # r13(-30488)
    "scene_created_flag":    30492,   # r13(-30492)
    "archive_heap":          30632,   # r13(-30632)
    "get_current_heap_res":  30640,   # r13(-30640)
    "heap_118C0":            30648,   # r13(-30648)
    "scene_load_handle":     30728,   # r13(-30728)
    "first_alloc_buf":       30740,   # r13(-30740)
    "second_alloc_buf":      30744,   # r13(-30744)
    "scene_loading_state":   30754,   # r13(-30754) int16
    "frame_buf_toggle":      30776,   # r13(-30776)
    "frame_counter":         30792,   # r13(-30792)
    "pending_scene_state":   32576,   # r13(-32576)
    "scene_type_flag":       32719,   # r13(-32719) byte
    "current_scene_state":   32736,   # r13(-32736)
}

# How many bytes to dump per process tree node
NODE_DUMP_SIZE = 0x200

# Max nodes to walk (safety limit)
MAX_NODES = 256


# ============================================================================
# Helpers
# ============================================================================

def read_u8(addr):
    return dme.read_byte(addr)

def read_u16(addr):
    return dme.read_bytes(addr, 2)

def read_u32(addr):
    data = dme.read_bytes(addr, 4)
    return struct.unpack(">I", bytes(data))[0]

def read_s16(addr):
    data = dme.read_bytes(addr, 2)
    return struct.unpack(">h", bytes(data))[0]

def read_blob(addr, size):
    """Read a block of memory and return as hex string."""
    try:
        data = dme.read_bytes(addr, size)
        return bytes(data).hex()
    except Exception as e:
        return f"ERROR: {e}"

def is_valid_ptr(val):
    return 0x80000000 <= val < 0x81800000

def read_string(addr, max_len=64):
    """Read a null-terminated string."""
    try:
        data = dme.read_bytes(addr, max_len)
        s = ""
        for b in data:
            if b == 0:
                break
            s += chr(b) if 32 <= b < 127 else f"\\x{b:02x}"
        return s
    except:
        return ""


# ============================================================================
# Dumpers
# ============================================================================

def dump_sda_globals():
    """Dump all known SDA-relative globals."""
    result = {}
    for name, offset in SDA_OFFSETS.items():
        addr = R13 - offset
        if name == "scene_loading_state":
            val = read_s16(addr)
        elif name == "scene_type_flag":
            val = read_u8(addr)
        else:
            val = read_u32(addr)
        result[name] = {
            "address": f"0x{addr:08X}",
            "sda_offset": f"r13(-{offset})",
            "value": val,
            "hex": f"0x{val:08X}" if isinstance(val, int) and val > 255 else val,
        }
    return result


def dump_process_tree():
    """Walk the process tree at 0x803726A0 and dump each node."""
    head = read_u32(PROCESS_TREE_ROOT + 0)
    field4 = read_u32(PROCESS_TREE_ROOT + 4)
    count = read_u32(PROCESS_TREE_ROOT + 8)

    result = {
        "root_address": f"0x{PROCESS_TREE_ROOT:08X}",
        "head": f"0x{head:08X}",
        "field4": f"0x{field4:08X}",
        "count": count,
        "root_blob": read_blob(PROCESS_TREE_ROOT, 64),
        "nodes": [],
    }

    if count == 0 or not is_valid_ptr(head):
        return result

    # Walk the linked list
    visited = set()
    current = head
    for i in range(min(count + 16, MAX_NODES)):  # extra margin
        if not is_valid_ptr(current) or current in visited:
            break
        visited.add(current)

        vtable = read_u32(current)
        next_ptr = read_u32(current + 4)
        prev_ptr = read_u32(current + 8)
        parent = read_u32(current + 12)

        node = {
            "index": i,
            "address": f"0x{current:08X}",
            "vtable": f"0x{vtable:08X}",
            "next": f"0x{next_ptr:08X}",
            "prev": f"0x{prev_ptr:08X}",
            "parent": f"0x{parent:08X}",
            "blob": read_blob(current, NODE_DUMP_SIZE),
        }

        # Try to read profile ID (often at +0x28 or nearby)
        # Decode some common fields
        for off in [0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30]:
            node[f"field_{off:02X}"] = f"0x{read_u32(current + off):08X}"

        result["nodes"].append(node)

        # Follow next pointer
        if not is_valid_ptr(next_ptr) or next_ptr == head:
            break
        current = next_ptr

    return result


def dump_birth_queue():
    """Dump the birth/ready queue at 0x803BCEC8."""
    blob = read_blob(BIRTH_QUEUE, 128)
    head = read_u32(BIRTH_QUEUE + 0)
    field4 = read_u32(BIRTH_QUEUE + 4)
    field8 = read_u32(BIRTH_QUEUE + 8)

    result = {
        "address": f"0x{BIRTH_QUEUE:08X}",
        "head": f"0x{head:08X}",
        "field4": f"0x{field4:08X}",
        "field8": f"0x{field8:08X}",
        "blob": blob,
        "entries": [],
    }

    # Walk entries if head is valid
    if is_valid_ptr(head):
        visited = set()
        current = head
        for i in range(MAX_NODES):
            if not is_valid_ptr(current) or current in visited:
                break
            visited.add(current)
            entry = {
                "index": i,
                "address": f"0x{current:08X}",
                "blob": read_blob(current, 64),
            }
            for off in range(0, 32, 4):
                entry[f"field_{off:02X}"] = f"0x{read_u32(current + off):08X}"
            result["entries"].append(entry)
            next_ptr = read_u32(current + 4)
            if not is_valid_ptr(next_ptr) or next_ptr == head:
                break
            current = next_ptr

    return result


def dump_creation_queue():
    """Dump the creation request queue at 0x803A72C0."""
    blob = read_blob(CREATE_QUEUE, 64)
    pending_head = read_u32(CREATE_QUEUE + 36)
    pending_count = read_u32(CREATE_QUEUE + 44)

    result = {
        "address": f"0x{CREATE_QUEUE:08X}",
        "pending_head": f"0x{pending_head:08X}",
        "pending_count": pending_count,
        "blob": blob,
        "items": [],
    }

    if is_valid_ptr(pending_head) and pending_count > 0:
        visited = set()
        current = pending_head
        sentinel = CREATE_QUEUE + 36
        for i in range(min(pending_count + 4, MAX_NODES)):
            if not is_valid_ptr(current) or current == sentinel or current in visited:
                break
            visited.add(current)
            item = {
                "index": i,
                "address": f"0x{current:08X}",
                "blob": read_blob(current, 64),
            }
            for off in range(0, 32, 4):
                item[f"field_{off:02X}"] = f"0x{read_u32(current + off):08X}"
            result["items"].append(item)
            next_ptr = read_u32(current + 4)
            current = next_ptr

    return result


def dump_object(name, addr, size=256):
    """Dump an object at a given address."""
    if not is_valid_ptr(addr):
        return {"address": f"0x{addr:08X}", "valid": False}
    result = {
        "name": name,
        "address": f"0x{addr:08X}",
        "valid": True,
        "blob": read_blob(addr, size),
    }
    # Decode first 64 bytes as u32 fields
    for off in range(0, min(size, 64), 4):
        val = read_u32(addr + off)
        result[f"field_{off:02X}"] = f"0x{val:08X}"
    # If first field looks like a vtable ptr, dump the vtable
    vtable = read_u32(addr)
    if is_valid_ptr(vtable):
        vt_entries = []
        for i in range(16):
            vt_entries.append(f"0x{read_u32(vtable + i * 4):08X}")
        result["vtable_entries"] = vt_entries
    return result


def dump_descriptor_list():
    """Walk the process descriptor linked list at r13(-28120)/(-28116)."""
    head = read_u32(R13 - 28120)
    tail = read_u32(R13 - 28116)
    result = {
        "head": f"0x{head:08X}",
        "tail": f"0x{tail:08X}",
        "entries": [],
    }

    if not is_valid_ptr(head):
        return result

    visited = set()
    current = head
    for i in range(MAX_NODES):
        if not is_valid_ptr(current) or current in visited:
            break
        visited.add(current)
        entry = {
            "index": i,
            "address": f"0x{current:08X}",
            "blob": read_blob(current, 64),
        }
        for off in range(0, 32, 4):
            entry[f"field_{off:02X}"] = f"0x{read_u32(current + off):08X}"
        result["entries"].append(entry)
        # Next pointer at +8
        next_ptr = read_u32(current + 8)
        current = next_ptr

    return result


def dump_handler_vtable():
    """Dump the handler vtable at 0x80371D58."""
    entries = []
    for i in range(16):
        entries.append(f"0x{read_u32(HANDLER_VTABLE + i * 4):08X}")
    return {
        "address": f"0x{HANDLER_VTABLE:08X}",
        "entries": entries,
    }


def dump_misc_globals():
    """Dump miscellaneous framework globals."""
    return {
        "fw_dispatch_plus16": f"0x{read_u32(FW_DISPATCH + 16):08X}",
        "fw_state": read_u8(FW_STATE),
        "fapgm_global": f"0x{read_u32(FAPGM_GLOBAL):08X}",
        "fapgm_table": f"0x{read_u32(FAPGM_TABLE):08X}",
        "jkr_mount_list": f"0x{read_u32(JKR_MOUNT_LIST):08X}",
        "process_vtable_blob": read_blob(PROCESS_VTABLE, 64),
    }


def dump_tree_node_details(nodes):
    """For each tree node, try to identify its type by checking profile tables."""
    details = []
    for node in nodes:
        addr = int(node["address"], 16)
        vtable = read_u32(addr)
        # Common framework process fields:
        # +0x00: vtable
        # +0x04: next in tree
        # +0x08: prev in tree
        # +0x0C: parent
        # +0x10: child (first child)
        # +0x14: process ID
        # +0x18: profile ID (or type)
        # +0x1C: flags/state
        detail = {
            "address": node["address"],
            "vtable": f"0x{vtable:08X}",
        }

        # Try various offset interpretations for process ID and profile
        for label, off in [("child", 0x10), ("process_id", 0x14),
                           ("profile_id", 0x18), ("flags", 0x1C),
                           ("name_ptr", 0x20)]:
            val = read_u32(addr + off)
            detail[label] = f"0x{val:08X}"
            # If it looks like a string pointer, try to read it
            if label == "name_ptr" and is_valid_ptr(val):
                detail["name"] = read_string(val)

        details.append(detail)
    return details


# ============================================================================
# Main
# ============================================================================

def main():
    print("Connecting to Dolphin...", file=sys.stderr)

    if not dme.is_hooked():
        dme.hook()
        # Wait a moment for connection
        time.sleep(0.5)

    if not dme.is_hooked():
        print("ERROR: Could not connect to Dolphin.", file=sys.stderr)
        print("  Make sure Dolphin is running with a game loaded.", file=sys.stderr)
        sys.exit(1)

    print("Connected to Dolphin!", file=sys.stderr)

    # Verify we can read memory
    try:
        test = read_u32(0x80000000)
        print(f"  Memory accessible (0x80000000 = 0x{test:08X})", file=sys.stderr)
    except Exception as e:
        print(f"ERROR: Cannot read Dolphin memory: {e}", file=sys.stderr)
        sys.exit(1)

    # Verify this is Wind Waker (check game ID at 0x80000000)
    game_id = read_string(0x80000000, 6)
    print(f"  Game ID: {game_id}", file=sys.stderr)
    if not game_id.startswith("GZLE"):
        print(f"WARNING: Expected GZLE01 (Wind Waker US), got {game_id}", file=sys.stderr)

    capture = {
        "metadata": {
            "game_id": game_id,
            "r13": f"0x{R13:08X}",
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "tool": "dolphin_dump.py",
        },
    }

    # Dump everything
    print("Dumping SDA globals...", file=sys.stderr)
    capture["sda_globals"] = dump_sda_globals()

    print("Dumping process tree...", file=sys.stderr)
    capture["process_tree"] = dump_process_tree()
    tree_count = capture["process_tree"]["count"]
    print(f"  Tree count: {tree_count}", file=sys.stderr)
    print(f"  Nodes captured: {len(capture['process_tree']['nodes'])}", file=sys.stderr)

    if capture["process_tree"]["nodes"]:
        print("Analyzing tree node details...", file=sys.stderr)
        capture["tree_node_details"] = dump_tree_node_details(capture["process_tree"]["nodes"])

    print("Dumping birth queue...", file=sys.stderr)
    capture["birth_queue"] = dump_birth_queue()

    print("Dumping creation queue...", file=sys.stderr)
    capture["creation_queue"] = dump_creation_queue()

    print("Dumping descriptor list...", file=sys.stderr)
    capture["descriptor_list"] = dump_descriptor_list()

    # Dump key objects
    print("Dumping scene manager...", file=sys.stderr)
    scene_mgr_addr = read_u32(R13 - 27984)
    capture["scene_manager"] = dump_object("scene_manager", scene_mgr_addr, 512)

    print("Dumping root scene...", file=sys.stderr)
    root_scene_addr = read_u32(R13 - 30488)
    capture["root_scene"] = dump_object("root_scene", root_scene_addr, 512)

    print("Dumping timing object...", file=sys.stderr)
    timing_addr = read_u32(R13 - 26600)
    capture["timing_obj"] = dump_object("timing_obj", timing_addr, 256)

    print("Dumping handler vtable...", file=sys.stderr)
    capture["handler_vtable"] = dump_handler_vtable()

    print("Dumping misc globals...", file=sys.stderr)
    capture["misc_globals"] = dump_misc_globals()

    # Summary
    print("\n=== Capture Summary ===", file=sys.stderr)
    print(f"  Process tree nodes: {tree_count}", file=sys.stderr)
    print(f"  Birth queue entries: {len(capture['birth_queue']['entries'])}", file=sys.stderr)
    print(f"  Creation queue items: {capture['creation_queue']['pending_count']}", file=sys.stderr)
    print(f"  Descriptor list entries: {len(capture['descriptor_list']['entries'])}", file=sys.stderr)
    print(f"  Scene manager: {capture['scene_manager']['address']} (valid={capture['scene_manager']['valid']})", file=sys.stderr)
    print(f"  Root scene: {capture['root_scene']['address']} (valid={capture['root_scene']['valid']})", file=sys.stderr)

    # Output JSON
    json.dump(capture, sys.stdout, indent=2)
    print(file=sys.stderr)
    print("Done! JSON written to stdout.", file=sys.stderr)


if __name__ == "__main__":
    main()
