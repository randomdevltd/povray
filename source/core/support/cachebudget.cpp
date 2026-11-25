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
#if defined(__GLIBC__)
#include <malloc.h>
#endif

// C++ standard header files
#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>

// POSIX and platform header files
#if !defined(_WIN32)
#include <fcntl.h>
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
const std::chrono::milliseconds kWatchPeriod(10);

// regions and their spill sections are aligned to this; page tables are read in the system's own pages
std::size_t PageBytes() { return CachePageBytes(); }

std::string MiB(double bytes)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MiB", bytes / 1048576.0);
    return text;
}

#if !defined(_WIN32)
/// The pages of [at, at + n) in this process's page tables: its own mapping's share of its resident memory.
std::uint64_t ResidentBytes(const void *at, std::size_t n)
{
    const std::size_t page = SystemPageBytes();
    const std::uintptr_t first = reinterpret_cast<std::uintptr_t>(at) / page;
    const std::size_t pages = (n + page - 1) / page;
    std::uint64_t present = 0;
#if defined(__linux__)
    // a page's entry has bit 63 set while it is mapped in memory; readable by the process without privileges
    static const int pagemap = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    if (pagemap >= 0)
    {
        std::uint64_t entries[4096];
        bool ok = true;
        for (std::size_t done = 0; ok && (done < pages); )
        {
            const std::size_t want = std::min<std::size_t>(pages - done, sizeof(entries) / sizeof(entries[0]));
            const ssize_t got = pread(pagemap, entries, want * sizeof(entries[0]), off_t((first + done) * sizeof(entries[0])));
            ok = (got == ssize_t(want * sizeof(entries[0])));
            for (std::size_t i = 0; ok && (i < want); ++i)
                present += (entries[i] >> 63) & 1;
            done += want;
        }
        if (ok)
            return present * page;
        present = 0;
    }
#endif
    // elsewhere the page cache's copy counts as well, which can only overstate
    std::vector<unsigned char> in(pages);
#if defined(__APPLE__)
    char *vector = reinterpret_cast<char *>(in.data());
#else
    unsigned char *vector = in.data();
#endif
    if (mincore(reinterpret_cast<void *>(first * page), n, vector) != 0)
        return n;
    for (unsigned char bit : in)
        present += bit & 1;
    return present * page;
}
#endif

}
// end of anonymous namespace

CacheRegion::~CacheRegion()
{
    Release();
}

void *CacheRegion::Pages(std::size_t n)
{
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
    return data;
}

void *CacheRegion::Allocate(std::size_t n, const std::shared_ptr<CacheBudget>& owner, CacheAccount *owned)
{
    Release();
    budget = owner;
    account = owned;
    Pages(n);
    if ((budget != nullptr) && (account != nullptr))
    {
        // in its own pages it is built in place, unspillable until sealed
        if (pages)
        {
            building = true;
            budget->Building(std::int64_t(length));
        }
        else
        {
            // too small for pages of its own, it can never be spilled
            heap = true;
            budget->HeapRegion(std::int64_t(length));
        }
        budget->Charge(*account, std::int64_t(length));
    }
    return data;
}

void CacheRegion::Adopt(const void *source, std::size_t n, const std::shared_ptr<CacheBudget>& owner, CacheAccount *owned, const char *name)
{
    Release();
    label = name;
#if !defined(_WIN32)
    if ((owner != nullptr) && (owned != nullptr) && (n >= kRegionPages))
    {
        const std::int64_t whole = std::int64_t((n + PageBytes() - 1) / PageBytes() * PageBytes());
        budget = owner;
        account = owned;
        // a copy holds the data twice for a moment: both copies are reserved at once, so threads sealing side by side
        // cannot each find room for one and together pass the budget
        if (owner->Reserve(*owned, 2 * whole))
        {
            std::memcpy(Pages(n), source, n);
            building = true;
            owner->Building(whole);
            // the source's share: the caller frees it as this returns
            owner->Charge(*owned, -whole);
            return;
        }
        // else the file holds it from the start
        bytes = n;
        length = std::size_t(whole);
        if (owner->MapCopy(*this, source))
            return;
        budget.reset();
        account = nullptr;
        bytes = length = 0;
    }
#endif
    std::memcpy(Allocate(n, owner, owned), source, n);
}

