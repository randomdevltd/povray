//******************************************************************************
///
/// @file core/support/preparedcache.h
///
/// Prepared caches saved to disk, so a later render of the same scene loads them instead of building them.
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

#ifndef POVRAY_CORE_PREPAREDCACHE_H
#define POVRAY_CORE_PREPAREDCACHE_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ standard header files
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// POV-Ray header files (base module)
#include "base/colour.h"

// POV-Ray header files (core module)
#include "core/math/vector.h"

namespace pov_base
{
class GenericMessenger;
}

namespace pov
{

using pov_base::ColourChannel;
using pov_base::MathColour;

/// A 64-bit hash of `n` bytes in the manner of xxHash64, chained through `seed`.
std::uint64_t ContentHash(const void *data, std::size_t n, std::uint64_t seed);
/// Differs between any two builds: the GNU build ID where there is one, else a hash of the executable.
std::uint64_t BuildHash();

/// Chains values into a cache key.
class CacheKey final
{
    public:
        explicit CacheKey(std::uint64_t seed = 0) : hash(seed) {}
        template<class T> typename std::enable_if<std::is_arithmetic<T>::value, CacheKey&>::type Add(T value)
            { hash = ContentHash(&value, sizeof(value), hash); return *this; }
        CacheKey& Add(const std::string& text) { Add(std::uint64_t(text.size())); hash = ContentHash(text.data(), text.size(), hash); return *this; }
        CacheKey& Add(const Vector3d& v) { return Add(v[X]).Add(v[Y]).Add(v[Z]); }
        std::uint64_t Value() const { return hash; }
    private:
        std::uint64_t hash;
};

/// A record that cannot be read as the preparation reading it expects.
struct CacheFormatError final : std::runtime_error
{
    explicit CacheFormatError(const std::string& what) : std::runtime_error(what) {}
};

/// One record's bytes, written or read field by field, so padding never reaches a file.
class CacheArchive final
{
    public:
        explicit CacheArchive(std::string& sink) : out(&sink), in(nullptr), end(nullptr) {}
        CacheArchive(const char *begin, const char *finish) : out(nullptr), in(begin), end(finish) {}
        bool Reading() const { return out == nullptr; }
        void Raw(void *data, std::size_t n)
        {
            if (out != nullptr)
            {
                out->append(static_cast<const char *>(data), n);
                return;
            }
            if (std::size_t(end - in) < n)
                throw CacheFormatError("the record ends early");
            std::memcpy(data, in, n);
            in += n;
        }
        /// A count read back, refused when the rest of the record cannot hold that many of `bytes` each.
        std::uint64_t Count(std::uint64_t count, std::size_t bytes)
        {
            Raw(&count, sizeof(count));
            if (Reading() && (count > std::uint64_t(end - in) / std::max<std::size_t>(bytes, 1)))
                throw CacheFormatError("a count runs past the record");
            return count;
        }
        bool Done() const { return (out != nullptr) || (in == end); }
    private:
        std::string *out;
        const char *in, *end;
};

template<class T>
inline typename std::enable_if<std::is_arithmetic<T>::value>::type Serialize(CacheArchive& a, T& value) { a.Raw(&value, sizeof(value)); }

inline void Serialize(CacheArchive& a, bool& value)
{
    unsigned char byte = value ? 1 : 0;
    a.Raw(&byte, 1);
    if (byte > 1)
        throw CacheFormatError("a flag is neither 0 nor 1");
    value = (byte != 0);
}

inline void Serialize(CacheArchive& a, Vector3d& v)
{
    for (int axis = X; axis <= Z; ++axis)
        Serialize(a, v[axis]);
}

inline void Serialize(CacheArchive& a, MathColour& c)
{
    for (int channel = 0; channel < MathColour::channels; ++channel)
        Serialize(a, c[channel]);
}

inline void Serialize(CacheArchive& a, std::string& text)
{
    const std::uint64_t n = a.Count(text.size(), 1);
    text.resize(std::size_t(n));
    if (n > 0)
        a.Raw(&text[0], std::size_t(n));
}

/// Element types written as one block of bytes: no padding, nothing to check per element.
template<class T> struct CacheBlock : std::integral_constant<bool, std::is_arithmetic<T>::value && !std::is_same<T, bool>::value> {};
template<> struct CacheBlock<Vector3d> : std::integral_constant<bool, sizeof(Vector3d) == 3 * sizeof(double)> {};
template<> struct CacheBlock<MathColour> : std::integral_constant<bool, sizeof(MathColour) == MathColour::channels * sizeof(ColourChannel)> {};

template<class T>
inline void Serialize(CacheArchive& a, std::vector<T>& values)
{
    const std::uint64_t n = a.Count(values.size(), CacheBlock<T>::value ? sizeof(T) : 1);
    if (a.Reading())
        values.resize(std::size_t(n));
    if (CacheBlock<T>::value)
    {
        if (n > 0)
            a.Raw(static_cast<void *>(values.data()), std::size_t(n) * sizeof(T));
    }
    else
        for (T& value : values)
            Serialize(a, value);
}

template<class T, std::size_t N>
inline void Serialize(CacheArchive& a, std::array<T, N>& values)
{
    for (T& value : values)
        Serialize(a, value);
}

/// What tells a file's state without reading it: a write changes its size, a time or, through a rename, its inode.
struct InputStamp final
{
    std::uint64_t size = 0, modified = 0, changed = 0, inode = 0, device = 0;
    bool operator==(const InputStamp& other) const
        { return (size == other.size) && (modified == other.modified) && (changed == other.changed) && (inode == other.inode) && (device == other.device); }
    bool operator!=(const InputStamp& other) const { return !(*this == other); }
};

/// False when the file cannot be examined.
bool StampFile(const std::string& path, InputStamp& stamp);
/// Hashes a file's contents and stamps it; false when it cannot be read or changed while it was read.
bool HashFile(const std::string& path, std::uint64_t& hash, InputStamp& stamp);

/// A file the parse read: the name the scene gave, where it was found, its contents' hash and its stamp at that time.
struct CacheInput final
{
    std::string referenced, path;
    std::uint64_t hash = 0;
    InputStamp stamp;
};

/// Records, under a key known before the parse, the scene key a parse gave and the inputs it read.
bool WriteSceneManifest(const std::string& directory, std::uint64_t preKey, std::uint64_t sceneKey, const std::vector<CacheInput>& inputs);
/// The scene key and inputs recorded under `preKey`; false when there is none or it is damaged.
bool ReadSceneManifest(const std::string& directory, std::uint64_t preKey, std::uint64_t& sceneKey, std::vector<CacheInput>& inputs);

/// Whether the directory holds a saved cache of `kind` built under `sceneKey`.
bool HasSavedCaches(const std::string& directory, const std::string& kind, std::uint64_t sceneKey);

/// Where a section lies: a brick's cells and world-space bounds, so a region reads only the bricks it touches; none for a whole record.
struct CacheSection final
{
    /// which grid of the file (high 32 bits) and which brick of it, in z, y, x order with x fastest; kWhole for no grid
    std::uint64_t id = kWhole;
    std::array<std::uint32_t, 3> first = {{ 0, 0, 0 }}, cells = {{ 0, 0, 0 }};
    Vector3d low, high;
    static constexpr std::uint64_t kWhole = ~std::uint64_t(0);
    bool operator==(const CacheSection& other) const
        { return (id == other.id) && (first == other.first) && (cells == other.cells); }
};

/// The edge of a grid's bricks, in cells.
const std::size_t kCacheBrick = 32;

/// The world-space bounds of a grid's cells [first, end) along each axis.
using CacheGridBounds = std::function<void(const std::array<std::size_t, 3>& first, const std::array<std::size_t, 3>& end,
                                           Vector3d& low, Vector3d& high)>;

/// Bounds of an axis-aligned lattice whose index i lies at low + i * step: its nodes, or its cells, which span a step each.
CacheGridBounds LatticeBounds(const Vector3d& low, const Vector3d& step, bool nodes);

/// A saved cache file: its header and index are checked on opening, then any section reads and checks alone.
class PreparedCacheFile final
{
    public:
        struct Entry final
        {
            std::uint64_t offset, length, hash;
            std::string label;
            CacheSection section;
        };
        /// Reads and checks the header and index only; throws CacheFormatError naming what is wrong; a missing file is reported by Missing().
        explicit PreparedCacheFile(const std::string& path);
        ~PreparedCacheFile();
        PreparedCacheFile(const PreparedCacheFile&) = delete;
        PreparedCacheFile& operator=(const PreparedCacheFile&) = delete;
        bool Missing() const { return !open; }
        std::uint64_t Build() const { return build; }
        std::uint64_t Key() const { return key; }
        const std::string& Name() const { return name; }
        std::uint64_t Bytes() const { return bytes; }
        const std::vector<Entry>& Entries() const { return entries; }
        /// The index of the section with this id, or of the first whole record labelled so; throws CacheFormatError if none.
        std::size_t Find(std::uint64_t id) const;
        /// One section's bytes, read alone; throws CacheFormatError if they do not match their checksum.
        std::string Read(std::size_t index) const;
        /// Checks every section's checksum, holding one section at a time; throws CacheFormatError at the first damaged one.
        void Check() const;
    private:
        bool open = false;
#if defined(_WIN32)
        std::FILE *file = nullptr;
        mutable std::mutex reading;
#else
        int file = -1;
#endif
        bool ReadAt(std::uint64_t offset, void *data, std::size_t n) const;
        std::uint64_t build = 0, key = 0, bytes = 0;
        std::string name;
        std::vector<Entry> entries;
};

/// Reads one brick written by PreparedCacheUnit::Grid into `values`, a grid of `counts` cells.
template<class T>
void ReadBrick(const std::string& bytes, const CacheSection& section, const std::array<std::size_t, 3>& counts, std::vector<T>& values);

class PreparedCacheUnit;

/// A step of `unit`, or just `build` where there is no unit.
template<class Build, class Fields>
inline void CachedStep(PreparedCacheUnit *unit, const char *label, Build&& build, Fields&& fields);

/// The saved caches of one render: where they live, the key of the scene they were built from, and what was done with each.
class PreparedCaches final
{
    public:
        /// `sceneKey` covers every input the preparation reads; `verify` rebuilds every loaded cache and compares the two;
        /// `maxBytes` caps the directory, least recently used files going first (0 for no cap). Removes stale temporary files.
        PreparedCaches(const std::string& directory, std::uint64_t sceneKey, bool verify, std::uint64_t maxBytes = kDefaultMaxBytes);
        const std::string& Directory() const { return directory; }
        std::uint64_t SceneKey() const { return sceneKey; }
        bool Verify() const { return verify; }
        void SetMessenger(pov_base::GenericMessenger *messenger) { messages = messenger; }
        /// One line totalling what every cache did.
        void Summarise();
        unsigned int Built() const { return built; }
        unsigned int Loaded() const { return loaded; }
        /// Removes the least recently used cache files until the directory is within its cap, never one this render uses.
        void Trim();
        /// Cache_Max_Size's default, 20 GiB.
        static constexpr std::uint64_t kDefaultMaxBytes = std::uint64_t(20) << 30;
        /// A temporary file older than this is left from a render that stopped, and is removed.
        static constexpr int kStaleTempSeconds = 24 * 60 * 60;
    private:
        friend class PreparedCacheUnit;
        std::string directory;
        std::uint64_t sceneKey;
        bool verify;
        std::uint64_t maxBytes;
        pov_base::GenericMessenger *messages = nullptr;
        std::mutex lock;
        std::map<std::string, unsigned int> ordinals;
        /// the files this render loaded or saved, which Trim keeps
        std::set<std::string> used;
        unsigned int loaded = 0, built = 0, verified = 0, trimmed = 0;
        std::uint64_t loadedBytes = 0, builtBytes = 0, trimmedBytes = 0;
        unsigned int Next(const std::string& kind);
        void Use(const std::string& path);
        void TrimLocked();
        void Info(const std::string& line);
        void Warn(const std::string& line);
};

/// One saved cache: sections read from its file when the key matches, else built and saved, in the same order either way.
class PreparedCacheUnit final
{
    public:
        /// No `caches` saves nothing; the store numbers each `kind` in preparation order, and `detail` joins the key.
        PreparedCacheUnit(PreparedCaches *caches, const std::string& kind, std::uint64_t detail);
        PreparedCacheUnit(const PreparedCacheUnit&) = delete;
        PreparedCacheUnit& operator=(const PreparedCacheUnit&) = delete;
        ~PreparedCacheUnit();

