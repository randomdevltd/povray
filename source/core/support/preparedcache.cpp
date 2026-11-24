//******************************************************************************
///
/// @file core/support/preparedcache.cpp
///
/// Prepared caches saved to disk: keys, the file format, and the directory's upkeep.
///
/// @copyright
/// @parblock
///
/// Persistence of Vision Ray Tracer ('POV-Ray') version 3.8.
/// Copyright 1991-2019 Persistence of Vision Raytracer Pty. Ltd.
///
/// POV-Ray is free software: you can redistribute it and/or modify
/// it under the terms of the GNU Affero General Public License as
/// published by the Free Software Foundation, either version 3 of the
/// License, or (at your option) any later version.
///
/// POV-Ray is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU Affero General Public License for more details.
///
/// You should have received a copy of the GNU Affero General Public License
/// along with this program.  If not, see <http://www.gnu.org/licenses/>.
///
/// ----------------------------------------------------------------------------
///
/// POV-Ray is based on the popular DKB raytracer version 2.12.
/// DKBTrace was originally written by David K. Buck.
/// DKBTrace Ver 2.0-2.12 were written by David K. Buck & Aaron A. Collins.
///
/// @endparblock
///
//******************************************************************************

// Unit header file must be the first file included within POV-Ray *.cpp files (pulls in config)
#include "core/support/preparedcache.h"

// C++ variants of C standard header files
#include <cerrno>
#include <cstdio>
#include <cstring>

// C++ standard header files
#include <algorithm>
#include <atomic>
#include <random>

// POSIX and platform header files
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/utime.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <utime.h>
#endif
#if defined(__linux__)
#include <link.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// POV-Ray header files (base module)
#include "base/messenger.h"
#include "base/pov_err.h"
#include "base/version_info.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

using std::uint32_t;
using std::uint64_t;

namespace
{

const uint64_t H1 = 11400714785074694791ull, H2 = 14029467366897019727ull, H3 = 1609587929392839161ull;
const uint64_t H4 = 9650029242287828579ull, H5 = 2870177450012600261ull;

inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t Round(uint64_t acc, uint64_t in) { return Rotl(acc + in * H2, 31) * H1; }
inline uint64_t Load64(const unsigned char *p) { uint64_t v; std::memcpy(&v, p, sizeof(v)); return v; }

uint64_t HashBytes(const void *data, size_t n, uint64_t seed)
{
    const unsigned char *p = static_cast<const unsigned char *>(data), *end = p + n;
    uint64_t h = seed + H5;
    if (n >= 32)
    {
        uint64_t v[4] = { seed + H1 + H2, seed + H2, seed, seed - H1 };
        for (; end - p >= 32; p += 32)
            for (int k = 0; k < 4; ++k)
                v[k] = Round(v[k], Load64(p + 8 * k));
        h = Rotl(v[0], 1) + Rotl(v[1], 7) + Rotl(v[2], 12) + Rotl(v[3], 18);
        for (int k = 0; k < 4; ++k)
            h = (h ^ Round(0, v[k])) * H1 + H4;
    }
    h += n;
    for (; end - p >= 8; p += 8)
        h = Rotl(h ^ Round(0, Load64(p)), 27) * H1 + H4;
    for (; p < end; ++p)
        h = Rotl(h ^ (*p * H5), 11) * H1;
    h ^= h >> 33; h *= H2; h ^= h >> 29; h *= H3; h ^= h >> 32;
    return h;
}

#if defined(__linux__)
int FindBuildId(struct dl_phdr_info *info, size_t, void *out)
{
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE)
            continue;
        // Padding aligns each field's end, counted from the note's start: an 8-aligned note's name ends at 16, not 20.
        const size_t align = (ph.p_align > 4) ? size_t(ph.p_align) : 4;
        const auto padded = [align](size_t n) { return (n + align - 1) & ~(align - 1); };
        const unsigned char *p = reinterpret_cast<const unsigned char *>(info->dlpi_addr + ph.p_vaddr), *end = p + ph.p_memsz;
        while (size_t(end - p) >= sizeof(ElfW(Nhdr)))
        {
            const ElfW(Nhdr) *note = reinterpret_cast<const ElfW(Nhdr) *>(p);
            const size_t left = size_t(end - p), descAt = padded(sizeof(ElfW(Nhdr)) + note->n_namesz);
            if (descAt + note->n_descsz > left)
                break;
            const unsigned char *name = p + sizeof(ElfW(Nhdr)), *desc = p + descAt;
            if ((note->n_type == NT_GNU_BUILD_ID) && (note->n_namesz == 4) && (std::memcmp(name, "GNU", 4) == 0))
            {
                static_cast<std::string *>(out)->assign(reinterpret_cast<const char *>(desc), note->n_descsz);
                return 1;
            }
            const size_t next = padded(descAt + note->n_descsz);
            if (next >= left)
                break;
            p += next;
        }
    }
    return 1;
}
#endif

FILE *OpenExecutable()
{
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    return ((n > 0) && (n < path.size())) ? _wfopen(path.data(), L"rb") : nullptr;
#elif defined(__APPLE__)
    char path[4096];
    uint32_t size = sizeof(path);
    return (_NSGetExecutablePath(path, &size) == 0) ? std::fopen(path, "rb") : nullptr;
#else
    return std::fopen("/proc/self/exe", "rb");
#endif
}


