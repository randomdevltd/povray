// cache_budget.cpp: the memory budget keeps evictable blocks within it, least recently used first, never one that is
// pinned; a block spilled and read back holds the same bytes; pinned caches past the budget fail with their names and
// sizes; a sealed array mapped from the spill file reads the same values at the same address; the spill file reads as
// a saved cache.
#include "core/support/cachebudget.h"
#include "core/support/preparedcache.h"
#include <cstdio>
#include <cstring>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>
using namespace pov;

static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok)
    {
        std::cerr << "cache_budget: " << what << std::endl;
        ++failures;
    }
}

// a block holding a vector of numbers it was built with, as a subsurface cell holds its points
struct TestBlock final : SpillBlock
{
    std::vector<std::uint64_t> values;
    explicit TestBlock(std::size_t n, std::uint64_t seed) : values(n)
    {
        for (std::size_t i = 0; i < n; ++i)
            values[i] = seed * 1000003u + i * 7919u;
    }
    std::size_t HeldBytes() const override { return values.capacity() * sizeof(values[0]); }
    std::string SpillBytes() const override { return std::string(reinterpret_cast<const char *>(values.data()), values.size() * sizeof(values[0])); }
    void Drop() override { std::vector<std::uint64_t>().swap(values); }
    void Restore(const std::string& bytes) override
    {
        values.resize(bytes.size() / sizeof(values[0]));
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }
};

