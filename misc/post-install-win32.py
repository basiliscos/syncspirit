#!/usr/bin/env python

import pefile
import sys
import os
import shutil
import subprocess
import argparse

from os.path import join

onerror = lambda err: print(err)
def gather(entry_dir, dlls, bitness):
    for root, dirs, files in os.walk(entry_dir, topdown=False, onerror = onerror, followlinks=True):
        for file in files:
            if (file.endswith(".dll")):
                full_path = join(root, file)
                #print(full_path)
                try:
                    pe = pefile.PE(full_path, fast_load = True)
                    pe_bitness = pe.OPTIONAL_HEADER.Magic
                    if (pe_bitness == bitness):
                        dlls[file.lower()] = full_path
                except Exception as e:
                    None;

def resolve(path):
    if (subprocess.check_output("uname").decode("utf-8").strip("\n") == "Linux"):
        # print(subprocess.check_output("uname").decode("utf-8").strip("\n"))
        return path
    else:
        # print(subprocess.check_output("uname"))
        return subprocess.check_output(["cygpath", "-w", path]).decode("utf-8").strip("\n")

parser = argparse.ArgumentParser(description='Gathers DLLs.')
parser.add_argument('--binaries', nargs='+', help='full paths to binaries for scaning')
parser.add_argument('--dirs', nargs='*', help='additional dirs for looking up dependencies')

bitness = None
args = parser.parse_args()

scan_queue = []
for file_name in args.binaries.copy():
    file_ext = os.path.splitext(file_name)[1]
    if (file_ext == ".dll" or file_ext == ".exe"):
        scan_queue.append(file_name)

for file_name in scan_queue:
    file_ext = os.path.splitext(file_name)[1]
    if (file_ext == ".dll" or file_ext == ".exe"):
        pe = pefile.PE(file_name, fast_load = True)
        pe_bitness = pe.OPTIONAL_HEADER.Magic
        if bitness is None:
            bitness = pe_bitness
        elif bitness != pe_bitness:
            raise("binaries has different bitness")

available_dlls = dict()
for d in args.dirs:
    gather(resolve(d), available_dlls, bitness)

existing_dir = os.path.dirname(args.binaries[0])
existing_dlls = dict()
gather(existing_dir, existing_dlls, bitness)

visited = dict()
needed_dlls = dict()

while scan_queue:
    file_name =  scan_queue.pop(0)
    # print(f"checking '{file_name}'")
    visited[file_name] = True
    pe = pefile.PE(file_name)


    if pe_bitness == bitness:
        for item in pe.DIRECTORY_ENTRY_IMPORT:
            dll = item.dll.lower().decode("utf-8")
            # print(f"{dll}, bitness = {bitness} / {pe_bitness}")
            if (dll not in needed_dlls and dll in available_dlls and dll not in existing_dlls):
                full_path = available_dlls[dll]
                needed_dlls[dll] = full_path
                if (dll not in visited):
                    scan_queue.append(full_path)

for dll, path in needed_dlls.items():
    print(f"{path}")
    shutil.copy(path, existing_dir)