/// Differs between any two builds, so a new build rebuilds every cached tree: the GNU build ID where there is one,
/// else a hash of the executable.
uint64_t BuildHashOnce()
{
    static const uint64_t hash = []() {
#if defined(__linux__)
        std::string gnu;
        dl_iterate_phdr(FindBuildId, &gnu);
        if (!gnu.empty())
            return HashBytes(gnu.data(), gnu.size(), 1);
#endif
        if (FILE *exe = OpenExecutable())
        {
            std::vector<unsigned char> chunk(1 << 20);
            uint64_t h = 2;
            for (size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), exe)) > 0; )
                h = HashBytes(chunk.data(), n, h);
            const bool read = !std::ferror(exe);
            std::fclose(exe);
            if (read)
                return h;
        }
        const std::string id = POV_RAY_VERSION " " __DATE__ " " __TIME__;
        return HashBytes(id.data(), id.size(), 3);
    }();
    return hash;
}

const char kMagic[8] = { 'P', 'O', 'V', 'C', 'A', 'C', 'H', 'E' };
const uint32_t kFormat = 1, kByteOrder = 0x01020304u;
/// The header fills the first page; a section of a page or more starts on a page, so it can be mapped from the file alone.
const std::size_t kHeaderBytes = 4096, kPage = 4096, kSmallAlign = 16, kNameBytes = 64, kLabelBytes = 32;
/// offset, length, checksum, id; first and cells; low and high; label
const std::size_t kIndexEntryBytes = 4 * sizeof(uint64_t) + 6 * sizeof(uint32_t) + 6 * sizeof(double) + kLabelBytes;
const std::size_t kHeaderUsed = 8 + 2 * sizeof(uint32_t) + 5 * sizeof(uint64_t) + kNameBytes;

void Append(std::string& out, const void *data, std::size_t n) { out.append(static_cast<const char *>(data), n); }
void AppendText(std::string& out, const std::string& text, std::size_t width)
{
    std::string field(text, 0, std::min(text.size(), width - 1));
    field.resize(width, '\0');
    out += field;
}
template<class T> T Take(const char *& p) { T v; std::memcpy(&v, p, sizeof(v)); p += sizeof(v); return v; }
std::string TakeText(const char *& p, std::size_t width) { std::string text(p, strnlen(p, width)); p += width; return text; }