        /// Whether records come from the saved file, so `build` is not run.
        bool Loading() const { return mode == Mode::Load; }
        /// The file it reads or writes.
        const std::string& Path() const { return path; }

        /// One step: `build` computes it unless the file holds it; `fields` writes the result or reads it back.
        template<class Build, class Fields>
        void Record(const char *label, Build&& build, Fields&& fields, const CacheSection *section = nullptr)
        {
            if (mode == Mode::Off)
            {
                build();
                return;
            }
            if (mode == Mode::Load)
            {
                Load(label, [&](CacheArchive& archive) { fields(archive); }, section);
                return;
            }
            build();
            std::string bytes;
            CacheArchive archive(bytes);
            fields(archive);
            Store(label, std::move(bytes), section);
        }

        /// Statistics text: saved and loaded, but not compared by Cache_Verify, as it may vary with the thread count.
        void Note(const char *label, std::string& text);

        /// A grid of `counts` cells, x fastest, built already unless loading, saved or read as bricks: one section each.
        template<class T>
        void Grid(const char *label, std::vector<T>& values, const std::array<std::size_t, 3>& counts, const CacheGridBounds& bounds);

        /// Saves a built cache, checks a loaded one was read to its end, and logs what was done.
        void Finish();
    private:
        enum class Mode { Off, Build, Load, Verify };
        PreparedCaches *caches;
        Mode mode = Mode::Off;
        std::string name, path, reason;
        std::uint64_t key = 0;
        /// a loaded file, whose sections are read one at a time as the preparation asks for them
        std::unique_ptr<PreparedCacheFile> file;
        std::vector<PreparedCacheFile::Entry> saved;
        std::uint64_t fileBytes = 0;
        std::size_t next = 0;
        std::uint32_t grids = 0;
        std::chrono::steady_clock::time_point start;
        bool finished = false, failed = false;
        /// a build writes each section to `temp` as it comes, then renames it into place
        std::FILE *out = nullptr;
        std::string temp;
        std::uint64_t written = 0;

