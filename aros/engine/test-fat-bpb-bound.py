#!/usr/bin/env python3
"""Host arithmetic regression for the AROS BPB companion patch, not native proof."""
import argparse
import ctypes
import pathlib
import shutil
import struct
import subprocess
import tempfile


class Info(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint32) for name in (
        'sector_size', 'sectors_per_cluster', 'cluster_size', 'reserved_sectors',
        'fat_count', 'fat_size', 'total_sectors', 'root_dir_sectors',
        'data_sectors', 'cluster_count', 'first_root_dir_sector',
        'first_data_sector', 'dos_type')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='fat-bpb-bound-') as directory:
        root = pathlib.Path(directory)
        for name in ('exec', 'dos', 'linklibs'):
            (root / name).mkdir()
        # Only the platform scalar declarations and filesystem IDs are stubbed.
        # Both builds execute the actual parser source, without AROS runtime IO.
        (root / 'exec/types.h').write_text(
            '#include <stdint.h>\n#include <stddef.h>\n'
            'typedef uint8_t UBYTE; typedef uint16_t UWORD; typedef uint32_t ULONG;\n'
            'typedef uint64_t UQUAD; typedef int BOOL;\n#define TRUE 1\n#define FALSE 0\n')
        (root / 'dos/filesystemids.h').write_text(
            '#define ID_FAT12_DISK 12\n#define ID_FAT16_DISK 16\n#define ID_FAT32_DISK 32\n')
        shutil.copy2(args.source / 'compiler/fatbpb/include/fatbpb.h', root / 'linklibs/fatbpb.h')
        shutil.copy2(args.source / 'compiler/fatbpb/bpb.c', root / 'original.c')
        shutil.copy2(root / 'original.c', root / 'bpb.c')
        shutil.copy2(args.source / 'rom/filesys/fat/volume.c', root / 'volume.c')
        subprocess.run(['patch', '--batch', '--fuzz=0', '-p0', '-i',
                        str(repo / 'aros/platform/fat-bpb-volume-bound.patch')], cwd=root, check=True)
        libraries = []
        for stem in ('original', 'bpb'):
            library = root / (stem + '.so')
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-fPIC', '-shared',
                            '-I', str(root), str(root / (stem + '.c')), '-o', str(library)], check=True)
            function = ctypes.CDLL(str(library)).FAT_ParseBPB
            function.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64, ctypes.POINTER(Info)]
            function.restype = ctypes.c_int
            libraries.append(function)

        def parse(function, sectors, media_sectors):
            boot = bytearray(512)
            struct.pack_into('<H', boot, 11, 512)
            boot[13] = 32
            struct.pack_into('<H', boot, 14, 32)
            boot[16] = 2
            struct.pack_into('<H', boot, 17, 512)
            boot[21] = 0xf8
            struct.pack_into('<H', boot, 22, 224)
            struct.pack_into('<I', boot, 32, sectors)
            boot[510:512] = b'\x55\xaa'
            buffer = ctypes.create_string_buffer(bytes(boot))
            result = Info()
            return function(buffer, 512, media_sectors * 512, ctypes.byref(result)), result

        original, fixed = libraries
        ok, result = parse(original, 1835001, 1835008)
        assert ok and result.cluster_count == 57328, 'baseline reproduction changed'
        for padding in range(65):
            ok, result = parse(fixed, 1835001, 1835001 + padding)
            assert ok and result.cluster_count == 57327
            assert result.data_sectors == 1835001 - 512
            assert result.first_data_sector + result.cluster_count * 32 <= result.total_sectors
        assert not parse(fixed, 1835001, 1835066)[0], 'oversized geometry accepted'
        assert not parse(fixed, 1835001, 1835000)[0], 'short device accepted'
        assert parse(original, 480, 544)[0], 'baseline overhead reproduction changed'
        assert not parse(fixed, 480, 544)[0], 'overhead outside volume accepted'
        print('PASS BPB boundary: baseline extra cluster reproduced; padding=0..64; short/oversized/overhead refused')


if __name__ == '__main__':
    main()