std::string Hex(uint64_t value)
{
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::string Size(uint64_t bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f MiB", double(bytes) / (1024.0 * 1024.0));
    return text;
}

/// The trailing index of a cache file: each section's place, checksum, cells, bounds and label.
std::string IndexBytes(const std::vector<PreparedCacheFile::Entry>& entries)
{
    std::string index;
    for (const PreparedCacheFile::Entry& entry : entries)
    {
        Append(index, &entry.offset, sizeof(entry.offset));
        Append(index, &entry.length, sizeof(entry.length));
        Append(index, &entry.hash, sizeof(entry.hash));
        Append(index, &entry.section.id, sizeof(entry.section.id));
        Append(index, entry.section.first.data(), sizeof(entry.section.first));
        Append(index, entry.section.cells.data(), sizeof(entry.section.cells));
        for (const Vector3d& corner : { entry.section.low, entry.section.high })
            for (int axis = X; axis <= Z; ++axis)
            {
                const double value = corner[axis];
                Append(index, &value, sizeof(value));
            }
        AppendText(index, entry.label, kLabelBytes);
    }
    return index;
}

/// The header of a cache file, with its own checksum.
std::string HeaderBytes(uint64_t key, uint64_t count, uint64_t indexOffset, uint64_t indexHash, const std::string& name)
{
    std::string header(kMagic, sizeof(kMagic));
    const uint64_t build = BuildHash();
    Append(header, &kFormat, sizeof(kFormat));
    Append(header, &kByteOrder, sizeof(kByteOrder));
    Append(header, &build, sizeof(build));
    Append(header, &key, sizeof(key));
    Append(header, &count, sizeof(count));
    Append(header, &indexOffset, sizeof(indexOffset));
    Append(header, &indexHash, sizeof(indexHash));
    AppendText(header, name, kNameBytes);
    const uint64_t headerHash = ContentHash(header.data(), header.size(), 0);
    Append(header, &headerHash, sizeof(headerHash));
    return header;
}

std::string Describe(const CacheSection& section)
{
    if (section.id == CacheSection::kWhole)
        return "whole";
    char text[96];
    std::snprintf(text, sizeof(text), "grid %u brick %u at cell %u,%u,%u", unsigned(section.id >> 32), unsigned(section.id & 0xffffffffu),
                  section.first[0], section.first[1], section.first[2]);
    return text;
}

bool MakeDirectories(const std::string& directory)
{
    for (std::size_t at = 1; at <= directory.size(); ++at)
    {
        if ((at < directory.size()) && (directory[at] != '/') && (directory[at] != '\\'))
            continue;
        const std::string part = directory.substr(0, at);
#if defined(_WIN32)
        const int made = _mkdir(part.c_str());
#else
        const int made = mkdir(part.c_str(), 0777);
#endif
        if ((made != 0) && (errno != EEXIST))
            return false;
    }
    struct stat info;
    return (stat(directory.c_str(), &info) == 0) && ((info.st_mode & S_IFDIR) != 0);
}

std::vector<std::string> ListDirectory(const std::string& directory)
{
    std::vector<std::string> names;
#if defined(_WIN32)
    _finddata_t found;
    const intptr_t handle = _findfirst((directory + "/*").c_str(), &found);
    if (handle == -1)
        return names;
    do
        names.push_back(found.name);
    while (_findnext(handle, &found) == 0);
    _findclose(handle);
#else
    if (DIR *dir = opendir(directory.c_str()))
    {
        while (const dirent *entry = readdir(dir))
            names.push_back(entry->d_name);
        closedir(dir);
    }
#endif
    std::sort(names.begin(), names.end());
    return names;
}

long ProcessId()
{
#if defined(_WIN32)
    return long(_getpid());
#else
    return long(getpid());
#endif
}

bool EndsWith(const std::string& text, const char *suffix)
{
    const std::size_t n = std::strlen(suffix);
    return (text.size() >= n) && (text.compare(text.size() - n, n, suffix) == 0);
}

/// A saved cache or a scene manifest: the files the size cap counts.
bool CacheFileName(const std::string& name)
{
    return EndsWith(name, ".povc") || EndsWith(name, ".povk");
}

/// A temporary file a writer has not renamed into place yet.
bool TempFileName(const std::string& name)
{
    return (name.find(".povc.tmp") != std::string::npos) || (name.find(".povk.tmp") != std::string::npos);
}

#if defined(_WIN32)
using StatInfo = struct _stat64;
inline int StatPath(const std::string& path, StatInfo& info) { return _stat64(path.c_str(), &info); }
#else
using StatInfo = struct stat;
inline int StatPath(const std::string& path, StatInfo& info) { return stat(path.c_str(), &info); }
#endif

InputStamp ToStamp(const StatInfo& info)
{
    InputStamp stamp;
    stamp.size = uint64_t(info.st_size);
#if defined(__APPLE__)
    stamp.modified = uint64_t(info.st_mtimespec.tv_sec) * 1000000000ull + uint64_t(info.st_mtimespec.tv_nsec);
    stamp.changed = uint64_t(info.st_ctimespec.tv_sec) * 1000000000ull + uint64_t(info.st_ctimespec.tv_nsec);
#elif defined(_WIN32)
    stamp.modified = uint64_t(info.st_mtime) * 1000000000ull;
    stamp.changed = uint64_t(info.st_ctime) * 1000000000ull;
#else
    stamp.modified = uint64_t(info.st_mtim.tv_sec) * 1000000000ull + uint64_t(info.st_mtim.tv_nsec);
    stamp.changed = uint64_t(info.st_ctim.tv_sec) * 1000000000ull + uint64_t(info.st_ctim.tv_nsec);
#endif
    stamp.inode = uint64_t(info.st_ino);
    stamp.device = uint64_t(info.st_dev);
    return stamp;
}

/// Seconds since the epoch of a file's last modification, or -1.
double ModifiedSeconds(const std::string& path, uint64_t *bytes = nullptr)
{
    StatInfo info;
    if (StatPath(path, info) != 0)
        return -1.0;
    if (bytes != nullptr)
        *bytes = uint64_t(info.st_size);
    return double(ToStamp(info).modified) * 1e-9;
}

/// Marks a file as just used, so the size cap removes it last.
void Touch(const std::string& path)
{
#if defined(_WIN32)
    _utime(path.c_str(), nullptr);
#else
    utime(path.c_str(), nullptr);
#endif
}

/// 64 bits that differ between any two writers: random bytes mixed with the time and the process id.
uint64_t Uniquifier()
{
    uint64_t value = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                     (uint64_t(std::chrono::system_clock::now().time_since_epoch().count()) * H2) ^ (uint64_t(ProcessId()) * H3);
    try
    {
        std::random_device device;
        value ^= (uint64_t(device()) << 32) ^ uint64_t(device());
    }
    catch (...)
    {
    }
    static std::atomic<uint64_t> counter(0);
    return HashBytes(&value, sizeof(value), ++counter);
}

/// Creates `base` plus a name no other writer, in this process or another container sharing the directory, can have.
std::FILE *CreateTemp(const std::string& base, std::string& temp)
{
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        temp = base + ".tmp-" + Hex(uint64_t(ProcessId())) + "-" + Hex(Uniquifier());
#if defined(_WIN32)
        const int fd = _open(temp.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
        if (fd >= 0)
            return _fdopen(fd, "wb");
#else
        const int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
        if (fd >= 0)
            return fdopen(fd, "wb");
#endif
        if (errno != EEXIST)
            break;
    }
    temp.clear();
    return nullptr;
}

const char kManifestMagic[8] = { 'P', 'O', 'V', 'C', 'K', 'E', 'Y', 'S' };

void SerializeStamp(CacheArchive& a, InputStamp& stamp)
{
    Serialize(a, stamp.size);
    Serialize(a, stamp.modified);
    Serialize(a, stamp.changed);
    Serialize(a, stamp.inode);
    Serialize(a, stamp.device);
}

std::string ManifestPath(const std::string& directory, uint64_t preKey)
{
    return directory + "/scene-" + Hex(preKey) + ".povk";
}

}
// end of anonymous namespace

uint64_t ContentHash(const void *data, std::size_t n, uint64_t seed)
{
    return HashBytes(data, n, seed);
}

uint64_t BuildHash()
{
    return BuildHashOnce();
}

CacheGridBounds LatticeBounds(const Vector3d& low, const Vector3d& step, bool nodes)
{
    return [low, step, nodes](const std::array<std::size_t, 3>& first, const std::array<std::size_t, 3>& end, Vector3d& from, Vector3d& to)
    {
        for (int axis = X; axis <= Z; ++axis)
        {
            from[axis] = low[axis] + double(first[axis]) * step[axis];
            to[axis] = low[axis] + double(nodes ? end[axis] - 1 : end[axis]) * step[axis];
        }
    };
}