void CacheRegion::Seal(const char *name)
{
    label = name;
    if (building)
    {
        building = false;
        budget->Building(-std::int64_t(length));
    }
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
        // waits for a spill of it under way
        budget->Unregister(*this);
        wasMapped = mapped.load(std::memory_order_acquire);
        if (!wasMapped && (account != nullptr))
            budget->Charge(*account, -std::int64_t(length));
        if (building)
            budget->Building(-std::int64_t(length));
        if (heap)
            budget->HeapRegion(-std::int64_t(length));
    }
    building = false;
    heap = false;
#if !defined(_WIN32)
    if (pages)
        munmap(data, length);
    else
#endif
        ::operator delete(data);
    // unmapped first, so its disk space can be given back
    if ((budget != nullptr) && (section != ~std::size_t(0)))
        budget->FreeSection(section);
    section = ~std::size_t(0);
    data = nullptr;
    bytes = length = 0;
    mapped.store(false, std::memory_order_release);
    budget.reset();
    account = nullptr;
}

CacheBudget::CacheBudget(std::uint64_t bytes, const std::string& spillDirectory, bool keep, std::uint64_t cap, unsigned threadCount) :
    budget(bytes),
    directory(spillDirectory),
    keepSpill(keep),
    spillCap(cap),
    threads(std::max(threadCount, 1u))
{
#if defined(__GLIBC__)
    // large arrays come from and go back to the system directly, so a spilled cell's freed memory leaves the process
    // instead of staying in a thread's heap arena
    mallopt(M_MMAP_THRESHOLD, int(kRegionPages / 4));
#endif
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
    Count(account, bytes);
    if ((bytes > 0) && (Resident() > budget))
        MakeRoom();
}

void CacheBudget::Count(CacheAccount& account, std::int64_t bytes)
{
    if (account.pinned)
        Unspillable(bytes);
    const std::int64_t held = account.resident.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::int64_t peak = account.peak.load(std::memory_order_relaxed);
    while ((held > peak) && !account.peak.compare_exchange_weak(peak, held, std::memory_order_relaxed)) {}
    const std::int64_t total = counted.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::uint64_t most = peakHeld.load(std::memory_order_relaxed);
    while ((total > std::int64_t(most)) && !peakHeld.compare_exchange_weak(most, std::uint64_t(total), std::memory_order_relaxed)) {}
    Note(total + std::int64_t(mappedResident.load(std::memory_order_relaxed)));
}

void CacheBudget::Unspillable(std::int64_t bytes)
{
    const std::int64_t now = unspillable.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::uint64_t most = peakUnspillable.load(std::memory_order_relaxed);
    while ((now > std::int64_t(most)) && !peakUnspillable.compare_exchange_weak(most, std::uint64_t(now), std::memory_order_relaxed)) {}
}

void CacheBudget::HeapRegion(std::int64_t bytes)
{
    heapRegions.fetch_add(bytes, std::memory_order_relaxed);
    Unspillable(bytes);
}

bool CacheBudget::Reserve(CacheAccount& account, std::int64_t bytes)
{
    std::int64_t now = counted.load(std::memory_order_relaxed);
    do
    {
        if (std::uint64_t(std::max<std::int64_t>(now + std::int64_t(mappedResident.load(std::memory_order_relaxed)), 0)) + std::uint64_t(bytes) > budget)
            return false;
    }
    while (!counted.compare_exchange_weak(now, now + bytes, std::memory_order_relaxed));
    if (account.pinned)
        Unspillable(bytes);
    const std::int64_t held = account.resident.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::int64_t peak = account.peak.load(std::memory_order_relaxed);
    while ((held > peak) && !account.peak.compare_exchange_weak(peak, held, std::memory_order_relaxed)) {}
    std::uint64_t most = peakHeld.load(std::memory_order_relaxed);
    while ((now + bytes > std::int64_t(most)) && !peakHeld.compare_exchange_weak(most, std::uint64_t(now + bytes), std::memory_order_relaxed)) {}
    Note(now + bytes + std::int64_t(mappedResident.load(std::memory_order_relaxed)));
    return true;
}

