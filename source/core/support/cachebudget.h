//******************************************************************************
///
/// @file core/support/cachebudget.h
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

#ifndef POVRAY_CORE_CACHEBUDGET_H
#define POVRAY_CORE_CACHEBUDGET_H

// Module config header file must be the first file included within POV-Ray unit header files
#include "core/configcore.h"

// C++ standard header files
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pov
{

class CacheSpillFile;
class CacheBudget;

/// One cache's figures under the budget: the bytes it holds in memory now and at most, and what it spilled and read back.
struct CacheAccount final
{
    std::string name;
    bool pinned = false; ///< it cannot be divided, so it is counted but never spilled
    std::atomic<std::int64_t> resident{0};
    std::atomic<std::int64_t> peak{0};
    std::atomic<std::uint64_t> spillWrites{0}, spillWriteBytes{0}, spillReads{0}, spillReadBytes{0}, evictions{0};
};

/// A piece of a cache the budget may evict once it is complete: it is written to the spill file the first time, its memory
/// freed, and read back when a lookup next pins it. Readers pin it for the length of a lookup.
class SpillBlock
{
    public:
        virtual ~SpillBlock() = default;
        /// The fast path: pins a resident block with two atomic operations and no lock; false when it must be faulted in.
        bool TryPin() noexcept
        {
            const int before = pins.fetch_add(1, std::memory_order_acquire);
            if ((before >= 0) && resident.load(std::memory_order_acquire))
            {
                if (!referenced.load(std::memory_order_relaxed))
                    referenced.store(true, std::memory_order_relaxed);
                return true;
            }
            pins.fetch_sub(1, std::memory_order_release);
            return false;
        }
        void Unpin() noexcept { pins.fetch_sub(1, std::memory_order_release); }
        bool Resident() const noexcept { return resident.load(std::memory_order_acquire); }
    protected:
        /// The bytes the block holds in memory now.
        virtual std::size_t HeldBytes() const = 0;
        /// Its contents, in the order Restore reads them back.
        virtual std::string SpillBytes() const = 0;
        /// Frees what SpillBytes wrote.
        virtual void Drop() = 0;
        /// Reads back what SpillBytes wrote; throws on a mismatch.
        virtual void Restore(const std::string& bytes) = 0;
    private:
        friend class CacheBudget;
        static constexpr int kEvicting = -(1 << 30);
        std::atomic<int> pins{0};
        std::atomic<bool> resident{true}, referenced{true};
        std::mutex blockMutex;
        CacheAccount *account = nullptr;
        std::size_t section = ~std::size_t(0); ///< its section of the spill file once written
        std::size_t charged = 0; ///< the bytes the budget counts for it
        std::size_t slot = ~std::size_t(0); ///< its place on the clock while it can be evicted
};

/// A memory region of a cache that never changes once built, which the budget may move to a mapping of the spill file in
/// place: the same addresses then read the same bytes from the file, so readers need no pins, and the kernel drops its
/// pages instead of swapping them.
class CacheRegion final
{
    public:
        CacheRegion() = default;
        ~CacheRegion();
        CacheRegion(const CacheRegion&) = delete;
        CacheRegion& operator=(const CacheRegion&) = delete;
        /// Bytes for `bytes` of data, page aligned; under a budget they are counted until Release.
        void *Allocate(std::size_t bytes, const std::shared_ptr<CacheBudget>& budget, CacheAccount *account);
        /// Holds a copy of final data: in pages of its own while the budget has room for them, else straight from the spill file.
        void Adopt(const void *source, std::size_t bytes, const std::shared_ptr<CacheBudget>& budget, CacheAccount *account, const char *label);
        /// Its contents are final: the budget may now map it from the spill file.
        void Seal(const char *label);
        void Release();
        void *Data() const { return data; }
        std::size_t Bytes() const { return bytes; }
        bool Mapped() const { return mapped.load(std::memory_order_acquire); }
    private:
        friend class CacheBudget;
        void *data = nullptr;
        std::size_t bytes = 0, length = 0; ///< asked for, and the whole pages held
        bool pages = false; ///< held as pages of its own, which can be mapped in place, rather than on the heap
        std::atomic<bool> mapped{false};
        std::shared_ptr<CacheBudget> budget;
        CacheAccount *account = nullptr;
        std::string label;
        std::size_t slot = ~std::size_t(0);
        std::size_t trimmed = 0; ///< where the next trim of its mapped pages starts
};

/// The budget: every large cache's resident bytes counted against one figure. Past it, sealed regions are mapped from the
/// spill file, complete blocks are evicted least recently used first (the clock's second chance), and mapped pages are
/// dropped. Caches that cannot be divided are pinned and counted.
class CacheBudget final : public std::enable_shared_from_this<CacheBudget>
{
    public:
        /// `bytes` above zero; spills go to a file in `spillDirectory`, created when first needed.
        CacheBudget(std::uint64_t bytes, const std::string& spillDirectory, bool keepSpill = false);
        ~CacheBudget();
        std::uint64_t Bytes() const { return budget; }
        const std::string& SpillDirectory() const { return directory; }
        /// The account named so, made the first time it is asked for.
        CacheAccount& Account(const std::string& name, bool pinned);
        /// Adds bytes (or with a negative count, removes them) to an account; past the budget, evicts what it can.
        void Charge(CacheAccount& account, std::int64_t bytes);
        /// A complete block, resident and unpinned, now counted under `account` and evictable.
        void Admit(SpillBlock& block, CacheAccount& account);
        /// A block going away, evicted or not; whoever calls holds no pin on it.
        void Forget(SpillBlock& block);
        /// Brings an evicted block back and pins it; the slow path of a lookup that TryPin refused.
        void Fault(SpillBlock& block);
        /// Pins a block, faulting it in when it was evicted.
        void Pin(SpillBlock& block) { if (!block.TryPin()) Fault(block); }
        /// The total resident, counted and measured.
        std::uint64_t Resident() const;
        std::uint64_t Peak() const { return peakTotal.load(std::memory_order_relaxed); }
        /// Throws, listing them, when the pinned caches alone pass the budget; before the first pixel.
        void CheckPinned() const;
        /// One line for each cache with figures, then a total.
        std::vector<std::string> Report() const;
        /// The spill file, made the first time it is asked for; throws when it cannot be made.
        CacheSpillFile& Spill();
        /// Whether any bytes went to the spill file.
        bool Spilled() const { return spillUsed.load(std::memory_order_relaxed); }
        /// The process's peak resident memory, from the system, or 0 where it cannot be read.
        static std::uint64_t ProcessPeak();
        /// The budget a Max_Cache_Memory option or POV_CACHE_MEMORY gives, in MB (MiB, 1024 x 1024 bytes), as bytes; 0 for none.
        static std::uint64_t FromMegabytes(long long megabytes) { return (megabytes > 0) ? std::uint64_t(megabytes) << 20 : 0; }
    private:
        friend class CacheRegion;
        std::uint64_t budget;
        std::string directory;
        bool keepSpill;
        mutable std::mutex lock;
        std::deque<CacheAccount> accounts;
        std::unique_ptr<CacheSpillFile> spill;
        std::mutex spillLock;
        std::atomic<bool> spillUsed{false};
        std::atomic<std::int64_t> counted{0}; ///< every account's resident bytes; mapped pages are measured instead
        std::atomic<std::uint64_t> peakTotal{0};
        std::vector<SpillBlock *> clock;
        std::size_t hand = 0;
        std::vector<CacheRegion *> regions; ///< sealed regions held in memory, evictable by mapping
        std::vector<CacheRegion *> mappings; ///< regions mapped from the spill file, whose pages can be dropped
        std::size_t regionHand = 0, mappingHand = 0;
        std::atomic<std::uint64_t> baselineFilePages{0};
        std::atomic<std::uint64_t> mappedResident{0}; ///< the mapped pages last measured in memory
        std::atomic<std::uint64_t> pinnedOver{0}; ///< how far pinned caches passed the budget during the render
        std::thread watcher;
        std::condition_variable wake;
        bool stopping = false;
        void Note(std::int64_t total);
        void MakeRoom();
        bool EvictOne();
        bool MapOne();
        std::uint64_t TrimMapped(std::uint64_t bytes);
        void Seal(CacheRegion& region);
        bool MapCopy(CacheRegion& region, const void *source);
        void Unregister(CacheRegion& region);
        void Watch();
        std::uint64_t MeasureMapped();
};

/// A vector of a cache that never changes once built: built in memory, then, under a budget, sealed so the budget can map
/// it from the spill file in place. Readers index it as they would a vector, at the same cost.
template<class T>
class CacheArray final
{
        static_assert(std::is_trivially_destructible<T>::value, "a cache array's elements are copied as bytes");
    public:
        using value_type = T;
        CacheArray() = default;
        CacheArray(std::vector<T>&& values) { *this = std::move(values); }
        CacheArray(CacheArray&& other) noexcept { swap(other); }
        CacheArray& operator=(CacheArray&& other) noexcept { swap(other); return *this; }
        CacheArray& operator=(std::vector<T>&& values)
        {
            clear();
            heap = std::move(values);
            view = heap.data();
            count = heap.size();
            return *this;
        }
        CacheArray(const CacheArray&) = delete;
        CacheArray& operator=(const CacheArray&) = delete;

        std::size_t size() const { return count; }
        bool empty() const { return count == 0; }
        const T *data() const { return view; }
        T *data() { return const_cast<T *>(view); }
        const T& operator[](std::size_t i) const { return view[i]; }
        /// Writes only while it is built; a write after it is sealed would land in a private copy of the page.
        T& operator[](std::size_t i) { return const_cast<T&>(view[i]); }
        const T *begin() const { return view; }
        const T *end() const { return view + count; }
        T *begin() { return data(); }
        T *end() { return data() + count; }

        /// Sizes it to `n` elements: valued `value`, or, unless `fill`, as they come where it holds pages of its own.
        void resize(std::size_t n, const T& value = T()) { Size(n, &value); }
        void assign(std::size_t n, const T& value) { clear(); Size(n, &value); }
        void clear()
        {
            std::vector<T>().swap(heap);
            region.reset();
            view = nullptr;
            count = 0;
        }
        void swap(CacheArray& other) noexcept
        {
            heap.swap(other.heap);
            region.swap(other.region);
            std::swap(view, other.view);
            std::swap(count, other.count);
        }

        /// Uses the budget's pages for later sizes: those of `bytes` or more can then be mapped once sealed.
        void Budget(const std::shared_ptr<CacheBudget>& owner, CacheAccount *account)
        {
            budget = owner;
            budgetAccount = account;
        }
        /// Its contents are final: under a budget, moves them to pages of their own and lets the budget map them.
        void Seal(const char *label)
        {
            if ((budget == nullptr) || (count == 0))
                return;
            if (region == nullptr)
            {
                // built on the heap: copied once, to the budget's pages or the spill file, before any reader sees it
                std::unique_ptr<CacheRegion> own(new CacheRegion());
                own->Adopt(view, count * sizeof(T), budget, budgetAccount, label);
                std::vector<T>().swap(heap);
                view = static_cast<const T *>(own->Data());
                region = std::move(own);
                if (region->Mapped())
                    return;
            }
            region->Seal(label);
        }
        bool Mapped() const { return (region != nullptr) && region->Mapped(); }
    private:
        std::vector<T> heap;
        std::unique_ptr<CacheRegion> region;
        const T *view = nullptr;
        std::size_t count = 0;
        std::shared_ptr<CacheBudget> budget;
        CacheAccount *budgetAccount = nullptr;
        void Size(std::size_t n, const T *value)
        {
            if ((budget != nullptr) && (region == nullptr) && heap.empty() && (n > 0))
            {
                // built straight into pages of its own, so sealing it moves nothing
                std::unique_ptr<CacheRegion> own(new CacheRegion());
                T *at = static_cast<T *>(own->Allocate(n * sizeof(T), budget, budgetAccount));
                for (std::size_t i = 0; i < n; ++i)
                    at[i] = *value;
                region = std::move(own);
                view = at;
                count = n;
                return;
            }
            if (region != nullptr)
            {
                std::vector<T> copy(view, view + count);
                region.reset();
                heap.swap(copy);
            }
            heap.resize(n, *value);
            view = heap.data();
            count = n;
        }
};

}
// end of namespace pov

#endif // POVRAY_CORE_CACHEBUDGET_H