bool StampFile(const std::string& path, InputStamp& stamp)
{
    StatInfo info;
    if (StatPath(path, info) != 0)
        return false;
    stamp = ToStamp(info);
    return true;
}

bool HashFile(const std::string& path, uint64_t& hash, InputStamp& stamp)
{
    InputStamp before, after;
    if (!StampFile(path, before))
        return false;
    std::FILE *file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return false;
    std::vector<char> chunk(1 << 20);
    uint64_t h = 0, bytes = 0;
    for (std::size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), file)) > 0; bytes += n)
        h = ContentHash(chunk.data(), n, h);
    const bool read = !std::ferror(file);
    std::fclose(file);
    // a write during the read leaves contents no stamp describes
    if (!read || !StampFile(path, after) || (after != before) || (bytes != before.size))
        return false;
    hash = h;
    stamp = before;
    return true;
}

bool WriteSceneManifest(const std::string& directory, uint64_t preKey, uint64_t sceneKey, const std::vector<CacheInput>& inputs)
{
    if (!MakeDirectories(directory))
        return false;
    std::string bytes(kManifestMagic, sizeof(kManifestMagic));
    CacheArchive archive(bytes);
    uint64_t build = BuildHash(), key = sceneKey;
    Serialize(archive, build);
    Serialize(archive, key);
    std::vector<CacheInput> list(inputs);
    archive.Count(list.size(), 1);
    for (CacheInput& input : list)
    {
        Serialize(archive, input.referenced);
        Serialize(archive, input.path);
        Serialize(archive, input.hash);
        SerializeStamp(archive, input.stamp);
    }
    const uint64_t check = ContentHash(bytes.data(), bytes.size(), 0);
    bytes.append(reinterpret_cast<const char *>(&check), sizeof(check));
    const std::string path = ManifestPath(directory, preKey);
    std::string temp;
    std::FILE *out = CreateTemp(path, temp);
    if (out == nullptr)
        return false;
    bool ok = (std::fwrite(bytes.data(), 1, bytes.size(), out) == bytes.size());
    ok = (std::fclose(out) == 0) && ok;
    ok = ok && (std::rename(temp.c_str(), path.c_str()) == 0);
    if (!ok)
        std::remove(temp.c_str());
    return ok;
}

bool ReadSceneManifest(const std::string& directory, uint64_t preKey, uint64_t& sceneKey, std::vector<CacheInput>& inputs)
{
    const std::string path = ManifestPath(directory, preKey);
    std::FILE *in = std::fopen(path.c_str(), "rb");
    if (in == nullptr)
        return false;
    std::string bytes;
    std::vector<char> chunk(1 << 16);
    for (std::size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), in)) > 0; )
        bytes.append(chunk.data(), n);
    std::fclose(in);
    uint64_t check;
    if ((bytes.size() < sizeof(kManifestMagic) + sizeof(check)) || (std::memcmp(bytes.data(), kManifestMagic, sizeof(kManifestMagic)) != 0))
        return false;
    std::memcpy(&check, bytes.data() + bytes.size() - sizeof(check), sizeof(check));
    if (ContentHash(bytes.data(), bytes.size() - sizeof(check), 0) != check)
        return false;
    try
    {
        CacheArchive archive(bytes.data() + sizeof(kManifestMagic), bytes.data() + bytes.size() - sizeof(check));
        uint64_t build = 0, key = 0;
        Serialize(archive, build);
        Serialize(archive, key);
        if (build != BuildHash())
            return false;
        std::vector<CacheInput> list(std::size_t(archive.Count(0, 1)));
        for (CacheInput& input : list)
        {
            Serialize(archive, input.referenced);
            Serialize(archive, input.path);
            Serialize(archive, input.hash);
            SerializeStamp(archive, input.stamp);
        }
        if (!archive.Done())
            return false;
        sceneKey = key;
        inputs.swap(list);
        return true;
    }
    catch (const CacheFormatError&)
    {
        return false;
    }
}

bool HasSavedCaches(const std::string& directory, const std::string& kind, uint64_t sceneKey)
{
    const std::string tag = "-" + Hex(sceneKey) + "-";
    for (const std::string& entry : ListDirectory(directory))
        if ((entry.compare(0, kind.size(), kind) == 0) && EndsWith(entry, ".povc") && (entry.find(tag) != std::string::npos))
            return true;
    return false;
}

bool PreparedCacheFile::ReadAt(uint64_t offset, void *data, std::size_t n) const
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> guard(reading);
    return (_fseeki64(file, __int64(offset), SEEK_SET) == 0) && (std::fread(data, 1, n, file) == n);
#else
    char *at = static_cast<char *>(data);
    while (n > 0)
    {
        const ssize_t got = pread(file, at, n, off_t(offset));
        if (got <= 0)
        {
            if ((got < 0) && (errno == EINTR))
                continue;
            return false;
        }
        at += got;
        offset += uint64_t(got);
        n -= std::size_t(got);
    }
    return true;
#endif
}

