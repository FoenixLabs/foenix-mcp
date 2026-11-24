#!/usr/bin/env python3
"""
check_sdk.py - checks that the MCP's Calypsi SDK (sdk/calypsi) is in line with the MCP itself

    check_sdk.py [REPO]          REPO = the foenix-mcp folder (default: the folder above tools/)

Programs built with the SDK fill in and read the MCP's own records and call it by number, so the
SDK must give the same numbers and the same record layouts as the MCP's sources. The MCP's Makefile
runs this before it builds: a change on one side only stops the build, with a list of what differs.

What is compared:
  - the SDK's version (MCP_SDK_VERSION_...) is the MCP's (src/version.h)
  - the system call numbers (KFN_...): the same names with the same numbers on both sides
  - every system call the MCP's dispatcher handles has a function in sdk/calypsi/src/mcp_syscalls.c
    (not the network, KFN_NET_..., which is FELINC's felinc_mcp.a, and not KFN_WARMBOOT)
  - the error numbers, the A2560K interrupt numbers, and the other #define values the SDK shares
    with the MCP (models, CPUs, channel and block device numbers, text modes, ...)
  - the records (t_sys_info, t_file_info, t_time, ...): the same members, in the same order, with
    the same types

Input:  the repository folder
Output: "check_sdk: OK" and exit code 0, or the differences and exit code 1
"""

import os
import re
import sys

# The MCP's files the SDK follows, per SDK header (paths from the repository folder)
MCP_VERSION = "src/version.h"
MCP_SYSCALLS = "src/include/syscalls.h"
MCP_ERRORS = "src/include/errors.h"
MCP_INTERRUPT = "src/interrupt.h"
MCP_DISPATCH = "src/m68040/bios_m68040.c"
MCP_TYPE_FILES = ["src/include/types.h", "src/include/constants.h", "src/sys_general.h",
                  "src/dev/channel.h", "src/dev/block.h", "src/dev/fsys.h", "src/dev/rtc.h",
                  "src/dev/txt_screen.h"]

SDK = "sdk/calypsi"
SDK_SYSCALLS = SDK + "/include/mcp/syscalls.h"
SDK_ERRORS = SDK + "/include/mcp/errors.h"
SDK_INTERRUPT = SDK + "/include/mcp/interrupt.h"
SDK_TYPES = SDK + "/include/mcp/types.h"
SDK_SOURCE = SDK + "/src/mcp_syscalls.c"

# The records whose layout must match
RECORDS = ["s_extent", "s_point", "s_rect", "s_color3", "s_color4", "s_channel", "s_dev_chan",
           "s_dev_block", "s_file_info", "s_time", "s_sys_info", "s_txt_capabilities"]

# Dispatcher cases the SDK has no function for, on purpose
NOT_IN_SDK = re.compile(r"^KFN_(NET_\w+|WARMBOOT)$")


