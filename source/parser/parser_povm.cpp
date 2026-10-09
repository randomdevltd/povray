//******************************************************************************
///
/// @file parser/parser_povm.cpp
///
/// This module loads binary `.povm` mesh geometry and caches its bounding tree
/// beside it. The format is described in `doc/povm.md`.
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
#include "parser/parser.h"

// C++ variants of C standard header files
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

// C++ standard header files
#include <algorithm>
#include <cstdarg>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// POSIX and platform header files
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#if defined(__linux__)
#include <link.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

// POV-Ray header files (base module)
#include "base/fileinputoutput.h"
#include "base/path.h"
#include "base/platformbase.h"
#include "base/pov_mem.h"
#include "base/stringutilities.h"
#include "base/version_info.h"

// POV-Ray header files (core module)
#include "core/bounding/boundingbox.h"
#include "core/scene/scenedata.h"
#include "core/scene/tracethreaddata.h"
#include "core/shape/mesh.h"

#if defined(_WIN32)
#include <windows.h>
#endif

// this must be the last file included
#include "base/povdebug.h"

namespace pov_parser
{

using std::uint32_t;
using std::uint64_t;

namespace
{

std::runtime_error PovmError(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return std::runtime_error(text);
}

const char POVM_MAGIC[4] = { 'P', 'O', 'V', 'M' };
const uint32_t POVM_VERSION = 1;
const uint32_t POVM_NORMAL_INDICES = 1;
const uint32_t POVM_UV_INDICES = 2;
const uint64_t POVM_HEADER_BYTES = 28;

const char TREE_MAGIC[8] = { 'P', 'O', 'V', 'M', 'T', 'R', 'E', 'E' };
const uint32_t TREE_VERSION = 1;

struct TreeHeader final
{
    char magic[8];
    uint32_t version;
    uint32_t blockBytes;
    uint64_t build;
    uint64_t povmBytes;
    uint64_t povmHash;
    uint32_t triangles;
    uint32_t blocks;
    uint64_t blocksHash;
};

static_assert(sizeof(MeshVector) == 3 * sizeof(float), "vertices are read straight into MeshVector arrays");
static_assert(sizeof(MESH_TRIANGLE) == 3 * sizeof(uint32_t), "face indices are read straight into triangles");
static_assert(sizeof(MeshIndex) == sizeof(uint32_t), "index columns are read straight into MeshIndex arrays");

const uint64_t H1 = 11400714785074694791ull, H2 = 14029467366897019727ull, H3 = 1609587929392839161ull;
const uint64_t H4 = 9650029242287828579ull, H5 = 2870177450012600261ull;

inline uint64_t Rotl(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }
inline uint64_t Round(uint64_t acc, uint64_t in) { return Rotl(acc + in * H2, 31) * H1; }
inline uint64_t Load64(const unsigned char *p) { uint64_t v; std::memcpy(&v, p, sizeof(v)); return v; }

/// A 64-bit hash of `n` bytes in the manner of xxHash64, chained through `seed`.
uint64_t Hash_Bytes(const void *data, size_t n, uint64_t seed)
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

bool Little_Endian()
{
    const uint32_t one = 1;
    unsigned char c;
    std::memcpy(&c, &one, 1);
    return c == 1;
}

void To_Host_Order(void *data, size_t words)
{
    if (Little_Endian())
        return;
    unsigned char *p = static_cast<unsigned char *>(data);
    for (size_t i = 0; i < words; ++i, p += 4)
    {
        std::swap(p[0], p[3]);
        std::swap(p[1], p[2]);
    }
}

bool Finite(const float *p, size_t n)
{
    // By bit pattern: -ffast-math folds std::isfinite to true.
    for (size_t i = 0; i < n; ++i)
    {
        uint32_t u;
        std::memcpy(&u, p + i, sizeof(u));
        if ((u & 0x7f800000u) == 0x7f800000u)
            return false;
    }
    return true;
}

uint32_t Word(const unsigned char *p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

#if defined(__linux__)
int Find_Build_Id(struct dl_phdr_info *info, size_t, void *out)
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

FILE *Open_Executable()
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
uint64_t Build_Hash()
{
    static const uint64_t hash = []() {
#if defined(__linux__)
        std::string gnu;
        dl_iterate_phdr(Find_Build_Id, &gnu);
        if (!gnu.empty())
            return Hash_Bytes(gnu.data(), gnu.size(), 1);
#endif
        if (FILE *exe = Open_Executable())
        {
            std::vector<unsigned char> chunk(1 << 20);
            uint64_t h = 2;
            for (size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), exe)) > 0; )
                h = Hash_Bytes(chunk.data(), n, h);
            const bool read = !std::ferror(exe);
            std::fclose(exe);
            if (read)
                return h;
        }
        const std::string id = POV_RAY_VERSION " " __DATE__ " " __TIME__;
        return Hash_Bytes(id.data(), id.size(), 3);
    }();
    return hash;
}

