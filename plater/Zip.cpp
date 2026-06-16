#include "Zip.h"

namespace Plater
{
    static uint32_t crc32(const std::string &data)
    {
        static uint32_t table[256];
        static bool ready = false;
        if (!ready) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) {
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                }
                table[i] = c;
            }
            ready = true;
        }

        uint32_t crc = 0xFFFFFFFFu;
        for (unsigned char ch : data) {
            crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    Zip::Zip(const std::string &filename)
        : out(filename.c_str(), std::ios::binary), offset(0), closed(false)
    {
    }

    Zip::~Zip()
    {
        if (!closed) {
            close();
        }
    }

    bool Zip::ok() const
    {
        return out.good();
    }

    void Zip::put16(uint16_t v)
    {
        char b[2] = { (char)(v & 0xFF), (char)((v >> 8) & 0xFF) };
        out.write(b, 2);
    }

    void Zip::put32(uint32_t v)
    {
        char b[4] = {
            (char)(v & 0xFF), (char)((v >> 8) & 0xFF),
            (char)((v >> 16) & 0xFF), (char)((v >> 24) & 0xFF)
        };
        out.write(b, 4);
    }

    void Zip::add(const std::string &name, const std::string &data)
    {
        // Once a write has failed (or a limit was hit), don't keep appending to
        // a broken file -- the offsets would no longer match what's on disk.
        if (!out) {
            return;
        }

        // The classic ZIP format stores sizes/offsets in 32-bit fields and the
        // name length in a 16-bit field. Rather than silently truncating (which
        // produces a corrupt archive), refuse and mark the stream failed so
        // ok()/close() report the error.
        uint64_t need = 30ull + name.size() + data.size();
        if (name.size() > 0xFFFFu ||
            data.size() > 0xFFFFFFFFull ||
            (uint64_t)offset + need > 0xFFFFFFFFull) {
            out.setstate(std::ios::failbit);
            return;
        }

        Entry entry;
        entry.name = name;
        entry.crc = crc32(data);
        entry.size = (uint32_t)data.size();
        entry.offset = offset;
        entries.push_back(entry);

        // Local file header
        put32(0x04034b50);          // signature
        put16(20);                  // version needed
        put16(0);                   // flags
        put16(0);                   // method (0 = stored)
        put16(0);                   // mod time
        put16(0);                   // mod date
        put32(entry.crc);           // crc-32
        put32(entry.size);          // compressed size
        put32(entry.size);          // uncompressed size
        put16((uint16_t)name.size());
        put16(0);                   // extra length
        out.write(name.data(), name.size());
        out.write(data.data(), data.size());

        offset += 30 + (uint32_t)name.size() + entry.size;
    }

    bool Zip::close()
    {
        if (closed) {
            return out.good();
        }
        closed = true;

        // The end-of-central-directory record counts entries in a 16-bit field.
        if (entries.size() > 0xFFFFu) {
            out.setstate(std::ios::failbit);
            return false;
        }

        uint32_t centralStart = offset;

        for (const auto &entry : entries) {
            put32(0x02014b50);          // central directory signature
            put16(20);                  // version made by
            put16(20);                  // version needed
            put16(0);                   // flags
            put16(0);                   // method
            put16(0);                   // mod time
            put16(0);                   // mod date
            put32(entry.crc);
            put32(entry.size);          // compressed size
            put32(entry.size);          // uncompressed size
            put16((uint16_t)entry.name.size());
            put16(0);                   // extra length
            put16(0);                   // comment length
            put16(0);                   // disk number start
            put16(0);                   // internal attributes
            put32(0);                   // external attributes
            put32(entry.offset);        // local header offset
            out.write(entry.name.data(), entry.name.size());

            offset += 46 + (uint32_t)entry.name.size();
        }

        uint32_t centralSize = offset - centralStart;

        // End of central directory record
        put32(0x06054b50);
        put16(0);                       // disk number
        put16(0);                       // disk with central directory
        put16((uint16_t)entries.size());
        put16((uint16_t)entries.size());
        put32(centralSize);
        put32(centralStart);
        put16(0);                       // comment length

        out.flush();
        return out.good();
    }
}