PreparedCacheFile::PreparedCacheFile(const std::string& path)
{
    uint64_t size = 0;
#if defined(_WIN32)
    file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return;
    open = true;
    struct _stat64 info;
    if (_fstat64(_fileno(file), &info) != 0)
        throw CacheFormatError("it cannot be read");
    size = uint64_t(info.st_size);
#else
    file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (file < 0)
        return;
    open = true;
    struct stat info;
    if (fstat(file, &info) != 0)
        throw CacheFormatError("it cannot be read");
    size = uint64_t(info.st_size);
#endif
    std::vector<char> header(kHeaderBytes);
    if ((size < kHeaderBytes) || !ReadAt(0, header.data(), header.size()))
        throw CacheFormatError("it is truncated within its header");
    const char *p = header.data();
    if (std::memcmp(p, kMagic, sizeof(kMagic)) != 0)
        throw CacheFormatError("it is not a prepared cache file");
    p += sizeof(kMagic);
    const uint32_t format = Take<uint32_t>(p), order = Take<uint32_t>(p);
    build = Take<uint64_t>(p);
    key = Take<uint64_t>(p);
    const uint64_t count = Take<uint64_t>(p), indexOffset = Take<uint64_t>(p), indexHash = Take<uint64_t>(p);
    name = TakeText(p, kNameBytes);
    if ((format != kFormat) || (order != kByteOrder))
        throw CacheFormatError("it was written in another format or byte order");
    if (Take<uint64_t>(p) != ContentHash(header.data(), kHeaderUsed, 0))
        throw CacheFormatError("its header is damaged");
    const uint64_t length = size;
    bytes = length;
    if ((indexOffset < kHeaderBytes) || (indexOffset > length) || (count > (length - indexOffset) / kIndexEntryBytes) ||
        (indexOffset + count * kIndexEntryBytes != length))
        throw CacheFormatError("it is truncated or has bytes past its index");
    std::vector<char> index(std::size_t(count * kIndexEntryBytes));
    if (!index.empty() && !ReadAt(indexOffset, index.data(), index.size()))
        throw CacheFormatError("its index cannot be read");
    if (ContentHash(index.data(), index.size(), 0) != indexHash)
        throw CacheFormatError("its index is damaged");
    p = index.data();
    for (uint64_t i = 0; i < count; ++i)
    {
        Entry entry;
        entry.offset = Take<uint64_t>(p);
        entry.length = Take<uint64_t>(p);
        entry.hash = Take<uint64_t>(p);
        entry.section.id = Take<uint64_t>(p);
        for (int axis = 0; axis < 3; ++axis)
            entry.section.first[axis] = Take<uint32_t>(p);
        for (int axis = 0; axis < 3; ++axis)
            entry.section.cells[axis] = Take<uint32_t>(p);
        for (int axis = X; axis <= Z; ++axis)
            entry.section.low[axis] = Take<double>(p);
        for (int axis = X; axis <= Z; ++axis)
            entry.section.high[axis] = Take<double>(p);
        entry.label = TakeText(p, kLabelBytes);
        if ((entry.offset < kHeaderBytes) || (entry.offset > indexOffset) || (entry.length > indexOffset - entry.offset))
            throw CacheFormatError("section " + std::to_string(i) + " lies outside the file");
        entries.push_back(entry);
    }
}

PreparedCacheFile::~PreparedCacheFile()
{
#if defined(_WIN32)
    if (file != nullptr)
        std::fclose(file);
#else
    if (file >= 0)
        close(file);
#endif
}

std::size_t PreparedCacheFile::Find(uint64_t id) const
{
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].section.id == id)
            return i;
    throw CacheFormatError("no section has id " + Hex(id));
}

std::string PreparedCacheFile::Read(std::size_t index) const
{
    const Entry& entry = entries.at(index);
    std::string bytes(std::size_t(entry.length), '\0');
    if (!bytes.empty() && !ReadAt(entry.offset, &bytes[0], bytes.size()))
        throw CacheFormatError("section " + std::to_string(index) + " cannot be read");
    if (ContentHash(bytes.data(), bytes.size(), 0) != entry.hash)
        throw CacheFormatError("section " + std::to_string(index) + " (" + entry.label + ", " + Describe(entry.section) + ") is damaged");
    return bytes;
}

void PreparedCacheFile::Check() const
{
    for (std::size_t i = 0; i < entries.size(); ++i)
        Read(i);
}

PreparedCaches::PreparedCaches(const std::string& dir, uint64_t key, bool check, uint64_t cap) :
    directory(dir),
    sceneKey(key),
    verify(check),
    maxBytes(cap)
{
    // a temporary file this old belongs to a render that stopped before renaming it
    const double now = double(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) * 1e-9;
    for (const std::string& entry : ListDirectory(directory))
        if (TempFileName(entry))
        {
            const std::string path = directory + "/" + entry;
            const double modified = ModifiedSeconds(path);
            if ((modified >= 0.0) && (now - modified > kStaleTempSeconds))
                std::remove(path.c_str());
        }
}

void PreparedCaches::Use(const std::string& path)
{
    std::lock_guard<std::mutex> guard(lock);
    used.insert(path);
}

void PreparedCaches::Trim()
{
    std::lock_guard<std::mutex> guard(lock);
    TrimLocked();
}

void PreparedCaches::TrimLocked()
{
    if (maxBytes == 0)
        return;
    struct Found { double modified; uint64_t bytes; std::string path; };
    std::vector<Found> files;
    uint64_t total = 0;
    for (const std::string& entry : ListDirectory(directory))
        if (CacheFileName(entry))
        {
            Found found{ 0.0, 0, directory + "/" + entry };
            found.modified = ModifiedSeconds(found.path, &found.bytes);
            if (found.modified < 0.0)
                continue;
            total += found.bytes;
            files.push_back(found);
        }
    if (total <= maxBytes)
        return;
    std::sort(files.begin(), files.end(), [](const Found& a, const Found& b) { return a.modified < b.modified; });
    for (const Found& found : files)
    {
        if (total <= maxBytes)
            break;
        // what this render uses stays; another render reading a removed file keeps its open handle
        if ((used.count(found.path) != 0) || (std::remove(found.path.c_str()) != 0))
            continue;
        total -= found.bytes;
        ++trimmed;
        trimmedBytes += found.bytes;
    }
}

