#include "InputStream.h"

#include "combase.h"

NS_KRMOVIE_BEGIN
InputStream::InputStream(IStream *s, const std::string &filename) :
    m_pSource(s), m_strFileName(filename) {
    // Keep the filename as an independent copy of the caller-owned string.
    // The copy is completed before the source receives its retained reference.
    s->AddRef();

    STATSTG stg;
    // The HRESULT is intentionally ignored.  The normal storage adapter
    // zero-fills this structure and returns S_OK; another IStream may leave the
    // cached size indeterminate when it reports failure.
    s->Stat(&stg, STATFLAG_NONAME);
    m_nFileSize = stg.cbSize.QuadPart;
}

InputStream::~InputStream() { m_pSource->Release(); }

int InputStream::Read(uint8_t *buf, int buf_size) {
    ULONG readed;
    HRESULT ret = m_pSource->Read(buf, buf_size, &readed);
    // Only exact S_OK is accepted.  S_FALSE and every failure collapse to -1,
    // even if the provider reported a partial byte count.
    if(ret != S_OK)
        return -1;
    return readed;
}

int64_t InputStream::Seek(int64_t offset, int whence) {
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    ULARGE_INTEGER newpos;
    HRESULT ret = m_pSource->Seek(pos, whence, &newpos);
    // Preserve the same exact-S_OK gate and raw 64-bit position conversion.
    if(ret != S_OK)
        return -1;
    return newpos.QuadPart;
}

NS_KRMOVIE_END