int main()
{
    const std::string dir = "cache_budget_test_dir";
    const std::size_t blockBytes = 1024 * 1024, budgetBytes = 4 * blockBytes;
    {
        auto budget = std::make_shared<CacheBudget>(budgetBytes, dir, true);
        CacheAccount& account = budget->Account("test blocks", false);
        std::vector<std::unique_ptr<TestBlock>> blocks;
        for (std::uint64_t i = 0; i < 16; ++i)
        {
            blocks.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), i));
            budget->Admit(*blocks.back(), account);
            Check(budget->Resident() <= budgetBytes, "the resident bytes passed the budget");
        }
        // the block being admitted and the copy an eviction writes from
        Check(budget->Peak() <= budgetBytes + 2 * blockBytes, "the peak passed the budget by more than the block being admitted and an eviction's copy");
        Check(budget->Peak() > budgetBytes + blockBytes, "an eviction's copy was not counted");
        std::size_t resident = 0;
        for (const auto& block : blocks)
            resident += block->Resident() ? 1 : 0;
        Check(resident <= 4, "more blocks stayed in memory than the budget holds");
        Check(account.spillWrites.load() >= 12, "evicted blocks were not written to the spill file");

        // a pinned block is never evicted while others come in
        TestBlock& kept = *blocks.back();
        budget->Pin(kept);
        for (std::uint64_t i = 0; i < 8; ++i)
        {
            budget->Pin(*blocks[i]);
            blocks[i]->Unpin();
        }
        Check(kept.Resident(), "a pinned block was evicted");
        kept.Unpin();

        // every block reads back as it was built, and no block is written twice
        const std::uint64_t writes = account.spillWrites.load();
        for (std::uint64_t i = 0; i < blocks.size(); ++i)
        {
            budget->Pin(*blocks[i]);
            const TestBlock fresh(blockBytes / sizeof(std::uint64_t), i);
            Check(blocks[i]->values == fresh.values, "a block read back differs from the one built");
            blocks[i]->Unpin();
        }
        Check(account.spillReads.load() >= 12, "evicted blocks were not read back");
        Check((account.spillWrites.load() >= writes) && (account.spillWrites.load() <= blocks.size()), "a block was written to the spill file twice");

        // a sealed array mapped from the spill file keeps its address and its values
        CacheAccount& grids = budget->Account("test grids", false);
        CacheArray<float> grid;
        grid.Budget(budget, &grids);
        grid.resize(3 * blockBytes / sizeof(float));
        for (std::size_t i = 0; i < grid.size(); ++i)
            grid[i] = float(i) * 0.5f;
        const float *before = grid.data();
        grid.Seal("test grid");
        std::vector<std::unique_ptr<TestBlock>> more;
        for (std::uint64_t i = 0; i < 8; ++i)
        {
            more.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), 100 + i));
            budget->Admit(*more.back(), account);
        }
        Check(grid.Mapped(), "a sealed array was not mapped once the budget needed its memory");
        Check(grid.data() == before, "mapping a sealed array moved it");
        bool same = true;
        for (std::size_t i = 0; i < grid.size(); ++i)
            same = same && (grid[i] == float(i) * 0.5f);
        Check(same, "a mapped array reads other values");

        // a heap array sealed when the budget is full goes straight to the file
        std::vector<double> heap(blockBytes / 4);
        for (std::size_t i = 0; i < heap.size(); ++i)
            heap[i] = 1.0 / double(i + 1);
        CacheArray<double> adopted{std::vector<double>(heap)};
        adopted.Budget(budget, &grids);
        adopted.Seal("test lattice");
        Check(adopted.Mapped(), "a sealed heap array was not mapped with the budget full");
        Check(std::memcmp(adopted.data(), heap.data(), heap.size() * sizeof(double)) == 0, "a mapped heap array reads other values");

        // pinned caches within the budget pass; past it they fail with their names and sizes
        CacheAccount& tree = budget->Account("test tree", true);
        budget->Charge(tree, std::int64_t(budgetBytes / 2));
        bool threw = false;
        try { budget->CheckPinned(); } catch (...) { threw = true; }
        Check(!threw, "pinned caches within the budget failed the check");
        CacheAccount& photons = budget->Account("test photons", true);
        budget->Charge(photons, std::int64_t(budgetBytes));
        std::string message;
        try { budget->CheckPinned(); } catch (const std::exception& error) { message = error.what(); }
        Check(message.find("test tree 2.0 MiB") != std::string::npos && message.find("test photons 4.0 MiB") != std::string::npos,
              ("pinned caches past the budget did not fail naming them: " + message).c_str());

        // a forgotten block's section is given back, and a later spill reuses its space before the file grows
        const std::uint64_t live = budget->Spill().LiveBytes(), length = budget->Spill().Bytes();
        for (const auto& block : more)
            budget->Forget(*block);
        Check(budget->Spill().LiveBytes() < live, "forgotten blocks kept their spill space");
        std::vector<std::unique_ptr<TestBlock>> again;
        for (std::uint64_t i = 0; i < 8; ++i)
        {
            again.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), 200 + i));
            budget->Admit(*again.back(), account);
        }
        Check(budget->Spill().Bytes() == length, "the spill file grew although freed sections had room");
        for (const auto& block : again)
            budget->Forget(*block);
        Check(budget->Report().size() >= 3, "the report has no line per cache");
        grid.clear();
        adopted.clear();
    }

    // the kept spill file is a cache file: its index reads and every section checks
    std::string spill;
    {
        std::FILE *list = popen(("ls " + dir + "/spill-*.povc 2>/dev/null").c_str(), "r");
        char line[1024];
        if ((list != nullptr) && std::fgets(line, sizeof(line), list))
            spill = std::string(line, std::strcspn(line, "\n"));
        if (list != nullptr)
            pclose(list);
    }
    Check(!spill.empty(), "the spill file was not kept for the test");
    if (!spill.empty())
    {
        try
        {
            PreparedCacheFile file(spill);
            Check(!file.Missing() && (file.Entries().size() >= 12), "the spill file's index holds too few sections");
            file.Check();
        }
        catch (const std::exception& error)
        {
            Check(false, (std::string("the spill file does not read as a cache file: ") + error.what()).c_str());
        }
        std::remove(spill.c_str());
    }
    std::remove(dir.c_str());

    // arrays sealed side by side reserve both their copies at once, so together they never pass the budget
    {
        auto budget = std::make_shared<CacheBudget>(8 * blockBytes, dir, false);
        CacheAccount& grids = budget->Account("test grids", false);
        std::vector<CacheArray<float>> arrays(8);
        std::vector<std::thread> threads;
        for (std::size_t t = 0; t < arrays.size(); ++t)
            threads.emplace_back([&, t]()
            {
                arrays[t] = std::vector<float>(2 * blockBytes / sizeof(float), float(t));
                arrays[t].Budget(budget, &grids);
                arrays[t].Seal("test lattice");
            });
        for (std::thread& thread : threads)
            thread.join();
        Check(budget->Peak() <= 8 * blockBytes, "arrays sealed side by side passed the budget together");
        bool same = true;
        for (std::size_t t = 0; t < arrays.size(); ++t)
            for (std::size_t i = 0; i < arrays[t].size(); i += 4096)
                same = same && (arrays[t][i] == float(t));
        Check(same, "an array sealed side by side reads other values");
        arrays.clear();
    }
    struct stat info;
    Check(stat(dir.c_str(), &info) != 0, "the spill directory a render made was left behind");

    // a budget that never spills makes no spill directory
    {
        auto budget = std::make_shared<CacheBudget>(64 * blockBytes, dir + "/spill", false);
        TestBlock block(blockBytes / sizeof(std::uint64_t), 1);
        budget->Admit(block, budget->Account("test blocks", false));
        budget->Forget(block);
    }
    Check(stat(dir.c_str(), &info) != 0, "a budget that never spilled made a spill directory");

    // spilling past the cap stops with an error naming the cap; a spill directory that cannot be made names itself
    {
        auto budget = std::make_shared<CacheBudget>(blockBytes, dir, false, 2 * blockBytes);
        CacheAccount& account = budget->Account("test blocks", false);
        std::vector<std::unique_ptr<TestBlock>> blocks;
        std::string message;
        try
        {
            for (std::uint64_t i = 0; i < 8; ++i)
            {
                blocks.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), i));
                budget->Admit(*blocks.back(), account);
            }
        }
        catch (const std::exception& error) { message = error.what(); }
        Check(message.find("Cache_Max_Size") != std::string::npos, ("spilling past the cap did not fail naming it: " + message).c_str());
        Check(budget->Spill().LiveBytes() <= 2 * blockBytes, "the spill file passed its cap");
        for (const auto& block : blocks)
            budget->Forget(*block);
    }
    {
        auto budget = std::make_shared<CacheBudget>(blockBytes, "/proc/no-such-dir/spill", false);
        std::string message;
        try { budget->Spill(); } catch (const std::exception& error) { message = error.what(); }
        Check(message.find("/proc/no-such-dir/spill") != std::string::npos, ("an unusable spill directory was not named: " + message).c_str());
    }

    // a spill failing while room is made for a read-back takes back its charge, and the block it could not evict stays
    // counted on the clock, so forgetting the blocks leaves nothing behind and later evictions touch only live blocks
    {
        auto budget = std::make_shared<CacheBudget>(blockBytes, dir, false, 2 * blockBytes + 4096);
        CacheAccount& account = budget->Account("test blocks", false);
        std::vector<std::unique_ptr<TestBlock>> blocks;
        std::string message;
        try
        {
            for (std::uint64_t i = 0; i < 3; ++i)
            {
                blocks.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), i));
                budget->Admit(*blocks.back(), account);
            }
        }
        catch (const std::exception& error) { message = error.what(); }
        // two blocks fill the spill file to its cap, the third fills the budget
        TestBlock *resident = nullptr, *spilled = nullptr;
        for (const auto& block : blocks)
            (block->Resident() ? resident : spilled) = block.get();
        Check(message.empty() && (resident != nullptr) && (spilled != nullptr), "the blocks were not spilled to the cap");
        if ((resident != nullptr) && (spilled != nullptr))
        {
            const std::int64_t before = account.resident.load();
            try { budget->Pin(*spilled); } catch (const std::exception& error) { message = error.what(); }
            Check(message.find("Cache_Max_Size") != std::string::npos, ("a read-back whose room could not be made did not fail naming the cap: " + message).c_str());
            Check(account.resident.load() == before, "a failed read-back left its charge behind");
            Check(resident->Resident(), "the block that could not be spilled was dropped");
        }
        for (const auto& block : blocks)
            budget->Forget(*block);
        Check(account.resident.load() == 0, "forgotten blocks left bytes counted after a failed spill");
        blocks.clear();
        TestBlock fresh(blockBytes / 2 / sizeof(std::uint64_t), 9), other(blockBytes / sizeof(std::uint64_t), 10);
        budget->Admit(fresh, account);
        try { budget->Admit(other, account); } catch (const std::exception&) {}
        Check(!fresh.Resident() || !other.Resident(), "a later block evicted nothing");
        budget->Forget(fresh);
        budget->Forget(other);
    }

    // arrays too small for pages of their own stay on the heap: counted as unspillable, named when they pass the budget
    {
        auto budget = std::make_shared<CacheBudget>(blockBytes, dir, false);
        CacheAccount& grids = budget->Account("test grids", false);
        std::vector<CacheArray<float>> small(24);
        for (CacheArray<float>& array : small)
        {
            array.Budget(budget, &grids);
            array.resize(64 * 1024 / sizeof(float), 1.0f);
            array.Seal("test small grid");
        }
        Check(budget->UnspillablePeak() >= 24 * 64 * 1024, "arrays on the heap were not counted as unspillable");
        Check(budget->Ceiling() >= budget->UnspillablePeak(), "the ceiling is below the unspillable caches");
        std::string message;
        try { budget->CheckPinned(); } catch (const std::exception& error) { message = error.what(); }
        Check(message.find("grids and lattices under 1.0 MiB each 1.5 MiB") != std::string::npos,
              ("arrays on the heap past the budget were not named: " + message).c_str());
        small.clear();
        Check(budget->Resident() == 0, "arrays on the heap left bytes counted once freed");
    }

    // sixteen threads: hot large blocks that stay pinned, many small blocks pinned a ring of a hundred at a time and
    // spilled between, an eviction's copy and a read-back's counted, and small arrays on the heap; what is held stays
    // under the ceiling the budget reports
    {
        const unsigned threadCount = 16;
        auto budget = std::make_shared<CacheBudget>(2 * blockBytes, dir, false, 0, threadCount);
        CacheAccount& account = budget->Account("test cells", false);
        CacheAccount& grids = budget->Account("test grids", false);
        std::vector<std::unique_ptr<TestBlock>> hot, small;
        for (std::uint64_t i = 0; i < 4; ++i)
        {
            hot.emplace_back(new TestBlock(blockBytes / sizeof(std::uint64_t), 1000 + i));
            budget->Admit(*hot.back(), account);
        }
        for (std::uint64_t i = 0; i < 400; ++i)
        {
            small.emplace_back(new TestBlock(16 * 1024 / sizeof(std::uint64_t), 2000 + i));
            budget->Admit(*small.back(), account);
        }
        std::vector<CacheArray<float>> lattices(40);
        for (CacheArray<float>& lattice : lattices)
        {
            lattice.Budget(budget, &grids);
            lattice.resize(32 * 1024 / sizeof(float), 0.5f);
            lattice.Seal("test lattice");
        }
        std::atomic<int> wrong{0};
        std::vector<std::thread> threads;
        for (unsigned t = 0; t < threadCount; ++t)
            threads.emplace_back([&, t]()
            {
                std::uint64_t seed = t * 2654435761u + 1;
                for (int round = 0; round < 60; ++round)
                {
                    std::vector<std::pair<TestBlock *, std::size_t>> pins;
                    std::size_t bytes = 0;
                    auto pin = [&](TestBlock& block)
                    {
                        budget->Pin(block);
                        pins.emplace_back(&block, block.PinnedBytes());
                        bytes += pins.back().second;
                        budget->NotePins(pins.size(), bytes);
                    };
                    // half the threads hold a hot block through the whole round, as every shading point near it does
                    if (t % 2 == 0)
                        pin(*hot[(t / 2) % hot.size()]);
                    for (int k = 0; k < 100; ++k)
                    {
                        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                        pin(*small[(seed >> 33) % small.size()]);
                    }
                    for (const auto& held : pins)
                    {
                        const std::uint64_t first = held.first->values.empty() ? ~0ull : held.first->values[0];
                        if ((first % 1000003u) != 0)
                            ++wrong;
                        held.first->Unpin();
                    }
                }
            });
        for (std::thread& thread : threads)
            thread.join();
        Check(wrong.load() == 0, "a pinned block read other values under sixteen threads");
        Check(account.spillWrites.load() > 0 && account.spillReads.load() > 0, "the sixteen-thread stress spilled nothing");
        Check(budget->MostPins() >= 100, "the most pins one thread held were not noted");
        Check(budget->LargestCell() >= blockBytes, "a hot block that never spilled was not the largest cell");
        char text[256];
        std::snprintf(text, sizeof(text), "sixteen threads held %.1f MiB, past the ceiling of %.1f MiB", budget->HeldPeak() / 1048576.0,
                      budget->Ceiling() / 1048576.0);
        Check(budget->HeldPeak() <= budget->Ceiling(), text);
        std::cout << "cache_budget: sixteen threads held at most " << budget->HeldPeak() / 1048576.0 << " MiB under a ceiling of "
                  << budget->Ceiling() / 1048576.0 << " MiB" << std::endl;
        for (const auto& block : hot)
            budget->Forget(*block);
        for (const auto& block : small)
            budget->Forget(*block);
        lattices.clear();
    }

    // a larger page, as some systems have: sections that can be mapped start on it, and a sealed array maps on it
    {
        SetCachePageBytes(16384);
        Check(CachePageBytes() == 16384, "a stand-in page was not taken");
        {
            CacheSpillFile file(dir, "pages", false);
            const std::string tiny(100, 'a'), page(20000, 'b');
            file.Append("tiny", tiny.data(), tiny.size());
            const std::size_t index = file.Append("page", page.data(), page.size());
            file.Append("tiny", tiny.data(), tiny.size());
            const std::size_t again = file.Append("page", page.data(), page.size());
            Check((file.Entry(index).offset % 16384 == 0) && (file.Entry(again).offset % 16384 == 0),
                  "a section of a page or more did not start on the larger page");
            file.Free(index);
            const std::size_t reused = file.Append("page", page.data(), page.size());
            Check(file.Entry(reused).offset % 16384 == 0, "a section placed in freed space did not start on the larger page");
        }
        auto budget = std::make_shared<CacheBudget>(blockBytes, dir, false);
        CacheAccount& grids = budget->Account("test grids", false);
        CacheAccount& account = budget->Account("test blocks", false);
        // a small block spilled first leaves the file's end off any page
        TestBlock odd(1000, 77);
        budget->Admit(odd, account);
        CacheArray<float> grid;
        grid.Budget(budget, &grids);
        grid.resize((blockBytes + 3000) / sizeof(float));
        for (std::size_t i = 0; i < grid.size(); ++i)
            grid[i] = float(i) * 0.25f;
        const float *before = grid.data();
        grid.Seal("test grid");
        TestBlock push(blockBytes / sizeof(std::uint64_t), 78);
        std::string message;
        try { budget->Admit(push, account); } catch (const std::exception& error) { message = error.what(); }
        Check(message.empty(), ("mapping on a larger page failed: " + message).c_str());
        Check(grid.Mapped() && (grid.data() == before), "a sealed array was not mapped in place on a larger page");
        bool same = true;
        for (std::size_t i = 0; i < grid.size(); ++i)
            same = same && (grid[i] == float(i) * 0.25f);
        Check(same, "an array mapped on a larger page reads other values");
        budget->Forget(odd);
        budget->Forget(push);
        grid.clear();
        SetCachePageBytes(0);
        Check(CachePageBytes() == SystemPageBytes(), "the system's page did not come back");
    }

    // without a budget an array stays a plain vector in memory
    CacheArray<int> plain(std::vector<int>{ 1, 2, 3 });
    plain.Seal("plain");
    Check(!plain.Mapped() && (plain.size() == 3) && (plain[2] == 3), "an array without a budget changed");

    if (failures == 0)
        std::cout << "cache_budget: blocks within the budget, pins held, spill round trip identical, pinned overflow named, "
                     "sealed arrays mapped in place, spill file in the cache format, freed space reused, side-by-side seals "
                     "within the budget, spill capped and its directory made only when used, failed spills take back their charge, heap arrays "
                     "counted unspillable, sixteen threads under the ceiling, sections and mappings on a larger page" << std::endl;
    return failures == 0 ? 0 : 1;
}
