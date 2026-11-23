// prepared_cache.cpp: a saved cache's grid is cut into bricks that each read alone; a load gives back what was built; a
// changed key rebuilds and leaves the other key's file; a damaged section is rejected while the others still read; two
// writers of one cache never share a temporary file; stale temporary files are swept; the size cap evicts the least
// recently used file.
#include "core/support/preparedcache.h"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <utime.h>
#include <iostream>
#include <string>
#include <vector>
using namespace pov;

static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "prepared_cache: " << what << std::endl;
        ++failures;
    }
}

int main()
{
    const std::string dir = "prepared_cache_test_dir";
    const std::array<std::size_t, 3> counts = {{ 70, 40, 33 }};
    const Vector3d low(-1.0, 0.5, 2.0), cell(0.1, 0.2, 0.05);
    std::vector<double> built(counts[0] * counts[1] * counts[2]);
    for (std::size_t i = 0; i < built.size(); ++i)
        built[i] = 0.25 * double(i) + 1.0 / double(i + 3);
    std::uint32_t header = 7;

    // build and save: a header record, then the grid as bricks of kCacheBrick cells
    std::string path;
    {
        PreparedCaches caches(dir, 11, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        Check(!unit.Loading(), "a new cache was loaded");
        unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        unit.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        unit.Finish();
        path = unit.Path();
    }

    // one section, read alone by its id, holds exactly its cells of the build
    {
        PreparedCacheFile file(path);
        Check(!file.Missing(), "the saved file is missing");
        const std::size_t bricks = 3 * 2 * 2;
        Check(file.Entries().size() == 1 + bricks, "the file does not hold a header and twelve bricks");
        const std::uint64_t id = (std::uint64_t(0) << 32) | ((1 * 2 + 1) * 3 + 2);  // brick x 2, y 1, z 1
        const PreparedCacheFile::Entry& entry = file.Entries()[file.Find(id)];
        Check((entry.section.first[0] == 64) && (entry.section.first[1] == 32) && (entry.section.first[2] == 32), "the brick starts elsewhere");
        Check((entry.section.cells[0] == 6) && (entry.section.cells[1] == 8) && (entry.section.cells[2] == 1), "the brick holds other cells");
        Check((entry.section.low[X] == low[X] + 64 * cell[X]) && (entry.section.high[Z] == low[Z] + 33 * cell[Z]), "the brick's bounds are wrong");
        std::vector<double> part;
        ReadBrick(file.Read(file.Find(id)), entry.section, counts, part);
        bool same = true, untouched = true;
        for (std::size_t z = 0; z < counts[2]; ++z)
            for (std::size_t y = 0; y < counts[1]; ++y)
                for (std::size_t x = 0; x < counts[0]; ++x)
                {
                    const std::size_t i = (z * counts[1] + y) * counts[0] + x;
                    const bool inside = (x >= 64) && (y >= 32) && (z >= 32);
                    same = same && (!inside || (part[i] == built[i]));
                    untouched = untouched && (inside || (part[i] == 0.0));
                }
        Check(same, "a section read alone differs from the build");
        Check(untouched, "reading one section filled cells outside it");
    }

    // a load gives back the whole build
    {
        PreparedCaches caches(dir, 11, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        Check(unit.Loading(), "a saved cache was not loaded");
        std::uint32_t loadedHeader = 0;
        std::vector<double> loaded;
        unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, loadedHeader); });
        unit.Grid("cells", loaded, counts, LatticeBounds(low, cell, false));
        unit.Finish();
        Check((loadedHeader == header) && (loaded == built), "a load differs from the build");
    }

    // a damaged section fails its own checksum; the others still read, and a whole load rejects the file and rebuilds
    {
        std::FILE *f = std::fopen(path.c_str(), "r+b");
        PreparedCacheFile before(path);
        const std::uint64_t at = before.Entries()[1].offset + 3;
        std::fseek(f, long(at), SEEK_SET);
        const int byte = std::fgetc(f);
        std::fseek(f, long(at), SEEK_SET);
        std::fputc(byte ^ 0xff, f);
        std::fclose(f);
        PreparedCacheFile file(path);
        bool rejected = false;
        try { file.Read(1); } catch (const CacheFormatError&) { rejected = true; }
        Check(rejected, "a damaged section was read");
        bool readable = true;
        try { file.Read(2); } catch (const CacheFormatError&) { readable = false; }
        Check(readable, "a sound section was refused beside a damaged one");
        PreparedCaches caches(dir, 11, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        Check(!unit.Loading(), "a damaged file was loaded");
        unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        unit.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        unit.Finish();
    }

    const auto exists = [](const std::string& file) { struct stat info; return stat(file.c_str(), &info) == 0; };
    const auto age = [](const std::string& file, long seconds) {
        struct utimbuf times;
        times.actime = times.modtime = std::time(nullptr) - seconds;
        utime(file.c_str(), &times);
    };
    const auto save = [&](PreparedCaches& caches) {
        PreparedCacheUnit unit(&caches, "grid", 5);
        unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        unit.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        unit.Finish();
        return unit.Path();
    };

    // another key is another file, and the first key's file stays for the job that uses it
    std::string other;
    {
        PreparedCaches caches(dir, 12, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        Check(!unit.Loading(), "a cache under another key was loaded");
        unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        unit.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        unit.Finish();
        other = unit.Path();
        Check(other != path, "two keys share a file name");
        Check(exists(path), "a build under another key removed the first key's file");
    }
    {
        PreparedCaches caches(dir, 11, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        Check(unit.Loading(), "the first key's cache no longer loads after another key was built");
    }
    std::remove(path.c_str());
    std::remove(other.c_str());

    // two writers of one cache, as two containers sharing a directory, interleave without sharing a temporary file
    {
        PreparedCaches first(dir, 13, false), second(dir, 13, false);
        PreparedCacheUnit a(&first, "grid", 5), b(&second, "grid", 5);
        Check(!a.Loading() && !b.Loading() && (a.Path() == b.Path()), "the concurrent writers do not write the same cache");
        a.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        b.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, header); });
        a.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        b.Grid("cells", built, counts, LatticeBounds(low, cell, false));
        a.Finish();
        b.Finish();
        Check((first.Built() == 1) && (second.Built() == 1), "a concurrent writer failed to save");
        PreparedCaches caches(dir, 13, false);
        PreparedCacheUnit unit(&caches, "grid", 5);
        std::uint32_t loadedHeader = 0;
        std::vector<double> loaded;
        Check(unit.Loading(), "concurrent writers left no loadable file");
        if (unit.Loading())
        {
            unit.Record("header", []() {}, [&](CacheArchive& archive) { Serialize(archive, loadedHeader); });
            unit.Grid("cells", loaded, counts, LatticeBounds(low, cell, false));
            unit.Finish();
        }
        Check((loadedHeader == header) && (loaded == built), "concurrent writers left a file that differs from the build");
        std::remove(unit.Path().c_str());
    }

    // a temporary file a day old is swept when the directory is opened; a fresh one may be a live writer's and stays
    {
        const std::string stale = dir + "/grid0-0-0.povc.tmp-1-stale", fresh = dir + "/grid0-0-0.povc.tmp-2-fresh";
        for (const std::string& file : { stale, fresh })
            if (std::FILE *f = std::fopen(file.c_str(), "wb"))
                std::fclose(f);
        age(stale, PreparedCaches::kStaleTempSeconds + 60);
        PreparedCaches caches(dir, 14, false);
        Check(!exists(stale), "a day-old temporary file was not swept");
        Check(exists(fresh), "a fresh temporary file was swept");
        std::remove(fresh.c_str());
    }

    // over the size cap the least recently used file goes, never one this render uses; a load counts as a use
    {
        PreparedCaches one(dir, 21, false);
        const std::string oldest = save(one);
        struct stat info;
        stat(oldest.c_str(), &info);
        const std::uint64_t bytes = std::uint64_t(info.st_size);
        PreparedCaches two(dir, 22, false);
        const std::string idle = save(two);
        age(oldest, 300);
        age(idle, 200);
        {
            PreparedCaches again(dir, 21, false);
            PreparedCacheUnit unit(&again, "grid", 5);
            Check(unit.Loading(), "a file under the cap did not load");
        }
        PreparedCaches three(dir, 23, false, bytes * 2 + bytes / 2);
        const std::string newest = save(three);
        Check(exists(oldest), "a file loaded since was evicted before an unused one");
        Check(!exists(idle), "the least recently used file was kept over the cap");
        Check(exists(newest), "the file this render saved was evicted");
        for (const std::string& file : { oldest, idle, newest })
            std::remove(file.c_str());
    }
    std::remove(dir.c_str());
    if (failures == 0)
        std::cout << "prepared cache: a brick reads alone and matches the build; a load matches; damage and key changes rebuild; "
                     "other keys stay; concurrent writers, stale temporary files and the size cap are handled" << std::endl;
    return failures == 0 ? 0 : 1;
}
