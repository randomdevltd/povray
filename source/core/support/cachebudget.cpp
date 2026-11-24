//******************************************************************************
///
/// @file core/support/cachebudget.cpp
///
/// One memory budget for a render's large caches, spilling to disk beyond it.
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
#include "core/support/cachebudget.h"

// C++ variants of C standard header files
#include <cmath>
#include <cstdio>
#include <cstdlib>

// C++ standard header files
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>

// POSIX and platform header files
#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

// POV-Ray header files (base module)
#include "base/pov_err.h"

// POV-Ray header files (core module)
#include "core/support/preparedcache.h"

// this must be the last file included
#include "base/povdebug.h"

namespace pov
{

namespace
{

/// Regions this large get pages of their own, which can be mapped in place; smaller ones stay on the heap, counted.
const std::size_t kRegionPages = std::size_t(1) << 20;
/// Mapped pages are dropped this many at a time, from where the last drop ended.
const std::size_t kTrimChunk = std::size_t(1) << 20;
/// How often the watcher measures the mapped pages in memory.
const std::chrono::milliseconds kWatchPeriod(25);

std::size_t PageBytes()
{
#if defined(_WIN32)
    return 4096;
#else
    static const std::size_t page = std::size_t(sysconf(_SC_PAGESIZE));
    return page;
#endif
}

std::string MiB(double bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MiB", bytes / 1048576.0);
    return text;
}

/// The file pages the process holds in memory, from the system; 0 where it cannot be read.
std::uint64_t FilePages()
{
#if defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t size = 0, resident = 0, shared = 0;
    if (statm >> size >> resident >> shared)
        return shared * PageBytes();
#endif
    return 0;
}

}
// end of anonymous namespace

CacheRegion::~CacheRegion()
{
    Release();
}

void *CacheRegion::Allocate(std::size_t n, const std::shared_ptr<CacheBudget>& owner, CacheAccount *owned)
{
    Release();
    budget = owner;
    account = owned;
    bytes = n;
#if !defined(_WIN32)
    if ((budget != nullptr) && (n >= kRegionPages))
    {
        length = (n + PageBytes() - 1) / PageBytes() * PageBytes();
        void *at = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (at == MAP_FAILED)
            throw std::bad_alloc();
        data = at;
        pages = true;
    }
#endif
    if (data == nullptr)
    {
        length = n;
        data = ::operator new(std::max<std::size_t>(n, 1));
        pages = false;
    }
    if ((budget != nullptr) && (account != nullptr))
        budget->Charge(*account, std::int64_t(length));
    return data;
}

void CacheRegion::Adopt(const void *source, std::size_t n, const std::shared_ptr<CacheBudget>& owner, CacheAccount *owned, const char *name)
{
    Release();
    label = name;
#if !defined(_WIN32)
    if ((owner != nullptr) && (n >= kRegionPages) && (owner->Resident() + n > owner->Bytes()))
    {
        budget = owner;
        account = owned;
        bytes = n;
        length = (n + PageBytes() - 1) / PageBytes() * PageBytes();
        if (budget->MapCopy(*this, source))
            return;
        budget.reset();
        account = nullptr;
    }
#endif
    std::memcpy(Allocate(n, owner, owned), source, n);
}

void CacheRegion::Seal(const char *name)
{
    label = name;
    if ((budget != nullptr) && pages)
        budget->Seal(*this);
}

void CacheRegion::Release()
{
    if (data == nullptr)
        return;
    bool wasMapped = false;
    if (budget != nullptr)
    {
        budget->Unregister(*this);
        wasMapped = mapped.load(std::memory_order_acquire);
        if (!wasMapped && (account != nullptr))
            budget->Charge(*account, -std::int64_t(length));
    }
#if !defined(_WIN32)
    if (pages)
        munmap(data, length);
    else
#endif
        ::operator delete(data);
    data = nullptr;
    bytes = length = 0;
    mapped.store(false, std::memory_order_release);
    budget.reset();
    account = nullptr;
}

CacheBudget::CacheBudget(std::uint64_t bytes, const std::string& spillDirectory, bool keep) :
    budget(bytes),
    directory(spillDirectory),
    keepSpill(keep)
{
    baselineFilePages = FilePages();
}

CacheBudget::~CacheBudget()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
    }
    wake.notify_all();
    if (watcher.joinable())
        watcher.join();
    if ((spill != nullptr) && keepSpill)
        spill->Finish();
}