def read_code_text(text):
    """
    Remove the comments from C code that is already read.

    Input:  text = C code
    Output: the code, comments replaced by a space (line breaks kept)
    """
    text = re.sub(r"/\*.*?\*/", lambda m: " " + "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def read_code(path):
    """
    Read a C file without its comments.

    Input:  path = the file
    Output: the text, comments replaced by a space (line breaks kept)
    """
    with open(path, encoding="utf-8", errors="replace") as f:
        return read_code_text(f.read())


def number_defines(text):
    """
    Find the #defines whose value is a plain number.

    Input:  text = C code without comments
    Output: {name: value as int}
    """
    defines = {}
    for name, value in re.findall(r"^\s*#define\s+(\w+)\s+\(?(-?(?:0[xX][0-9a-fA-F]+|\d+))\)?\s*$", text, re.M):
        defines[name] = int(value, 0)
    return defines


def records(text):
    """
    Find the struct definitions.

    Input:  text = C code without comments
    Output: {struct name: list of members, each "type name" with single spaces}
    """
    found = {}
    for name, body in re.findall(r"typedef\s+struct\s+(\w+)\s*\{(.*?)\}", text, re.S):
        members = []
        for member in body.split(";"):
            member = " ".join(member.split())
            if member:
                members.append(member.replace(" *", "*").replace("* ", "*"))
        found[name] = members
    return found


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

    def code(path):
        return read_code(os.path.join(repo, path))

    problems = []

    # The SDK's version is the version of the MCP it belongs to
    mcp_version = number_defines(code(MCP_VERSION))
    sdk_version = number_defines(code(SDK_SYSCALLS))
    mcp_v = tuple(mcp_version.get("VER_" + part) for part in ("MAJOR", "MINOR", "BUILD"))
    sdk_v = tuple(sdk_version.get("MCP_SDK_VERSION_" + part) for part in ("MAJOR", "MINOR", "BUILD"))
    if mcp_v != sdk_v:
        problems.append("the MCP is version %s, the SDK (MCP_SDK_VERSION_...) %s" % (mcp_v, sdk_v))

    # The system call numbers: compare the numeric KFN_ names (the SDK's older names are aliases)
    mcp_kfn = {k: v for k, v in number_defines(code(MCP_SYSCALLS)).items() if k.startswith("KFN_")}
    sdk_kfn = {k: v for k, v in number_defines(code(SDK_SYSCALLS)).items() if k.startswith("KFN_")}
    for name in sorted(set(mcp_kfn) | set(sdk_kfn)):
        if name not in sdk_kfn:
            problems.append("%s = 0x%02X is in the MCP but not in the SDK" % (name, mcp_kfn[name]))
        elif name not in mcp_kfn:
            problems.append("%s = 0x%02X is in the SDK but not in the MCP" % (name, sdk_kfn[name]))
        elif mcp_kfn[name] != sdk_kfn[name]:
            problems.append("%s: the MCP has 0x%02X, the SDK 0x%02X" % (name, mcp_kfn[name], sdk_kfn[name]))

    # Every call the MCP handles has an SDK function
    sdk_source = code(SDK_SOURCE)
    for name in sorted(set(re.findall(r"case\s+(KFN_\w+)\s*:", code(MCP_DISPATCH)))):
        if not NOT_IN_SDK.match(name) and not re.search(r"\bsyscall\s*\(\s*%s\b" % name, sdk_source):
            problems.append("the MCP handles %s, but %s has no function for it" % (name, SDK_SOURCE))

    # The error numbers: the same set on both sides
    mcp_err = number_defines(code(MCP_ERRORS))
    sdk_err = number_defines(code(SDK_ERRORS))
    for name in sorted(set(mcp_err) | set(sdk_err)):
        if mcp_err.get(name) != sdk_err.get(name):
            problems.append("error %s: the MCP has %s, the SDK %s" % (name, mcp_err.get(name), sdk_err.get(name)))

    # The interrupt numbers: the MCP's list starts at its "standard interrupt numbers" (the
    # lines above it are register bit masks)
    with open(os.path.join(repo, MCP_INTERRUPT), encoding="utf-8", errors="replace") as f:
        mcp_int_text = f.read()
    start = mcp_int_text.find("Define standard interrupt numbers")
    mcp_int = {k: v for k, v in number_defines(read_code_text(mcp_int_text[start:])).items() if k.startswith("INT_")}
    sdk_int = {k: v for k, v in number_defines(code(SDK_INTERRUPT)).items() if k.startswith("INT_")}
    for name in sorted(set(mcp_int) | set(sdk_int)):
        if mcp_int.get(name) != sdk_int.get(name):
            problems.append("interrupt %s: the MCP has %s, the SDK %s" % (name, mcp_int.get(name), sdk_int.get(name)))

    # The other numbers in types.h: each one the MCP defines too must have the same value
    mcp_types_text = "\n".join(code(path) for path in MCP_TYPE_FILES)
    mcp_numbers = number_defines(mcp_types_text)
    for name, value in sorted(number_defines(code(SDK_TYPES)).items()):
        if name in mcp_numbers and mcp_numbers[name] != value:
            problems.append("%s: the MCP has %d, the SDK %d" % (name, mcp_numbers[name], value))

    # The records
    mcp_records = records(mcp_types_text)
    sdk_records = records(code(SDK_TYPES))
    for name in RECORDS:
        if name not in mcp_records or name not in sdk_records:
            problems.append("struct %s is missing in %s" % (name, "the MCP" if name not in mcp_records else "the SDK"))
        elif mcp_records[name] != sdk_records[name]:
            problems.append("struct %s differs:\n      MCP: %s\n      SDK: %s"
                            % (name, "; ".join(mcp_records[name]), "; ".join(sdk_records[name])))

    if problems:
        print("check_sdk: sdk/calypsi is not in line with the MCP:")
        for problem in problems:
            print("  - " + problem)
        print("check_sdk: update sdk/calypsi (headers and src/mcp_syscalls.c) with the MCP change")
        return 1
    print("check_sdk: OK (%d system calls, %d records)" % (len(mcp_kfn), len(RECORDS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