void CacheBudget::MakeRoom()
{
    // mapped pages are dropped first, which loses nothing as they come back from the file on demand; then sealed regions are
    // mapped from the file; then blocks are evicted, least recently used first
    bool trimmed = false;
    while (Resident() > budget)
    {
        const std::uint64_t mapped = mappedResident.load(std::memory_order_relaxed);
        if (!trimmed && (mapped > 0))
        {
            trimmed = true;
            if (TrimMapped(std::min(Resident() - budget, mapped)) > 0)
                continue;
        }
        if (MapOne() || EvictOne())
            continue;
        break;
    }
    // what remains is pinned, being built or in use this moment, which the peak records
}

void CacheBudget::NoteCell(std::uint64_t bytes)
{
    std::uint64_t largest = largestCell.load(std::memory_order_relaxed);
    while ((bytes > largest) && !largestCell.compare_exchange_weak(largest, bytes, std::memory_order_relaxed)) {}
}

void CacheBudget::Building(std::int64_t bytes)
{
    const std::int64_t now = building.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::uint64_t most = peakBuilding.load(std::memory_order_relaxed);
    while ((now > std::int64_t(most)) && !peakBuilding.compare_exchange_weak(most, std::uint64_t(now), std::memory_order_relaxed)) {}
}

std::size_t CacheBudget::SpillSection(const std::string& label, const void *data, std::size_t n)
{
    CacheSpillFile& file = Spill();
    try
    {
        const std::size_t index = file.Append(label.c_str(), data, n);
        spillUsed.store(true, std::memory_order_relaxed);
        return index;
    }
    catch (const CacheSpillFull& error)
    {
        throw POV_EXCEPTION(pov_base::kFileDataErr, ("Max_Cache_Memory: the " + label + " cannot spill to disk: " + error.what() +
                                           ". Raise Max_Cache_Memory or Cache_Max_Size.").c_str());
    }
    catch (const std::exception& error)
    {
        throw POV_EXCEPTION(pov_base::kFileDataErr, ("Max_Cache_Memory: the " + label + " cannot spill to disk: " + error.what() + ".").c_str());
    }
}

void CacheBudget::FreeSection(std::size_t section) noexcept
{
    std::lock_guard<std::mutex> guard(spillLock);
    if (spill != nullptr)
        spill->Free(section);
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
    // a hot cell may never spill, so its resident size is what bounds what a gather holds
    NoteCell(held);
    {
        std::lock_guard<std::mutex> guard(lock);
        block.slot = clock.size();
        clock.push_back(&block);
    }
    Charge(account, std::int64_t(held));
}