CacheAccount& CacheBudget::Account(const std::string& name, bool pinned)
{
    std::lock_guard<std::mutex> guard(lock);
    for (CacheAccount& account : accounts)
        if (account.name == name)
            return account;
    accounts.emplace_back();
    accounts.back().name = name;
    accounts.back().pinned = pinned;
    return accounts.back();
}

std::uint64_t CacheBudget::Resident() const
{
    const std::int64_t total = counted.load(std::memory_order_relaxed) + std::int64_t(mappedResident.load(std::memory_order_relaxed));
    return std::uint64_t(std::max<std::int64_t>(total, 0));
}

void CacheBudget::Note(std::int64_t total)
{
    const std::uint64_t now = std::uint64_t(std::max<std::int64_t>(total, 0));
    std::uint64_t peak = peakTotal.load(std::memory_order_relaxed);
    while ((now > peak) && !peakTotal.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {}
}

void CacheBudget::Charge(CacheAccount& account, std::int64_t bytes)
{
    if (bytes == 0)
        return;
    const std::int64_t held = account.resident.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::int64_t peak = account.peak.load(std::memory_order_relaxed);
    while ((held > peak) && !account.peak.compare_exchange_weak(peak, held, std::memory_order_relaxed)) {}
    const std::int64_t total = counted.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    Note(total + std::int64_t(mappedResident.load(std::memory_order_relaxed)));
    if ((bytes > 0) && (Resident() > budget))
        MakeRoom();
}

void CacheBudget::MakeRoom()
{
    // sealed regions are mapped from the file first, which loses nothing as their pages come back on demand; then blocks
    // are evicted, least recently used first; then mapped pages are dropped
    while (Resident() > budget)
    {
        if (MapOne() || EvictOne())
            continue;
        const std::uint64_t over = Resident() - budget, mapped = mappedResident.load(std::memory_order_relaxed);
        if (mapped > 0)
            TrimMapped(std::min(over, mapped));
        break;
    }
    // what remains is pinned or in use this moment: the overshoot is counted in the peak
}

void CacheBudget::Admit(SpillBlock& block, CacheAccount& account)
{
    std::size_t held;
    {
        std::lock_guard<std::mutex> blockGuard(block.blockMutex);
        block.account = &account;
        held = block.HeldBytes();
        block.charged = held;
        block.resident.store(true, std::memory_order_release);
        block.referenced.store(true, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        block.slot = clock.size();
        clock.push_back(&block);
    }
    Charge(account, std::int64_t(held));
}

void CacheBudget::Forget(SpillBlock& block)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        if (block.slot != ~std::size_t(0))
        {
            clock[block.slot] = clock.back();
            clock[block.slot]->slot = block.slot;
            clock.pop_back();
            block.slot = ~std::size_t(0);
            if (hand >= clock.size())
                hand = 0;
        }
    }
    std::size_t freed = 0;
    CacheAccount *account;
    {
        // an eviction in progress holds the block's mutex, so this waits for it
        std::lock_guard<std::mutex> blockGuard(block.blockMutex);
        account = block.account;
        if (block.resident.load(std::memory_order_acquire))
            freed = block.charged;
        block.charged = 0;
        block.account = nullptr;
    }
    if ((account != nullptr) && (freed > 0))
        Charge(*account, -std::int64_t(freed));
}