/// True if every child reference lies inside the tree and after its block, so a walk always ends.
bool Valid_Tree(const std::vector<FlatQBBoxBlock>& blocks, uint32_t triangles)
{
    const size_t n = blocks.size();
    if ((n == 0) || (blocks[0].count != 1) || blocks[0].more)
        return false;
    for (size_t b = 0; b < n; ++b)
    {
        const FlatQBBoxBlock& k = blocks[b];
        if ((k.count < 1) || (k.count > FLAT_BBOX_WIDTH) || (k.more > 1) || (k.more && (b + 1 >= n)))
            return false;
        for (int i = 0; i < k.count; ++i)
        {
            const std::int32_t c = k.child[i];
            if ((c >= 0) ? ((size_t(c) <= b) || (size_t(c) >= n)) : (uint32_t(-1 - std::int64_t(c)) >= triangles))
                return false;
        }
    }
    return true;
}

enum class TreeRead { Loaded, Missing, Stale, Corrupt };

TreeRead Read_Tree(const PovmTreeKey& key, uint32_t triangles, FlatMeshBBoxTree *& tree)
{
    std::unique_ptr<IStream> file;
    try
    {
        file.reset(NewIStream(key.path, POV_File_Data_POVM));
    }
    catch (pov_base::Exception&)
    {
        return TreeRead::Missing;
    }
    if ((file == nullptr) || !*file)
        return TreeRead::Missing;

    TreeHeader h;
    if (!file->read(&h, sizeof(h)) || (std::memcmp(h.magic, TREE_MAGIC, sizeof(TREE_MAGIC)) != 0))
        return TreeRead::Corrupt;
    if ((h.version != TREE_VERSION) || (h.blockBytes != sizeof(FlatQBBoxBlock)) || (h.build != Build_Hash()) ||
        (h.povmBytes != key.bytes) || (h.povmHash != key.hash) || (h.triangles != triangles))
        return TreeRead::Stale;
    if (!file->seekg(0, IOBase::seek_end) || (uint64_t(file->tellg()) != sizeof(h) + uint64_t(h.blocks) * sizeof(FlatQBBoxBlock)) ||
        !file->seekg(sizeof(h)))
        return TreeRead::Corrupt;

    std::unique_ptr<FlatMeshBBoxTree> t(new FlatMeshBBoxTree);
    t->blocks.resize(h.blocks);
    if (!file->read(t->blocks.data(), t->blocks.size() * sizeof(FlatQBBoxBlock)) ||
        (Hash_Bytes(t->blocks.data(), t->blocks.size() * sizeof(FlatQBBoxBlock), 0) != h.blocksHash) ||
        !Valid_Tree(t->blocks, triangles))
        return TreeRead::Corrupt;
    tree = t.release();
    return TreeRead::Loaded;
}

const double STALE_TEMP_SECONDS = 600.0;
const double BUSY_TEMP_SECONDS = 2.0;

#if defined(_WIN32)
using FileStat = struct ::_stat64;
#else
using FileStat = struct stat;
#endif