void CacheBudget::Forget(SpillBlock& block)
{
    std::size_t freed = 0, section;
    CacheAccount *account;
    {
        // an eviction in progress holds the block's mutex, so this waits for it; one that failed has put it back on the clock
        std::lock_guard<std::mutex> blockGuard(block.blockMutex);
        {
            // under the clock's lock a block's mutex is only ever tried, never waited for, so this order cannot deadlock
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
        account = block.account;
        if (block.resident.load(std::memory_order_acquire))
            freed = block.charged;
        block.charged = 0;
        block.account = nullptr;
        section = block.section;
        block.section = ~std::size_t(0);
    }
    if ((account != nullptr) && (freed > 0))
        Charge(*account, -std::int64_t(freed));
    // its copy on disk is no longer needed
    if (section != ~std::size_t(0))
        FreeSection(section);
}

bool CacheBudget::EvictOne()
{
    SpillBlock *victim = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (advised.load(std::memory_order_relaxed))
        {
            // the region schedule knows each block's next cluster: the one needed latest goes first, if it can go now
            std::size_t best = clock.size();
            std::uint32_t latest = 1;
            for (std::size_t i = 0; i < clock.size(); ++i)
            {
                const std::uint32_t rank = clock[i]->advice.load(std::memory_order_relaxed);
                if ((rank > latest) && (clock[i]->pins.load(std::memory_order_relaxed) == 0))
                {
                    latest = rank;
                    best = i;
                }
            }
            if (best < clock.size())
            {
                SpillBlock *block = clock[best];
                int none = 0;
                if (block->blockMutex.try_lock())
                {
                    if (block->pins.compare_exchange_strong(none, SpillBlock::kEvicting, std::memory_order_acquire))
                    {
                        clock[best] = clock.back();
                        clock[best]->slot = best;
                        clock.pop_back();
                        block->slot = ~std::size_t(0);
                        if (hand >= clock.size())
                            hand = 0;
                        victim = block;
                    }
                    else
                        block->blockMutex.unlock();
                }
            }
        }
        // the clock: a block used since the hand last passed gets a second chance
        for (std::size_t visited = 0, n = clock.size(); (victim == nullptr) && (visited < 2 * n) && !clock.empty(); ++visited)
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
    // the copy written to the spill file is held beside the block until it is written: counted from before it is made, at
    // the block's size, then at its own, without making room again
    std::int64_t copy = 0;
    try
    {
        if (victim->section == ~std::size_t(0))
        {
            // written once: a block never changes after it is complete, so a later eviction only frees it
            copy = std::int64_t(freed);
            Count(account, copy);
            const std::string bytes = victim->SpillBytes();
            Count(account, std::int64_t(bytes.size()) - copy);
            copy = std::int64_t(bytes.size());
            NoteCell(bytes.size());
            victim->section = SpillSection(account.name, bytes.data(), bytes.size());
            account.spillWrites.fetch_add(1, std::memory_order_relaxed);
            account.spillWriteBytes.fetch_add(bytes.size(), std::memory_order_relaxed);
        }
        Count(account, -copy);
        copy = 0;
        victim->Drop();
    }
    catch (...)
    {
        if (copy != 0)
            Count(account, -copy);
        {
            // it stays in memory, still counted, and can be tried again: back on the clock before anyone can forget it
            std::lock_guard<std::mutex> guard(lock);
            victim->slot = clock.size();
            clock.push_back(victim);
        }
        victim->pins.fetch_sub(SpillBlock::kEvicting, std::memory_order_release);
        victim->blockMutex.unlock();
        try { throw; }
        catch (const pov_base::Exception&) { throw; }
        catch (const std::exception& error)
        {
            throw POV_EXCEPTION(pov_base::kFileDataErr, ("Max_Cache_Memory: the " + account.name + " cannot spill to disk: " + error.what() + ".").c_str());
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
    std::int64_t transient = 0;
    CacheAccount *account;
    {
        std::unique_lock<std::mutex> blockGuard(block.blockMutex);
        account = block.account;
        if (!block.resident.load(std::memory_order_acquire))
        {
            const PreparedCacheFile::Entry entry = Spill().Entry(block.section);
            // the section's bytes and the arrays they fill are both held while it is read back: counted before, so the
            // budget makes room for them
            transient = 2 * std::int64_t(entry.length);
            NoteCell(entry.length);
            std::uint64_t read = 0;
            try
            {
                // counted first, so room is made for it; a spill failing meanwhile takes the count back with it
                if (account != nullptr)
                    Charge(*account, transient);
                std::string bytes(std::size_t(entry.length), '\0');
                Spill().Read(block.section, bytes.empty() ? nullptr : &bytes[0]);
                block.Restore(bytes);
                read = bytes.size();
            }
            catch (...)
            {
                if (account != nullptr)
                    Count(*account, -transient);
                try { throw; }
                catch (const CacheFormatError& error)
                {
                    throw POV_EXCEPTION(pov_base::kFileDataErr, ("Max_Cache_Memory: a spilled cache could not be read back: " + std::string(error.what()) + ".").c_str());
                }
            }
            held = block.HeldBytes();
            NoteCell(held);
            block.charged = held;
            if (account != nullptr)
            {
                account->spillReads.fetch_add(1, std::memory_order_relaxed);
                account->spillReadBytes.fetch_add(read, std::memory_order_relaxed);
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
    if (account == nullptr)
        return;
    try
    {
        Charge(*account, std::int64_t(held) - transient);
    }
    catch (...)
    {
        // the bytes are held and stay counted; the pin is the caller's only once this returns
        block.Unpin();
        throw;
    }
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
    std::unique_lock<std::mutex> guard(lock);
    // a region being written to the spill file goes only once it is written and mapped
    regionIdle.wait(guard, [&region]() { return !region.busy; });
    for (std::vector<CacheRegion *> *list : { &regions, &mappings })
    {
        auto found = std::find(list->begin(), list->end(), &region);
        if (found != list->end())
            list->erase(found);
    }
    region.slot = ~std::size_t(0);
}

void CacheBudget::StartWatcher()
{
    // under the lock
    if (!watcher.joinable())
        watcher = std::thread([this]() { Watch(); });
}

bool CacheBudget::MapCopy(CacheRegion& region, const void *source)
{
#if defined(_WIN32)
    (void)region; (void)source;
    return false;
#else
    // no other thread knows the region yet, so it is written without the budget's lock
    const std::size_t index = SpillSection(region.label, source, region.bytes);
    CacheSpillFile& file = Spill();
    const std::uint64_t offset = file.Entry(index).offset;
    // sections of a page or more start on the cache page, a multiple of the system's
    void *at = (offset % SystemPageBytes() == 0) ? mmap(nullptr, region.length, PROT_READ | PROT_WRITE, MAP_PRIVATE, file.Descriptor(), off_t(offset))
                                                 : MAP_FAILED;
    if (at == MAP_FAILED)
    {
        FreeSection(index);
        return false;
    }
    // read where the render looks, so the kernel reads no pages ahead that the budget did not ask for
    madvise(at, region.length, MADV_RANDOM);
    region.data = at;
    region.pages = true;
    region.section = index;
    region.mapped.store(true, std::memory_order_release);
    if (region.account != nullptr)
    {
        region.account->spillWrites.fetch_add(1, std::memory_order_relaxed);
        region.account->spillWriteBytes.fetch_add(region.bytes, std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> guard(lock);
    mappings.push_back(&region);
    StartWatcher();
    return true;
#endif
}

bool CacheBudget::MapOne()
{
#if defined(_WIN32)
    return false;
#else
    CacheRegion *region = nullptr;
    std::string label;
    void *data;
    std::size_t length;
    CacheAccount *account;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (regions.empty())
            return false;
        // the oldest sealed region first: it is written once and read from the file's pages after
        region = regions.front();
        regions.erase(regions.begin());
        // busy, it cannot be released while it is written and mapped without the lock; what is needed is copied now
        region->busy = true;
        label = region->label;
        data = region->data;
        length = region->length;
        account = region->account;
    }
    auto restore = [this, region]()
    {
        std::lock_guard<std::mutex> guard(lock);
        region->busy = false;
        regions.insert(regions.begin(), region);
        regionIdle.notify_all();
    };
    std::size_t index;
    try
    {
        index = SpillSection(label, data, length);
    }
    catch (...)
    {
        restore();
        throw;
    }
    CacheSpillFile& file = Spill();
    const std::uint64_t offset = file.Entry(index).offset;
    std::string reason;
    void *at = MAP_FAILED;
    if (offset % SystemPageBytes() != 0)
        reason = "its section starts at " + std::to_string(offset) + ", off the " + std::to_string(SystemPageBytes()) + "-byte page";
    else
    {
        at = mmap(data, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, file.Descriptor(), off_t(offset));
        // taken at once, before anything else can set it
        const int error = errno;
        if (at == MAP_FAILED)
            reason = std::strerror(error);
        else if (at != data)
            reason = "mapped elsewhere";
    }
    if (at != data)
    {
        FreeSection(index);
        restore();
        throw POV_EXCEPTION(pov_base::kFileDataErr, ("Max_Cache_Memory: the " + label + " could not be mapped from the spill file " + file.Path() +
                                           " (" + reason + ").").c_str());
    }
    madvise(data, length, MADV_RANDOM);
    {
        std::lock_guard<std::mutex> guard(lock);
        region->section = index;
        region->mapped.store(true, std::memory_order_release);
        mappings.push_back(region);
        region->busy = false;
        StartWatcher();
    }
    regionIdle.notify_all();
    // the region may be gone from here on: only the copies are used
    if (account != nullptr)
    {
        account->spillWrites.fetch_add(1, std::memory_order_relaxed);
        account->spillWriteBytes.fetch_add(length, std::memory_order_relaxed);
        account->evictions.fetch_add(1, std::memory_order_relaxed);
        Charge(*account, -std::int64_t(length));
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
        // round the mapped regions from where the last trim stopped, a chunk at a time, at most once round; only the pages
        // that were in memory count as dropped
        for (std::uint64_t visited = 0; (dropped < want) && (visited < total) && !mappings.empty(); )
        {
            if (mappingHand >= mappings.size())
                mappingHand = 0;
            CacheRegion& region = *mappings[mappingHand];
            const std::size_t length = std::min(kTrimChunk, region.length - region.trimmed);
            char *at = static_cast<char *>(region.data) + region.trimmed;
            const std::uint64_t held = ResidentBytes(at, length);
            if (held > 0)
                madvise(at, length, MADV_DONTNEED);
            dropped += held;
            visited += length;
            region.trimmed += length;
            if (region.trimmed >= region.length)
            {
                region.trimmed = 0;
                ++mappingHand;
            }
        }
    }
    // pages read meanwhile are counted at the next measure
    std::uint64_t mapped = mappedResident.load(std::memory_order_relaxed);
    while (!mappedResident.compare_exchange_weak(mapped, mapped - std::min(mapped, dropped), std::memory_order_relaxed)) {}
    return dropped;
#endif
}

std::uint64_t CacheBudget::MeasureMapped()
{
    std::uint64_t mapped = 0;
#if !defined(_WIN32)
    {
        // the mapped regions' own pages in this process's page tables, not the page cache nor other files
        std::lock_guard<std::mutex> guard(lock);
        for (const CacheRegion *region : mappings)
            mapped += ResidentBytes(region->data, region->length);
    }
#endif
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
        // only trims, which cannot throw; spilling is left to the render threads, which report its errors
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
            spill.reset(new CacheSpillFile(directory, "spill", keepSpill, spillCap));
        }
        catch (const std::exception& error)
        {
            throw POV_EXCEPTION(pov_base::kCannotOpenFileErr, (std::string("Max_Cache_Memory: caches cannot spill to disk: ") + error.what() + ".").c_str());
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
    // grids and lattices too small for pages of their own stay on the heap, never spilled
    const std::int64_t heap = heapRegions.load(std::memory_order_relaxed);
    if (heap > 0)
    {
        list << (pinned == 0 ? "" : ", ") << "grids and lattices under " << MiB(double(kRegionPages)) << " each " << MiB(double(heap));
        pinned += std::uint64_t(heap);
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
    std::string total = "Cache memory: budget " + MiB(double(budget)) + " (Max_Cache_Memory); peak " + MiB(double(Peak())) +
                        " of caches resident with mapped pages, " + MiB(double(HeldPeak())) + " held";
    if (process > 0)
        total += "; process peak " + MiB(double(process));
    lines.push_back(total + ".");
    char text[512];
    std::snprintf(text, sizeof(text), "Cache memory: ceiling on held caches %s for %u threads (the budget or the unspillable caches' peak %s, "
                  "whichever is more, + grids built at once %s + threads x (most one thread pinned %s in %llu cells + %u x largest cell %s)); "
                  "held passed the budget by %s.", MiB(double(Ceiling())).c_str(), threads, MiB(double(UnspillablePeak())).c_str(),
                  MiB(double(BuildingPeak())).c_str(), MiB(double(MostPinnedBytes())).c_str(), (unsigned long long)MostPins(), kTransientCells,
                  MiB(double(LargestCell())).c_str(), MiB(double(HeldPeak() > budget ? HeldPeak() - budget : 0)).c_str());
    lines.push_back(text);
    {
        std::lock_guard<std::mutex> spillGuard(spillLock);
        if (spill != nullptr)
            lines.push_back("Cache memory: spill file " + spill->Path() + ": " + MiB(double(spill->LiveBytes())) + " live, peak " +
                            MiB(double(spill->PeakLiveBytes())) + " live, " + MiB(double(spill->Bytes())) + " long" +
                            (spill->Cap() > 0 ? ", cap " + MiB(double(spill->Cap())) + " (Cache_Max_Size)." : "."));
    }
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