bool CacheBudget::EvictOne()
{
    SpillBlock *victim = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        // the clock: a block used since the hand last passed gets a second chance
        for (std::size_t visited = 0, n = clock.size(); (visited < 2 * n) && !clock.empty(); ++visited)
        {
            if (hand >= clock.size())
                hand = 0;
            SpillBlock *block = clock[hand];
            if (block->referenced.load(std::memory_order_relaxed))
            {
                block->referenced.store(false, std::memory_order_relaxed);
                ++hand;
                continue;
            }
            if (!block->blockMutex.try_lock())
            {
                ++hand;
                continue;
            }
            int none = 0;
            if (!block->pins.compare_exchange_strong(none, SpillBlock::kEvicting, std::memory_order_acquire))
            {
                block->blockMutex.unlock();
                ++hand;
                continue;
            }
            clock[hand] = clock.back();
            clock[hand]->slot = hand;
            clock.pop_back();
            block->slot = ~std::size_t(0);
            victim = block;
            break;
        }
    }
    if (victim == nullptr)
        return false;
    // the victim's mutex is held and its pins are closed: no reader can see it until it is evicted
    CacheAccount& account = *victim->account;
    std::size_t freed = victim->charged;
    try
    {
        if (victim->section == ~std::size_t(0))
        {
            // written once: a block never changes after it is complete, so a later eviction only frees it
            const std::string bytes = victim->SpillBytes();
            victim->section = Spill().Append(account.name.c_str(), bytes.data(), bytes.size());
            spillUsed.store(true, std::memory_order_relaxed);
            account.spillWrites.fetch_add(1, std::memory_order_relaxed);
            account.spillWriteBytes.fetch_add(bytes.size(), std::memory_order_relaxed);
        }
        victim->Drop();
    }
    catch (...)
    {
        victim->pins.fetch_sub(SpillBlock::kEvicting, std::memory_order_release);
        victim->blockMutex.unlock();
        {
            // it stays in memory, still counted, and can be tried again
            std::lock_guard<std::mutex> guard(lock);
            victim->slot = clock.size();
            clock.push_back(victim);
        }
        try { throw; }
        catch (const pov_base::Exception&) { throw; }
        catch (const std::exception& error)
        {
            throw POV_EXCEPTION_STRING(("Max_Cache_Memory: the " + account.name + " cannot spill to disk: " + error.what() + ".").c_str());
        }
    }
    victim->resident.store(false, std::memory_order_relaxed);
    victim->charged = 0;
    account.evictions.fetch_add(1, std::memory_order_relaxed);
    victim->pins.fetch_sub(SpillBlock::kEvicting, std::memory_order_release);
    victim->blockMutex.unlock();
    Charge(account, -std::int64_t(freed));
    return true;
}

void CacheBudget::Fault(SpillBlock& block)
{
    std::size_t held = 0;
    CacheAccount *account;
    {
        std::unique_lock<std::mutex> blockGuard(block.blockMutex);
        account = block.account;
        if (!block.resident.load(std::memory_order_acquire))
        {
            const PreparedCacheFile::Entry entry = Spill().Entry(block.section);
            std::string bytes(std::size_t(entry.length), '\0');
            try
            {
                Spill().Read(block.section, bytes.empty() ? nullptr : &bytes[0]);
                block.Restore(bytes);
            }
            catch (const CacheFormatError& error)
            {
                throw POV_EXCEPTION_STRING(("Max_Cache_Memory: a spilled cache could not be read back: " + std::string(error.what()) + ".").c_str());
            }
            held = block.HeldBytes();
            block.charged = held;
            if (account != nullptr)
            {
                account->spillReads.fetch_add(1, std::memory_order_relaxed);
                account->spillReadBytes.fetch_add(bytes.size(), std::memory_order_relaxed);
            }
            block.resident.store(true, std::memory_order_release);
        }
        // no eviction can start while the mutex is held, so the pin cannot meet one
        block.pins.fetch_add(1, std::memory_order_acquire);
        block.referenced.store(true, std::memory_order_relaxed);
    }
    if (held == 0)
        return;
    {
        std::lock_guard<std::mutex> guard(lock);
        if ((block.slot == ~std::size_t(0)) && (account != nullptr))
        {
            block.slot = clock.size();
            clock.push_back(&block);
        }
    }
    if (account != nullptr)
        Charge(*account, std::int64_t(held));
}

void CacheBudget::Seal(CacheRegion& region)
{
    {
        std::lock_guard<std::mutex> guard(lock);
        region.slot = regions.size();
        regions.push_back(&region);
    }
    if (Resident() > budget)
        MakeRoom();
}