unsigned int PreparedCaches::Next(const std::string& kind, int ordinal)
{
    std::lock_guard<std::mutex> guard(lock);
    unsigned int& next = ordinals[kind];
    if (ordinal < 0)
        return next++;
    next = std::max(next, unsigned(ordinal) + 1);
    return unsigned(ordinal);
}

void PreparedCaches::Info(const std::string& line)
{
    if (messages != nullptr)
        messages->Info("%s", line.c_str());
}

void PreparedCaches::Warn(const std::string& line)
{
    if (messages != nullptr)
        messages->Warning(pov_base::kWarningGeneral, "%s", line.c_str());
}

void PreparedCaches::Summarise()
{
    std::lock_guard<std::mutex> guard(lock);
    if (loaded + built + verified == 0)
        return;
    char line[512];
    std::snprintf(line, sizeof(line), "Prepared caches: %u loaded (%s), %u built and saved (%s), %u rebuilt and verified; in %s.",
                  loaded, Size(loadedBytes).c_str(), built, Size(builtBytes).c_str(), verified, directory.c_str());
    if (messages != nullptr)
        messages->Info("%s", line);
    if ((trimmed > 0) && (messages != nullptr))
        messages->Info("Prepared caches: %u least recently used file(s) removed (%s) to keep %s within Cache_Max_Size (%s).", trimmed,
                       Size(trimmedBytes).c_str(), directory.c_str(), Size(maxBytes).c_str());
}

PreparedCacheUnit::PreparedCacheUnit(PreparedCaches *store, const std::string& kind, uint64_t detail, int ordinal) :
    caches(store)
{
    if (caches == nullptr)
        return;
    start = std::chrono::steady_clock::now();
    name = kind + std::to_string(caches->Next(kind, ordinal));
    key = CacheKey(caches->SceneKey()).Add(name).Add(detail).Value();
    // the scene key in the name lets a render ask whether caches of its key exist before it knows their details
    path = caches->Directory() + "/" + name + "-" + Hex(caches->SceneKey()) + "-" + Hex(key) + ".povc";
    Open();
}

PreparedCacheUnit::~PreparedCacheUnit()
{
    if (out != nullptr)
        std::fclose(out);
    if (!temp.empty())
        std::remove(temp.c_str());
}

void PreparedCacheUnit::Open()
{
    std::string problem;
    if (Read(problem))
    {
        mode = caches->Verify() ? Mode::Verify : Mode::Load;
        return;
    }
    mode = Mode::Build;
    file.reset();
    saved.clear();
    if (!problem.empty())
    {
        caches->Warn("Prepared cache " + path + " is rejected (" + problem + ") and rebuilt.");
        std::remove(path.c_str());
        reason = "; rebuilt after rejecting a damaged file";
        return;
    }
    // another key's file may serve another render sharing the directory, so it stays; the size cap ages it out
    const std::string prefix = name + "-";
    for (const std::string& entry : ListDirectory(caches->Directory()))
        if ((entry.compare(0, prefix.size(), prefix) == 0) && EndsWith(entry, ".povc"))
        {
            reason = "; rebuilt because its key changed: the scene, an input file, a setting or the build differs from the saved one";
            break;
        }
}

bool PreparedCacheUnit::Read(std::string& problem)
{
    try
    {
        std::unique_ptr<PreparedCacheFile> opened(new PreparedCacheFile(path));
        if (opened->Missing())
            return false;
        if ((opened->Build() != BuildHash()) || (opened->Key() != key) || (opened->Name() != name))
            throw CacheFormatError("its header names another build, key or cache than its file name");
        // every section is checked before any is used, one at a time; a later increment checks only those a region reads
        opened->Check();
        saved = opened->Entries();
        fileBytes = opened->Bytes();
        file = std::move(opened);
        caches->Use(path);
        Touch(path);
        return true;
    }
    catch (const CacheFormatError& error)
    {
        problem = error.what();
        return false;
    }
}

void PreparedCacheUnit::Mismatch(const std::string& what)
{
    std::remove(path.c_str());
    throw POV_EXCEPTION_STRING(("Prepared cache " + path + " does not fit this scene's preparation: " + what +
                                ". Its key missed an input that changed; it was deleted, so the next render rebuilds it.").c_str());
}

void PreparedCacheUnit::Load(const char *label, const std::function<void(CacheArchive&)>& fields, const CacheSection *section)
{
    if (next >= saved.size())
        Mismatch(std::string("it holds fewer sections than the preparation reads, ending before ") + label);
    const PreparedCacheFile::Entry& entry = saved[next];
    if (entry.label != std::string(label, 0, kLabelBytes - 1))
        Mismatch("section " + std::to_string(next) + " is " + entry.label + " where the preparation reads " + label);
    if (!(entry.section == (section ? *section : CacheSection())))
        Mismatch("section " + std::to_string(next) + " (" + label + ") is " + Describe(entry.section) + " where the preparation reads " +
                 Describe(section ? *section : CacheSection()));
    try
    {
        // read now, from the file, and released when done
        const std::string bytes = file->Read(next);
        CacheArchive archive(bytes.data(), bytes.data() + bytes.size());
        fields(archive);
        if (!archive.Done())
            throw CacheFormatError("the section is longer than what was read");
    }
    catch (const CacheFormatError& error)
    {
        Mismatch("section " + std::to_string(next) + " (" + label + "): " + error.what());
    }
    ++next;
}

