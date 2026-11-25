#!/usr/bin/env python3
"""Re-sign an lld -adhoc_codesign arm64 Mach-O with an XML entitlements blob.

  entsign.py IN OUT ENTITLEMENTS.plist [IDENTIFIER]

The kernel (patch 0035) reads entitlements from the XML blob (special slot -5) of a valid ad-hoc
signature. lld writes no entitlements, and ipsw's signer made a signature the kernel did not accept
(csops status 0). This keeps lld's CodeDirectory fields, adds the entitlements blob and its special
slot hash, resizes LC_CODE_SIGNATURE and __LINKEDIT, and rehashes every 4K page.
"""
import hashlib, plistlib, struct, sys

LC_SEGMENT_64, LC_CODE_SIGNATURE = 0x19, 0x1d
CSMAGIC_EMBEDDED_SIGNATURE, CSMAGIC_CODEDIRECTORY, CSMAGIC_EMBEDDED_ENTITLEMENTS = 0xfade0cc0, 0xfade0c02, 0xfade7171
CSSLOT_CODEDIRECTORY, CSSLOT_ENTITLEMENTS = 0, 5


def main():
    src, dst, ents = sys.argv[1], sys.argv[2], sys.argv[3]
    ident = (sys.argv[4] if len(sys.argv) > 4 else None)
    b = bytearray(open(src, 'rb').read())
    xml = plistlib.dumps(plistlib.load(open(ents, 'rb')), fmt=plistlib.FMT_XML)
    eblob = struct.pack('>II', CSMAGIC_EMBEDDED_ENTITLEMENTS, 8 + len(xml)) + xml

    ncmds, = struct.unpack_from('<I', b, 16)
    off, sig_lc, linkedit = 32, None, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from('<II', b, off)
        if cmd == LC_CODE_SIGNATURE:
            sig_lc = off
        elif cmd == LC_SEGMENT_64 and bytes(b[off + 8:off + 24]).rstrip(b'\0') == b'__LINKEDIT':
            linkedit = off
        off += size
    assert sig_lc and linkedit, 'needs LC_CODE_SIGNATURE and __LINKEDIT'
    sig_off, sig_size = struct.unpack_from('<II', b, sig_lc + 8)

    # lld's CodeDirectory: keep its header fields
    sb = b[sig_off:sig_off + sig_size]
    magic, _, count = struct.unpack_from('>III', sb, 0)
    assert magic == CSMAGIC_EMBEDDED_SIGNATURE
    cd = None
    for i in range(count):
        typ, o = struct.unpack_from('>II', sb, 12 + 8 * i)
        if typ == CSSLOT_CODEDIRECTORY:
            cd = sb[o:]
    (cmagic, _, version, flags, hash_off, ident_off, nspecial, ncode, code_limit, hsize, htype, platform,
     pshift, _spare2) = struct.unpack_from('>IIIIIIIIIBBBBI', cd, 0)
    assert cmagic == CSMAGIC_CODEDIRECTORY and hsize == 32 and pshift == 12
    old_ident = bytes(cd[ident_off:cd.index(b'\0', ident_off) + 1])
    idb = (ident.encode() + b'\0') if ident else old_ident
    # tail runs from the end of the fixed header up to the identifier
    fixed_end = 44
    extra = bytes(cd[fixed_end:ident_off])

    nspecial = CSSLOT_ENTITLEMENTS
    new_ident_off = fixed_end + len(extra)
    new_hash_off = new_ident_off + len(idb) + 32 * nspecial
    pages = (code_limit + 4095) // 4096
    cd_len = new_hash_off + 32 * pages
    sb_len = 12 + 8 * 2 + cd_len + len(eblob)
    sb_len_al = (sb_len + 15) & ~15

    # new signature size into the load commands, then __LINKEDIT sizes
    struct.pack_into('<I', b, sig_lc + 12, sb_len_al)
    le_vmaddr, le_vmsize, le_fileoff, le_filesize = struct.unpack_from('<QQQQ', b, linkedit + 24)
    new_end = sig_off + sb_len_al
    le_filesize = new_end - le_fileoff
    le_vmsize = (le_filesize + 0x3fff) & ~0x3fff
    struct.pack_into('<QQQQ', b, linkedit + 24, le_vmaddr, le_vmsize, le_fileoff, le_filesize)
    del b[sig_off:]

    special = [b'\0' * 32] * nspecial
    special[CSSLOT_ENTITLEMENTS - 1] = hashlib.sha256(eblob).digest()
    code = b''.join(hashlib.sha256(bytes(b[p * 4096:min((p + 1) * 4096, code_limit)])).digest() for p in range(pages))
    hdr = struct.pack('>IIIIIIIIIBBBBI', CSMAGIC_CODEDIRECTORY, cd_len, version, flags, new_hash_off, new_ident_off,
                      nspecial, pages, code_limit, 32, htype, platform, 12, 0)
    cdb = hdr + extra + idb + b''.join(reversed(special)) + code
    assert len(cdb) == cd_len
    out = struct.pack('>III', CSMAGIC_EMBEDDED_SIGNATURE, sb_len, 2)
    out += struct.pack('>II', CSSLOT_CODEDIRECTORY, 28) + struct.pack('>II', CSSLOT_ENTITLEMENTS, 28 + cd_len)
    out += cdb + eblob
    out += b'\0' * (sb_len_al - len(out))
    b += out
    open(dst, 'wb').write(b)


if __name__ == '__main__':
    main()