void CacheBudget::Unregister(CacheRegion& region)
{
    std::lock_guard<std::mutex> guard(lock);
    for (std::vector<CacheRegion *> *list : { &regions, &mappings })
    {
        auto found = std::find(list->begin(), list->end(), &region);
        if (found != list->end())
            list->erase(found);
    }
    region.slot = ~std::size_t(0);
}

bool CacheBudget::MapCopy(CacheRegion& region, const void *source)
{
#if defined(_WIN32)
    (void)region; (void)source;
    return false;
#else
    std::lock_guard<std::mutex> guard(lock);
    CacheSpillFile& file = Spill();
    if (mappings.empty())
        baselineFilePages = FilePages();
    const std::size_t index = file.Append(region.label.c_str(), source, region.bytes);
    const std::uint64_t offset = file.Entry(index).offset;
    if (offset % PageBytes() != 0)
        return false;
    void *at = mmap(nullptr, region.length, PROT_READ | PROT_WRITE, MAP_PRIVATE, file.Descriptor(), off_t(offset));
    if (at == MAP_FAILED)
        return false;
    region.data = at;
    region.pages = true;
    region.mapped.store(true, std::memory_order_release);
    mappings.push_back(&region);
    spillUsed.store(true, std::memory_order_relaxed);
    if (region.account != nullptr)
    {
        region.account->spillWrites.fetch_add(1, std::memory_order_relaxed);
        region.account->spillWriteBytes.fetch_add(region.bytes, std::memory_order_relaxed);
    }
    if (!watcher.joinable())
        watcher = std::thread([this]() { Watch(); });
    return true;
#endif
}

bool CacheBudget::MapOne()
{
#if defined(_WIN32)
    return false;
#else
    CacheRegion *region = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (regions.empty())
            return false;
        // the oldest sealed region first: it is written once and read from the file's pages after
        region = regions.front();
        regions.erase(regions.begin());
        // under the lock, so that the region is not released while it is written and mapped
        CacheSpillFile& file = Spill();
        // the file pages held before the first mapping are the program's own, not the caches'
        if (mappings.empty())
            baselineFilePages = FilePages();
        const std::size_t index = file.Append(region->label.c_str(), region->data, region->length);
        const std::uint64_t offset = file.Entry(index).offset;
        void *at = mmap(region->data, region->length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, file.Descriptor(), off_t(offset));
        if (at != region->data)
            throw POV_EXCEPTION_STRING(("Max_Cache_Memory: the " + region->label + " could not be mapped from the spill file " +
                                        file.Path() + " (" + std::strerror(errno) + ").").c_str());
        spillUsed.store(true, std::memory_order_relaxed);
        region->mapped.store(true, std::memory_order_release);
        mappings.push_back(region);
        if (!watcher.joinable())
            watcher = std::thread([this]() { Watch(); });
    }
    if (region->account != nullptr)
    {
        region->account->spillWrites.fetch_add(1, std::memory_order_relaxed);
        region->account->spillWriteBytes.fetch_add(region->length, std::memory_order_relaxed);
        region->account->evictions.fetch_add(1, std::memory_order_relaxed);
        Charge(*region->account, -std::int64_t(region->length));
    }
    return true;
#endif
}

std::uint64_t CacheBudget::TrimMapped(std::uint64_t want)
{
#if defined(_WIN32)
    (void)want;
    return 0;
#else
    std::uint64_t dropped = 0;
    {
        std::lock_guard<std::mutex> guard(lock);
        std::uint64_t total = 0;
        for (const CacheRegion *region : mappings)
            total += region->length;
        // round the mapped regions from where the last trim stopped, a chunk at a time
        while ((dropped < want) && (dropped < total) && !mappings.empty())
        {
            if (mappingHand >= mappings.size())
                mappingHand = 0;
            CacheRegion& region = *mappings[mappingHand];
            const std::size_t length = std::min(kTrimChunk, region.length - region.trimmed);
            madvise(static_cast<char *>(region.data) + region.trimmed, length, MADV_DONTNEED);
            dropped += length;
            region.trimmed += length;
            if (region.trimmed >= region.length)
            {
                region.trimmed = 0;
                ++mappingHand;
            }
        }
    }
    MeasureMapped();
    return dropped;
#endif
}