void PreparedCacheUnit::Note(const char *label, std::string& text)
{
    if (mode == Mode::Off)
        return;
    if (mode == Mode::Load)
    {
        Load(label, [&](CacheArchive& archive) { Serialize(archive, text); }, nullptr);
        return;
    }
    if (mode == Mode::Verify)
    {
        std::string saved;
        Load(label, [&](CacheArchive& archive) { Serialize(archive, saved); }, nullptr);
        return;
    }
    std::string bytes;
    CacheArchive archive(bytes);
    Serialize(archive, text);
    Store(label, std::move(bytes), nullptr);
}

void PreparedCacheUnit::Store(const char *label, std::string&& bytes, const CacheSection *section)
{
    if (mode == Mode::Verify)
    {
        const bool same = (next < saved.size()) && (saved[next].section == (section ? *section : CacheSection())) &&
                          (file->Read(next) == bytes);
        if (!same)
            throw POV_EXCEPTION_STRING(("Cache_Verify: the rebuilt " + name + " differs from its saved cache " + path + " at section " +
                                        std::to_string(next) + " (" + label + ", " + Describe(section ? *section : CacheSection()) +
                                        "). A saved cache would have rendered differently from a fresh build.").c_str());
        ++next;
        return;
    }
    if (failed)
        return;
    if (out == nullptr)
    {
        if (!MakeDirectories(caches->Directory()))
        {
            failed = true;
            caches->Warn("Prepared cache " + name + " is not saved: cannot create the directory " + caches->Directory() + ".");
            return;
        }
        out = CreateTemp(path, temp);
        const std::string blank(kHeaderBytes, '\0');
        if ((out == nullptr) || (std::fwrite(blank.data(), 1, blank.size(), out) != blank.size()))
        {
            failed = true;
            caches->Warn("Prepared cache " + name + " is not saved: cannot write a temporary file beside " + path + " (" + std::strerror(errno) + ").");
            return;
        }
        written = kHeaderBytes;
    }
    const uint64_t align = (bytes.size() >= kPage) ? kPage : kSmallAlign;
    const uint64_t offset = (written + align - 1) / align * align;
    const std::string pad(std::size_t(offset - written), '\0');
    if ((std::fwrite(pad.data(), 1, pad.size(), out) != pad.size()) ||
        (std::fwrite(bytes.data(), 1, bytes.size(), out) != bytes.size()))
    {
        failed = true;
        caches->Warn("Prepared cache " + name + " is not saved: writing " + temp + " failed (" + std::strerror(errno) + ").");
        return;
    }
    PreparedCacheFile::Entry entry{ offset, uint64_t(bytes.size()), ContentHash(bytes.data(), bytes.size(), 0), label,
                                    section ? *section : CacheSection() };
    saved.push_back(entry);
    written = offset + bytes.size();
}

bool PreparedCacheUnit::Write()
{
    if (failed)
        return false;
    if (out == nullptr)
    {
        // a cache of no sections still records that its preparation was done
        Store("", std::string(), nullptr);
        saved.pop_back();
        if (failed)
            return false;
    }
    const uint64_t indexOffset = (written + 7) / 8 * 8;
    std::string index(std::size_t(indexOffset - written), '\0');
    const std::size_t padding = index.size();
    index += IndexBytes(saved);
    const std::string header = HeaderBytes(key, saved.size(), indexOffset, ContentHash(index.data() + padding, index.size() - padding, 0), name);
    bool ok = (std::fwrite(index.data(), 1, index.size(), out) == index.size()) && (std::fseek(out, 0, SEEK_SET) == 0) &&
              (std::fwrite(header.data(), 1, header.size(), out) == header.size());
    ok = (std::fclose(out) == 0) && ok;
    out = nullptr;
    // the rename publishes a complete file or nothing
    ok = ok && (std::rename(temp.c_str(), path.c_str()) == 0);
    if (!ok)
    {
        caches->Warn("Prepared cache " + name + " is not saved: writing " + path + " failed (" + std::strerror(errno) + ").");
        std::remove(temp.c_str());
    }
    temp.clear();
    written = indexOffset + index.size() - padding;
    return ok;
}

void PreparedCacheUnit::Finish()
{
    if ((caches == nullptr) || finished)
        return;
    finished = true;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    char timing[64];
    std::snprintf(timing, sizeof(timing), "%.3f s", seconds);
    if (mode == Mode::Build)
    {
        const bool ok = Write();
        std::lock_guard<std::mutex> guard(caches->lock);
        if (ok)
        {
            ++caches->built;
            caches->builtBytes += written;
            caches->used.insert(path);
            caches->TrimLocked();
        }
        caches->Info("Prepared cache " + name + ": built" + (ok ? " and saved, " + Size(written) : std::string(", not saved")) + ", " +
                     std::to_string(saved.size()) + " sections, " + timing + reason + ".");
        return;
    }
    if (next != saved.size())
        Mismatch("it holds " + std::to_string(saved.size()) + " sections where the preparation read " + std::to_string(next));
    file.reset();
    std::lock_guard<std::mutex> guard(caches->lock);
    if (mode == Mode::Load)
    {
        ++caches->loaded;
        caches->loadedBytes += fileBytes;
        caches->Info("Prepared cache " + name + ": loaded, " + Size(fileBytes) + ", " + std::to_string(saved.size()) + " sections, " + timing + ".");
    }
    else
    {
        ++caches->verified;
        caches->Info("Prepared cache " + name + ": rebuilt and verified identical to its saved file, " + Size(fileBytes) + ", " +
                     std::to_string(saved.size()) + " sections, " + timing + ".");
    }
}