int Create_Exclusive(const UCS2String& path)
{
#if defined(_WIN32)
    return _wopen(reinterpret_cast<const wchar_t *>(path.c_str()), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return open(UCS2toSysString(path).c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
}

bool Stat(const UCS2String& path, FileStat& st)
{
#if defined(_WIN32)
    return _wstat64(reinterpret_cast<const wchar_t *>(path.c_str()), &st) == 0;
#else
    return stat(UCS2toSysString(path).c_str(), &st) == 0;
#endif
}

bool Stat(FILE *file, FileStat& st)
{
#if defined(_WIN32)
    return _fstat64(_fileno(file), &st) == 0;
#else
    return fstat(fileno(file), &st) == 0;
#endif
}

/// Whether `path` is still the file `st` describes; Windows reports no inode, but cannot delete a file held open.
bool Same_File(const UCS2String& path, const FileStat& st)
{
    FileStat now;
#if defined(_WIN32)
    return Stat(path, now);
#else
    return Stat(path, now) && (now.st_dev == st.st_dev) && (now.st_ino == st.st_ino);
#endif
}

bool Remove(const UCS2String& path)
{
#if defined(_WIN32)
    return _wremove(reinterpret_cast<const wchar_t *>(path.c_str())) == 0;
#else
    return std::remove(UCS2toSysString(path).c_str()) == 0;
#endif
}

bool Replace(const UCS2String& from, const UCS2String& to)
{
#if defined(_WIN32)
    return MoveFileExW(reinterpret_cast<const wchar_t *>(from.c_str()), reinterpret_cast<const wchar_t *>(to.c_str()),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    return std::rename(UCS2toSysString(from).c_str(), UCS2toSysString(to).c_str()) == 0;
#endif
}

enum class TreeWrite { Written, Busy, Held, Invalid, Failed };

/// Writes through `<cache>.tmp`, created exclusively: a render finding it fresh leaves it to its writer, one left by a
/// crash (or dated in the future) is replaced, and only a writer whose own file is still there renames it into place.
TreeWrite Write_Tree(const PovmTreeKey& key, uint32_t triangles, const FlatMeshBBoxTree& tree)
{
    if (!Valid_Tree(tree.blocks, triangles))
        return TreeWrite::Invalid;
    const UCS2String temp = key.path + SysToUCS2String(".tmp");
    PlatformBase& platform = PlatformBase::GetInstance();
    if (!platform.AllowLocalFileAccess(temp, POV_File_Data_POVM, true) || !platform.AllowLocalFileAccess(key.path, POV_File_Data_POVM, true))
        return TreeWrite::Failed;

    int fd = Create_Exclusive(temp);
    int err = errno;
    if ((fd < 0) && (err == EEXIST))
    {
        FileStat st;
        const double age = Stat(temp, st) ? std::difftime(std::time(nullptr), st.st_mtime) : -1.0;
        if ((age >= 0.0) && (age <= STALE_TEMP_SECONDS))
            return (age < BUSY_TEMP_SECONDS) ? TreeWrite::Busy : TreeWrite::Held;
        Remove(temp);
        fd = Create_Exclusive(temp);
        err = errno;
        if ((fd < 0) && (err == EEXIST))
            return TreeWrite::Busy;
    }
    if (fd < 0)
        return TreeWrite::Failed;
#if defined(_WIN32)
    FILE *file = _fdopen(fd, "wb");
#else
    FILE *file = fdopen(fd, "wb");
#endif
    if (file == nullptr)
    {
#if defined(_WIN32)
        _close(fd);
#else
        close(fd);
#endif
        Remove(temp);
        return TreeWrite::Failed;
    }
    FileStat mine;
    if (!Stat(file, mine))
    {
        std::fclose(file);
        Remove(temp);
        return TreeWrite::Failed;
    }

    TreeHeader h;
    std::memcpy(h.magic, TREE_MAGIC, sizeof(TREE_MAGIC));
    h.version = TREE_VERSION;
    h.blockBytes = sizeof(FlatQBBoxBlock);
    h.build = Build_Hash();
    h.povmBytes = key.bytes;
    h.povmHash = key.hash;
    h.triangles = triangles;
    h.blocks = uint32_t(tree.blocks.size());
    h.blocksHash = Hash_Bytes(tree.blocks.data(), tree.blocks.size() * sizeof(FlatQBBoxBlock), 0);
    bool written = (std::fwrite(&h, sizeof(h), 1, file) == 1) &&
                   (std::fwrite(tree.blocks.data(), sizeof(FlatQBBoxBlock), tree.blocks.size(), file) == tree.blocks.size());
    written = (std::fclose(file) == 0) && written;
    if (!Same_File(temp, mine))
        return TreeWrite::Busy;
    if (written && Replace(temp, key.path))
        return TreeWrite::Written;
    Remove(temp);
    return TreeWrite::Failed;
}

const char GENERATED_MAGIC[8] = { 'P', 'O', 'V', 'G', 'M', 'E', 'S', 'H' };
const uint32_t GENERATED_VERSION = 1;

/// Appends the generated mesh file's fields; Take reads them back in order, failing on a short or inconsistent file.
struct Packer final
{
    std::string bytes;
    template<typename T> void Put(const T& v) { bytes.append(reinterpret_cast<const char *>(&v), sizeof(v)); }
    void Put(const void *p, size_t n) { Put(uint64_t(n)); bytes.append(static_cast<const char *>(p), n); }
};

struct Unpacker final
{
    const unsigned char *p, *end;
    template<typename T> bool Take(T& v)
    {
        if (size_t(end - p) < sizeof(v))
            return false;
        std::memcpy(&v, p, sizeof(v));
        p += sizeof(v);
        return true;
    }
    bool Take(std::string& out)
    {
        uint64_t n;
        if (!Take(n) || (uint64_t(end - p) < n))
            return false;
        out.assign(reinterpret_cast<const char *>(p), size_t(n));
        p += n;
        return true;
    }
    template<typename T> bool Array(T *& out, MeshIndex count)
    {
        uint64_t n;
        if (!Take(n) || (n != uint64_t(count) * sizeof(T)) || (uint64_t(end - p) < n))
            return false;
        out = (count > 0) ? reinterpret_cast<T *>(POV_MALLOC(size_t(n), "triangle mesh data")) : nullptr;
        if (n > 0)
            std::memcpy(out, p, size_t(n));
        p += n;
        return true;
    }
};

void Pack_Column(Packer& out, const MeshIndexColumn& c)
{
    out.Put(c.fill);
    out.Put(uint8_t(c.byVertex));
    out.Put(int32_t(c.width));
    out.Put(c.values.data(), c.values.size() * sizeof(MeshIndex));
}

bool Unpack_Column(Unpacker& in, MeshIndexColumn& c)
{
    uint8_t byVertex;
    int32_t width;
    std::string values;
    if (!in.Take(c.fill) || !in.Take(byVertex) || !in.Take(width) || (width != c.width) || !in.Take(values) ||
        (values.size() % sizeof(MeshIndex) != 0))
        return false;
    c.byVertex = (byVertex != 0);
    c.values.resize(values.size() / sizeof(MeshIndex));
    std::memcpy(c.values.data(), values.data(), values.size());
    return true;
}

}
// end of anonymous namespace

GeneratedMeshKey Generated_Mesh_Key(const UCS2String& dir, const std::string& definition)
{
    GeneratedMeshKey key;
    if (dir.empty())
        return key;
    key.a = Hash_Bytes(definition.data(), definition.size(), Build_Hash());
    key.b = Hash_Bytes(definition.data(), definition.size(), ~key.a);
    char name[40];
    std::snprintf(name, sizeof(name), "%016llx%016llx.povg", (unsigned long long)key.a, (unsigned long long)key.b);
    key.path = dir;
    if ((key.path.back() != '/') && (key.path.back() != '\\'))
        key.path += SysToUCS2String("/");
    key.path += SysToUCS2String(name);
    return key;
}

GeneratedMeshKey Generated_Mesh_Key(const UCS2String& dir, const std::string& definition, TraceThreadData *thread)
{
    if (definition.empty())
        return GeneratedMeshKey();
    std::string full = definition;
    Describe_Bytes(full, thread->GetSceneData()->noiseGenerator);
    return Generated_Mesh_Key(dir, full);
}

std::shared_ptr<Mesh> Read_Generated_Mesh(const GeneratedMeshKey& key, DeferredMeshState& state)
{
    if (!key.Valid() || !PlatformBase::GetInstance().AllowLocalFileAccess(key.path, POV_File_Data_POVM, false))
        return nullptr;
    FILE *file = std::fopen(UCS2toSysString(key.path).c_str(), "rb");
    if (file == nullptr)
        return nullptr;
    std::string bytes;
    std::vector<char> chunk(1 << 20);
    for (size_t n; (n = std::fread(chunk.data(), 1, chunk.size(), file)) > 0; )
        bytes.append(chunk.data(), n);
    const bool read = !std::ferror(file);
    std::fclose(file);
    const std::string path = UCS2toSysString(key.path);
    uint64_t trailer;
    if (!read || (bytes.size() < sizeof(trailer)))
        return nullptr;
    std::memcpy(&trailer, bytes.data() + bytes.size() - sizeof(trailer), sizeof(trailer));
    Unpacker in { reinterpret_cast<const unsigned char *>(bytes.data()), reinterpret_cast<const unsigned char *>(bytes.data()) + bytes.size() - sizeof(trailer) };
    char magic[8];
    uint32_t version;
    uint64_t build, a, b;
    if ((Hash_Bytes(bytes.data(), bytes.size() - sizeof(trailer), 0) != trailer) || !in.Take(magic) ||
        (std::memcmp(magic, GENERATED_MAGIC, sizeof(magic)) != 0) || !in.Take(version) || (version != GENERATED_VERSION) ||
        !in.Take(build) || (build != Build_Hash()) || !in.Take(a) || (a != key.a) || !in.Take(b) || (b != key.b))
    {
        state.warnings.push_back("Generated mesh cache '" + path + "' does not match its name; rebuilding it.");
        return nullptr;
    }

    std::shared_ptr<Mesh> mesh(new Mesh());
    mesh->Data = new MESH_DATA();
    mesh->Data->References = 1;
    MESH_DATA& data = *mesh->Data;
    uint8_t inside, patch, tree;
    uint64_t blocks;
    std::string debug, warning;
    uint32_t warnings;
    bool ok = in.Take(inside) && in.Take(patch) && in.Take(mesh->BBox) && in.Take(data.Inside_Vect) &&
              in.Take(data.Number_Of_Vertices) && in.Take(data.Number_Of_Normals) && in.Take(data.Number_Of_UVCoords) &&
              in.Take(data.Number_Of_Triangles) && (data.Number_Of_Vertices >= 0) && (data.Number_Of_Normals >= 0) &&
              (data.Number_Of_UVCoords >= 0) && (data.Number_Of_Triangles >= 0) &&
              in.Array(data.Vertices, data.Number_Of_Vertices) && in.Array(data.Normals, data.Number_Of_Normals) &&
              in.Array(data.UVCoords, data.Number_Of_UVCoords) && in.Array(data.Triangles, data.Number_Of_Triangles) &&
              Unpack_Column(in, data.NormalInd) && Unpack_Column(in, data.UVInd) && Unpack_Column(in, data.TextureInd) &&
              Unpack_Column(in, data.Texture23Ind) && in.Take(tree);
    if (ok && tree)
    {
        std::string raw;
        ok = in.Take(blocks) && in.Take(raw) && (raw.size() == blocks * sizeof(FlatQBBoxBlock));
        if (ok)
        {
            std::unique_ptr<FlatMeshBBoxTree> t(new FlatMeshBBoxTree);
            t->blocks.resize(size_t(blocks));
            std::memcpy(t->blocks.data(), raw.data(), raw.size());
            ok = Valid_Tree(t->blocks, uint32_t(data.Number_Of_Triangles));
            data.FlatTree = t.release();
        }
    }
    ok = ok && in.Take(debug) && in.Take(warnings);
    for (uint32_t i = 0; ok && (i < warnings); ++i)
    {
        ok = in.Take(warning);
        state.warnings.push_back(warning);
    }
    if (!ok || (in.p != in.end))
    {
        state.warnings.push_back("Generated mesh cache '" + path + "' is corrupt; rebuilding it.");
        return nullptr;
    }
    mesh->has_inside_vector = (inside != 0);
    mesh->Type = patch ? (mesh->Type | PATCH_OBJECT) : (mesh->Type & ~PATCH_OBJECT);
    state.debug = debug.empty() ? debug : debug.substr(0, debug.find_last_not_of('\n') + 1) + " (cached)\n";
    return mesh;
}

void Write_Generated_Mesh(const GeneratedMeshKey& key, const Mesh& mesh, DeferredMeshState& state, size_t firstWarning)
{
    if (!key.Valid())
        return;
    const MESH_DATA& data = *mesh.Data;
    Packer out;
    out.bytes.append(GENERATED_MAGIC, sizeof(GENERATED_MAGIC));
    out.Put(GENERATED_VERSION);
    out.Put(Build_Hash());
    out.Put(key.a);
    out.Put(key.b);
    out.Put(uint8_t(mesh.has_inside_vector));
    out.Put(uint8_t((mesh.Type & PATCH_OBJECT) != 0));
    out.Put(mesh.BBox);
    out.Put(data.Inside_Vect);
    out.Put(data.Number_Of_Vertices);
    out.Put(data.Number_Of_Normals);
    out.Put(data.Number_Of_UVCoords);
    out.Put(data.Number_Of_Triangles);
    out.Put(data.Vertices, size_t(data.Number_Of_Vertices) * sizeof(MeshVector));
    out.Put(data.Normals, size_t(data.Number_Of_Normals) * sizeof(MeshVector));
    out.Put(data.UVCoords, size_t(data.Number_Of_UVCoords) * sizeof(MeshUVVector));
    out.Put(data.Triangles, size_t(data.Number_Of_Triangles) * sizeof(MESH_TRIANGLE));
    Pack_Column(out, data.NormalInd);
    Pack_Column(out, data.UVInd);
    Pack_Column(out, data.TextureInd);
    Pack_Column(out, data.Texture23Ind);
    out.Put(uint8_t(data.FlatTree != nullptr));
    if (data.FlatTree != nullptr)
    {
        out.Put(uint64_t(data.FlatTree->blocks.size()));
        out.Put(data.FlatTree->blocks.data(), data.FlatTree->blocks.size() * sizeof(FlatQBBoxBlock));
    }
    out.Put(state.debug.data(), state.debug.size());
    out.Put(uint32_t(state.warnings.size() - firstWarning));
    for (size_t i = firstWarning; i < state.warnings.size(); ++i)
        out.Put(state.warnings[i].data(), state.warnings[i].size());
    out.Put(Hash_Bytes(out.bytes.data(), out.bytes.size(), 0));

    const UCS2String temp = key.path + SysToUCS2String(".tmp");
    PlatformBase& platform = PlatformBase::GetInstance();
    const std::string path = UCS2toSysString(key.path);
    int fd = -1;
    if (platform.AllowLocalFileAccess(temp, POV_File_Data_POVM, true) && platform.AllowLocalFileAccess(key.path, POV_File_Data_POVM, true))
    {
        fd = Create_Exclusive(temp);
        FileStat st;
        if ((fd < 0) && (errno == EEXIST) && Stat(temp, st) && (std::difftime(std::time(nullptr), st.st_mtime) > STALE_TEMP_SECONDS))
        {
            Remove(temp);
            fd = Create_Exclusive(temp);
        }
        if ((fd < 0) && (errno == EEXIST))
            return;
    }
#if defined(_WIN32)
    FILE *file = (fd >= 0) ? _fdopen(fd, "wb") : nullptr;
#else
    FILE *file = (fd >= 0) ? fdopen(fd, "wb") : nullptr;
#endif
    FileStat mine;
    if ((file == nullptr) || !Stat(file, mine))
    {
        if (file != nullptr)
            std::fclose(file);
#if defined(_WIN32)
        else if (fd >= 0) _close(fd);
#else
        else if (fd >= 0) close(fd);
#endif
        if (fd >= 0)
            Remove(temp);
        state.warnings.push_back("Cannot write generated mesh cache '" + path + "'; the mesh is built for this render only.");
        return;
    }
    bool written = std::fwrite(out.bytes.data(), 1, out.bytes.size(), file) == out.bytes.size();
    written = (std::fclose(file) == 0) && written;
    if (!Same_File(temp, mine))
        return;
    if (!(written && Replace(temp, key.path)))
    {
        Remove(temp);
        state.warnings.push_back("Cannot write generated mesh cache '" + path + "'; the mesh is built for this render only.");
    }
}

void Parser::Parse_Povm(Mesh *mesh, PovmTreeKey& key)
{
    UCS2 *parsedName = Parse_String(true);
    const std::string name = UCS2toSysString(parsedName);
    UCS2String found;
    std::shared_ptr<IStream> file = Locate_File(parsedName, POV_File_Data_POVM, found, true);
    POV_FREE(parsedName);
    if (file == nullptr)
        Error("Cannot open povm file '%s'.", name.c_str());

    Vector3d insideVect(0.0, 0.0, 0.0);
    if (AllowToken(INSIDE_VECTOR_TOKEN))
        Parse_Vector(insideVect);
    const bool solid = !insideVect.IsNearNull(EPSILON);
    std::shared_ptr<DeferredMeshState> deferred = RecordMeshBuild(
        [file, name, found, insideVect](TraceThreadData *, DeferredMeshState& state) -> std::shared_ptr<Mesh>
        {
    std::shared_ptr<Mesh> result(new Mesh());
    Mesh *mesh = result.get();
    PovmTreeKey key;

    mesh->Data = new MESH_DATA();
    mesh->Data->References = 1;
    MESH_DATA& data = *mesh->Data;

    unsigned char header[POVM_HEADER_BYTES];
    if (!file->read(header, sizeof(header)) || (std::memcmp(header, POVM_MAGIC, sizeof(POVM_MAGIC)) != 0))
        throw PovmError("'%s' is not a povm file.", name.c_str());
    const uint32_t version = Word(header + 4), flags = Word(header + 8);
    const uint64_t vertices = Word(header + 12), normals = Word(header + 16), uvs = Word(header + 20), faces = Word(header + 24);
    if (version != POVM_VERSION)
        throw PovmError("povm file '%s' has version %u; this build reads version %u.", name.c_str(), version, POVM_VERSION);
    if ((flags & ~(POVM_NORMAL_INDICES | POVM_UV_INDICES)) != 0)
        throw PovmError("povm file '%s' has unknown flags 0x%x.", name.c_str(), flags);
    const bool normalIndices = (flags & POVM_NORMAL_INDICES) != 0, uvIndices = (flags & POVM_UV_INDICES) != 0;
    if ((vertices == 0) || (faces == 0))
        throw PovmError("povm file '%s' has no vertices or no faces.", name.c_str());
    if (vertices >= uint64_t(MESH_MAX_VERTICES))
        throw PovmError("Too many vertices in povm file '%s'.", name.c_str());
    if ((faces > uint64_t(std::numeric_limits<MeshIndex>::max()) / 3) || (normals > uint64_t(std::numeric_limits<MeshIndex>::max())) ||
        (uvs > uint64_t(std::numeric_limits<MeshIndex>::max())))
        throw PovmError("Too many faces, normals or uvs in povm file '%s'.", name.c_str());
    if (normalIndices ? (normals == 0) : ((normals != 0) && (normals != vertices)))
        throw PovmError("povm file '%s' needs normal indices unless it has one normal per vertex.", name.c_str());
    if (uvIndices ? (uvs == 0) : ((uvs != 0) && (uvs != vertices)))
        throw PovmError("povm file '%s' needs uv indices unless it has one uv per vertex.", name.c_str());

    const uint64_t expected = POVM_HEADER_BYTES + 12 * (vertices + normals) + 8 * uvs +
                              12 * faces * (1 + int(normalIndices) + int(uvIndices));
    if (!file->seekg(0, IOBase::seek_end))
        throw PovmError("Cannot read povm file '%s'.", name.c_str());
    const uint64_t bytes = uint64_t(file->tellg());
    if ((bytes != expected) || !file->seekg(POVM_HEADER_BYTES))
        throw PovmError("povm file '%s' has %llu bytes where its header needs %llu.", name.c_str(),
              (unsigned long long)bytes, (unsigned long long)expected);

    uint64_t hash = Hash_Bytes(header, sizeof(header), 0);
    const auto read = [&](void *dst, size_t words) {
        if (state.cancelled->load(std::memory_order_relaxed))
            throw PovmError("Mesh build cancelled.");
        if (!file->read(dst, words * 4))
            throw PovmError("Cannot read povm file '%s'.", name.c_str());
        hash = Hash_Bytes(dst, words * 4, hash);
        To_Host_Order(dst, words);
    };

    data.Number_Of_Vertices = MeshIndex(vertices);
    data.Vertices = reinterpret_cast<MeshVector *>(POV_MALLOC(vertices * sizeof(MeshVector), "triangle mesh data"));
    read(data.Vertices, 3 * vertices);

    if (normals > 0)
    {
        data.Number_Of_Normals = MeshIndex(normals);
        data.Normals = reinterpret_cast<MeshVector *>(POV_MALLOC(normals * sizeof(MeshVector), "triangle mesh data"));
        read(data.Normals, 3 * normals);
        if (!Finite(&data.Normals[0][X], 3 * normals))
            throw PovmError("Mesh normal is infinite or not a number in povm file '%s'.", name.c_str());
        bool foundZeroNormal = false;
        for (uint64_t i = 0; i < normals; ++i)
        {
            Vector3d n(data.Normals[i]);
            if ((fabs(n[X]) < EPSILON) && (fabs(n[Y]) < EPSILON) && (fabs(n[Z]) < EPSILON))
            {
                if (!foundZeroNormal)
                    state.warnings.push_back("Normal vector in povm file '" + name + "' cannot be zero - changing it to <1,0,0>.");
                foundZeroNormal = true;
                n[X] = 1.0;
            }
            if (fabs(n.length() - 1.0) > 1.0e-4)
                data.Normals[i] = MeshVector(n.normalized());
            else
                data.Normals[i] = MeshVector(n);
        }
    }

    data.Number_Of_UVCoords = MeshIndex(uvs > 0 ? uvs : 1);
    data.UVCoords = reinterpret_cast<MeshUVVector *>(POV_MALLOC(data.Number_Of_UVCoords * sizeof(MeshUVVector), "triangle mesh data"));
    data.UVCoords[0] = MeshUVVector(0.0, 0.0);
    std::vector<float> chunk;
    for (uint64_t done = 0; done < uvs; )
    {
        const uint64_t n = std::min<uint64_t>(uvs - done, 1 << 16);
        chunk.resize(2 * n);
        read(chunk.data(), 2 * n);
        if (!Finite(chunk.data(), 2 * n))
            throw PovmError("Mesh uv is infinite or not a number in povm file '%s'.", name.c_str());
        for (uint64_t i = 0; i < n; ++i)
            data.UVCoords[done + i] = MeshUVVector(chunk[2 * i], chunk[2 * i + 1]);
        done += n;
    }
    std::vector<float>().swap(chunk);

    data.Number_Of_Triangles = MeshIndex(faces);
    data.Triangles = reinterpret_cast<MESH_TRIANGLE *>(POV_MALLOC(faces * sizeof(MESH_TRIANGLE), "triangle mesh data"));
    read(data.Triangles, 3 * faces);
    for (uint64_t i = 0; i < faces; ++i)
        if (std::max({ data.Triangles[i].Word[0], data.Triangles[i].Word[1], data.Triangles[i].Word[2] }) >= vertices)
            throw PovmError("Mesh face index out of range in povm file '%s'.", name.c_str());

    const auto readColumn = [&](MeshIndexColumn& column, uint64_t count, const char *what) {
        column.values.resize(3 * faces);
        read(column.values.data(), 3 * faces);
        for (const MeshIndex v : column.values)
            if (uint32_t(v) >= count)
                throw PovmError("Mesh %s index out of range in povm file '%s'.", what, name.c_str());
    };
    if (normalIndices)
        readColumn(data.NormalInd, normals, "normal");
    else if (normals > 0)
        data.NormalInd.byVertex = true;
    if (uvIndices)
        readColumn(data.UVInd, uvs, "uv");
    else if (uvs > 0)
        data.UVInd.byVertex = true;

    key.bytes = bytes;
    key.hash = hash;
    key.path = found;
    const size_t dot = key.path.find_last_of('.');
    if ((dot != UCS2String::npos) && (key.path.find_first_of(SysToUCS2String("/\\:"), dot) == UCS2String::npos))
        key.path.erase(dot);
    key.path += SysToUCS2String(".povt");

    for (MeshIndex i = 0; i < data.Number_Of_Triangles; ++i)
    {
        if (((i & 0xFFFF) == 0) && state.cancelled->load(std::memory_order_relaxed))
            throw PovmError("Mesh build cancelled.");
        MESH_TRIANGLE& t = data.Triangles[i];
        const Vector3d P1(data.Vertices[t.P1()]), P2(data.Vertices[t.P2()]), P3(data.Vertices[t.P3()]);
        bool smooth = false;
        if (normals > 0)
        {
            const Vector3d N1(data.Normals[data.NormalInd.Get(t, i, 0)]);
            const Vector3d N2(data.Normals[data.NormalInd.Get(t, i, 1)]);
            const Vector3d N3(data.Normals[data.NormalInd.Get(t, i, 2)]);
            smooth = ((N1 - N2).lengthSqr() > EPSILON) || ((N1 - N3).lengthSqr() > EPSILON);
        }
        mesh->Compute_Mesh_Triangle(&t, i, smooth, P1, P2, P3);
    }

    if (insideVect.IsNearNull(EPSILON))
    {
        mesh->has_inside_vector = false;
        mesh->Type |= PATCH_OBJECT;
    }
    else
    {
        data.Inside_Vect = insideVect.normalized();
        mesh->has_inside_vector = true;
        mesh->Type &= ~PATCH_OBJECT;
    }

    mesh->Textures = nullptr;
    mesh->Number_Of_Textures = 0;
    mesh->Finish_Mesh_Data();
    if (!mesh->Vertices_Finite())
        throw PovmError("Mesh vertex is infinite or not a number.");
    mesh->Compute_BBox();

    const uint32_t triangleCount = uint32_t(data.Number_Of_Triangles);
    const std::string treePath = UCS2toSysString(key.path);
    const TreeRead cached = Read_Tree(key, triangleCount, data.FlatTree);
    if (cached == TreeRead::Corrupt)
        state.warnings.push_back("Mesh tree cache '" + treePath + "' is corrupt; rebuilding it.");
    if (cached != TreeRead::Loaded)
    {
        mesh->Build_Mesh_BBox_Tree();
        switch (Write_Tree(key, triangleCount, *data.FlatTree))
        {
            case TreeWrite::Written:
            case TreeWrite::Busy:
                break;
            case TreeWrite::Held:
                state.warnings.push_back("Mesh tree cache '" + treePath + ".tmp' exists, so another render is writing the cache; delete it if no render is running.");
                break;
            case TreeWrite::Invalid:
                state.warnings.push_back("The tree built for '" + treePath + "' failed its own checks and is not cached.");
                break;
            case TreeWrite::Failed:
                state.warnings.push_back("Cannot write mesh tree cache '" + treePath + "'; the tree is built for this render only.");
                break;
        }
    }
    return result;
        });

    Make_BBox(mesh->BBox, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    mesh->has_inside_vector = solid;
    if (solid)
        mesh->Type &= ~PATCH_OBJECT;
    mesh->SetDeferred(deferred);
    key.path.clear();
}

void Parser::Povm_Mesh_Tree(Mesh *mesh, const PovmTreeKey& key)
{
    if (!Test_Flag(mesh, HIERARCHY_FLAG))
        return;
    MESH_DATA& data = *mesh->Data;
    const uint32_t triangles = uint32_t(data.Number_Of_Triangles);
    const std::string path = UCS2toSysString(key.path);

    delete data.FlatTree;
    data.FlatTree = nullptr;
    const TreeRead cached = Read_Tree(key, triangles, data.FlatTree);
    if (cached == TreeRead::Loaded)
        return;
    if (cached == TreeRead::Corrupt)
        Warning("Mesh tree cache '%s' is corrupt; rebuilding it.", path.c_str());

    mesh->Build_Mesh_BBox_Tree();
    switch (Write_Tree(key, triangles, *data.FlatTree))
    {
        case TreeWrite::Written:
        case TreeWrite::Busy:
            break;
        case TreeWrite::Held:
            Warning("Mesh tree cache '%s.tmp' exists, so another render is writing the cache; delete it if no render is running.",
                    path.c_str());
            break;
        case TreeWrite::Invalid:
            POV_ASSERT(false);
            Warning("The tree built for '%s' failed its own checks and is not cached.", path.c_str());
            break;
        case TreeWrite::Failed:
            Warning("Cannot write mesh tree cache '%s'; the tree is built for this render only.", path.c_str());
            break;
    }
}

}
// end of namespace pov_parser