std::uint64_t CacheBudget::MeasureMapped()
{
    // the file pages the process holds past those it held when the budget began: the mapped regions' pages in memory
    const std::uint64_t now = FilePages();
    const std::uint64_t mapped = (now > baselineFilePages) ? now - baselineFilePages : 0;
    mappedResident.store(mapped, std::memory_order_relaxed);
    Note(counted.load(std::memory_order_relaxed) + std::int64_t(mapped));
    return mapped;
}

void CacheBudget::Watch()
{
    std::unique_lock<std::mutex> guard(lock);
    while (!stopping)
    {
        wake.wait_for(guard, kWatchPeriod);
        if (stopping)
            break;
        guard.unlock();
        const std::uint64_t mapped = MeasureMapped();
        if ((mapped > 0) && (Resident() > budget))
            TrimMapped(std::min(Resident() - budget, mapped));
        guard.lock();
    }
}

CacheSpillFile& CacheBudget::Spill()
{
    std::lock_guard<std::mutex> guard(spillLock);
    if (spill == nullptr)
    {
        try
        {
            spill.reset(new CacheSpillFile(directory, "spill", keepSpill));
        }
        catch (const std::exception& error)
        {
            throw POV_EXCEPTION_STRING((std::string("Max_Cache_Memory: caches cannot spill to disk: ") + error.what() + ".").c_str());
        }
    }
    return *spill;
}

void CacheBudget::CheckPinned() const
{
    std::uint64_t pinned = 0;
    std::ostringstream list;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (const CacheAccount& account : accounts)
        {
            const std::int64_t held = account.resident.load(std::memory_order_relaxed);
            if (!account.pinned || (held <= 0))
                continue;
            pinned += std::uint64_t(held);
            list << (pinned == std::uint64_t(held) ? "" : ", ") << account.name << " " << MiB(double(held));
        }
    }
    if (pinned > budget)
        throw POV_EXCEPTION_STRING(("Max_Cache_Memory is " + MiB(double(budget)) + ", but the caches that cannot be divided and spilled "
                                    "need " + MiB(double(pinned)) + " on their own: " + list.str() + ". Raise Max_Cache_Memory.").c_str());
}

std::vector<std::string> CacheBudget::Report() const
{
    std::vector<std::string> lines;
    std::lock_guard<std::mutex> guard(lock);
    for (const CacheAccount& account : accounts)
    {
        const std::int64_t peak = account.peak.load(std::memory_order_relaxed);
        if ((peak <= 0) && (account.spillWrites.load() == 0))
            continue;
        std::string line = "Cache memory: " + account.name + (account.pinned ? " (pinned)" : "") + ": peak " + MiB(double(peak)) + " resident";
        if (account.spillWrites.load() > 0)
        {
            char text[256];
            std::snprintf(text, sizeof(text), "; spilled %s in %llu writes, read back %s in %llu reads, %llu evictions",
                          MiB(double(account.spillWriteBytes.load())).c_str(), (unsigned long long)account.spillWrites.load(),
                          MiB(double(account.spillReadBytes.load())).c_str(), (unsigned long long)account.spillReads.load(),
                          (unsigned long long)account.evictions.load());
            line += text;
        }
        lines.push_back(line + ".");
    }
    const std::uint64_t process = ProcessPeak();
    std::string total = "Cache memory: budget " + MiB(double(budget)) + " (Max_Cache_Memory); peak " + MiB(double(Peak())) + " of caches resident";
    if (spill != nullptr)
        total += "; spill file " + MiB(double(spill->Bytes())) + " in " + directory;
    if (process > 0)
        total += "; process peak " + MiB(double(process));
    lines.push_back(total + ".");
    return lines;
}

std::uint64_t CacheBudget::ProcessPeak()
{
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string field;
    while (status >> field)
    {
        if (field == "VmHWM:")
        {
            std::uint64_t kilobytes = 0;
            status >> kilobytes;
            return kilobytes * 1024;
        }
        status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
#endif
    return 0;
}

}
// end of namespace pov