CacheSpillFile::CacheSpillFile(const std::string& directory, const std::string& name, bool keepFile) :
    keep(keepFile)
{
    if (!MakeDirectories(directory))
        throw std::runtime_error("cannot create the spill directory " + directory);
    path = directory + "/" + name + "-" + std::to_string(ProcessId()) + "-" + Hex(Uniquifier()) + ".povc";
#if defined(_WIN32)
    file = std::fopen(path.c_str(), "w+bx");
    if (file == nullptr)
        throw std::runtime_error("cannot create the spill file " + path + " (" + std::strerror(errno) + ")");
#else
    file = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (file < 0)
        throw std::runtime_error("cannot create the spill file " + path + " (" + std::strerror(errno) + ")");
    // an unlinked file lives until it is closed, so nothing is left behind however the render ends
    if (!keep && (unlink(path.c_str()) == 0))
        linked = false;
#endif
    const std::string blank(kHeaderBytes, '\0');
    WriteAt(0, blank.data(), blank.size());
    end = kHeaderBytes;
}

CacheSpillFile::~CacheSpillFile()
{
#if defined(_WIN32)
    if (file != nullptr)
        std::fclose(file);
#else
    if (file >= 0)
        close(file);
#endif
    if (linked && !keep)
        std::remove(path.c_str());
}

void CacheSpillFile::WriteAt(uint64_t offset, const void *data, std::size_t n)
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> guard(lock);
    if ((_fseeki64(file, __int64(offset), SEEK_SET) != 0) || (std::fwrite(data, 1, n, file) != n))
        throw std::runtime_error("writing the spill file " + path + " failed (" + std::strerror(errno) + ")");
#else
    const char *at = static_cast<const char *>(data);
    while (n > 0)
    {
        const ssize_t put = pwrite(file, at, n, off_t(offset));
        if (put <= 0)
        {
            if ((put < 0) && (errno == EINTR))
                continue;
            throw std::runtime_error("writing the spill file " + path + " failed (" + std::strerror(errno) + ")");
        }
        at += put;
        offset += uint64_t(put);
        n -= std::size_t(put);
    }
#endif
}

void CacheSpillFile::ReadAt(uint64_t offset, void *data, std::size_t n) const
{
#if defined(_WIN32)
    std::lock_guard<std::mutex> guard(lock);
    if ((_fseeki64(file, __int64(offset), SEEK_SET) != 0) || (std::fread(data, 1, n, file) != n))
        throw CacheFormatError("the spill file " + path + " cannot be read");
#else
    char *at = static_cast<char *>(data);
    while (n > 0)
    {
        const ssize_t got = pread(file, at, n, off_t(offset));
        if (got <= 0)
        {
            if ((got < 0) && (errno == EINTR))
                continue;
            throw CacheFormatError("the spill file " + path + " cannot be read");
        }
        at += got;
        offset += uint64_t(got);
        n -= std::size_t(got);
    }
#endif
}

std::size_t CacheSpillFile::Append(const char *label, const void *data, std::size_t n, const CacheSection *section)
{
    PreparedCacheFile::Entry entry{ 0, uint64_t(n), ContentHash(data, n, 0), label, section ? *section : CacheSection() };
    std::size_t index;
    {
        // the place is taken under the lock; the bytes are written outside it, so threads spill side by side
        std::lock_guard<std::mutex> guard(lock);
        if (finished)
            throw std::runtime_error("the spill file " + path + " is finished");
        const uint64_t align = (n >= kPage) ? kPage : kSmallAlign;
        entry.offset = (end + align - 1) / align * align;
        end = entry.offset + n;
        index = entries.size();
        entries.push_back(entry);
    }
    if (n > 0)
        WriteAt(entry.offset, data, n);
    return index;
}

PreparedCacheFile::Entry CacheSpillFile::Entry(std::size_t index) const
{
    std::lock_guard<std::mutex> guard(lock);
    return entries.at(index);
}

void CacheSpillFile::Read(std::size_t index, void *data) const
{
    const PreparedCacheFile::Entry entry = Entry(index);
    if (entry.length > 0)
        ReadAt(entry.offset, data, std::size_t(entry.length));
    if (ContentHash(data, std::size_t(entry.length), 0) != entry.hash)
        throw CacheFormatError("section " + std::to_string(index) + " (" + entry.label + ") of the spill file " + path + " is damaged");
}

int CacheSpillFile::Descriptor() const
{
#if defined(_WIN32)
    return -1;
#else
    return file;
#endif
}

uint64_t CacheSpillFile::Bytes() const
{
    std::lock_guard<std::mutex> guard(lock);
    return end;
}

void CacheSpillFile::Finish()
{
    std::vector<PreparedCacheFile::Entry> written;
    uint64_t indexOffset;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (finished)
            return;
        finished = true;
        written = entries;
        indexOffset = (end + 7) / 8 * 8;
    }
    const std::string index = IndexBytes(written);
    WriteAt(indexOffset, index.data(), index.size());
    const std::string header = HeaderBytes(0, written.size(), indexOffset, ContentHash(index.data(), index.size(), 0), "spill");
    WriteAt(0, header.data(), header.size());
#if defined(_WIN32)
    std::fflush(file);
#endif
}

}
// end of namespace pov