        void Open();
        bool Read(std::string& problem);
        void Load(const char *label, const std::function<void(CacheArchive&)>& fields, const CacheSection *section);
        void Store(const char *label, std::string&& bytes, const CacheSection *section);
        bool Write();
        [[noreturn]] void Mismatch(const std::string& what);
};

/// A row of a brick: one block of bytes where the element type has no padding, else element by element.
template<class T>
inline void SerializeRow(CacheArchive& archive, T *row, std::size_t count)
{
    if (CacheBlock<T>::value)
        archive.Raw(static_cast<void *>(row), count * sizeof(T));
    else
        for (std::size_t i = 0; i < count; ++i)
            Serialize(archive, row[i]);
}

template<class T>
inline void SerializeBrick(CacheArchive& archive, const CacheSection& section, const std::array<std::size_t, 3>& counts, std::vector<T>& values)
{
    for (std::size_t z = section.first[2]; z < section.first[2] + section.cells[2]; ++z)
        for (std::size_t y = section.first[1]; y < section.first[1] + section.cells[1]; ++y)
            SerializeRow(archive, &values[(z * counts[1] + y) * counts[0] + section.first[0]], section.cells[0]);
}

template<class T>
void ReadBrick(const std::string& bytes, const CacheSection& section, const std::array<std::size_t, 3>& counts, std::vector<T>& values)
{
    for (int axis = 0; axis < 3; ++axis)
        if (std::size_t(section.first[axis]) + section.cells[axis] > counts[axis])
            throw CacheFormatError("a brick lies outside its grid");
    values.resize(counts[0] * counts[1] * counts[2]);
    CacheArchive archive(bytes.data(), bytes.data() + bytes.size());
    SerializeBrick(archive, section, counts, values);
    if (!archive.Done())
        throw CacheFormatError("a brick is longer than its cells");
}

template<class T>
void PreparedCacheUnit::Grid(const char *label, std::vector<T>& values, const std::array<std::size_t, 3>& counts, const CacheGridBounds& bounds)
{
    if (mode == Mode::Off)
        return;
    const std::size_t total = counts[0] * counts[1] * counts[2];
    if (mode == Mode::Load)
        values.resize(total);
    else if (values.size() != total)
        throw std::logic_error(std::string("a cache grid's values do not match its cells: ") + label);
    const std::uint64_t grid = grids++;
    std::array<std::size_t, 3> bricks;
    for (int axis = 0; axis < 3; ++axis)
        bricks[axis] = (counts[axis] + kCacheBrick - 1) / kCacheBrick;
    std::uint64_t brick = 0;
    for (std::size_t bz = 0; bz < bricks[2]; ++bz)
        for (std::size_t by = 0; by < bricks[1]; ++by)
            for (std::size_t bx = 0; bx < bricks[0]; ++bx, ++brick)
            {
                CacheSection section;
                section.id = (grid << 32) | brick;
                const std::array<std::size_t, 3> at = {{ bx, by, bz }};
                std::array<std::size_t, 3> first, end;
                for (int axis = 0; axis < 3; ++axis)
                {
                    first[axis] = at[axis] * kCacheBrick;
                    end[axis] = std::min(counts[axis], first[axis] + kCacheBrick);
                    section.first[axis] = std::uint32_t(first[axis]);
                    section.cells[axis] = std::uint32_t(end[axis] - first[axis]);
                }
                bounds(first, end, section.low, section.high);
                Record(label, []() {}, [&](CacheArchive& archive) { SerializeBrick(archive, section, counts, values); }, &section);
            }
}

template<class Build, class Fields>
inline void CachedStep(PreparedCacheUnit *unit, const char *label, Build&& build, Fields&& fields)
{
    if (unit != nullptr)
        unit->Record(label, build, fields);
    else
        build();
}

}
// end of namespace pov

#endif // POVRAY_CORE_PREPAREDCACHE_H
