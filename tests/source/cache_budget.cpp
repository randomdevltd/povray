// cache_budget.cpp: the memory budget keeps evictable blocks within it, least recently used first, never one that is
// pinned; a block spilled and read back holds the same bytes; pinned caches past the budget fail with their names and
// sizes; a sealed array mapped from the spill file reads the same values at the same address; the spill file reads as
// a saved cache.
#include "core/support/cachebudget.h"
#include "core/support/preparedcache.h"
#include <cstdio>
#include <cstring>
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
        Check(budget->Peak() <= budgetBytes + blockBytes, "the peak passed the budget by more than the block being admitted");
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

    // without a budget an array stays a plain vector in memory
    CacheArray<int> plain(std::vector<int>{ 1, 2, 3 });
    plain.Seal("plain");
    Check(!plain.Mapped() && (plain.size() == 3) && (plain[2] == 3), "an array without a budget changed");

    if (failures == 0)
        std::cout << "cache_budget: blocks within the budget, pins held, spill round trip identical, pinned overflow named, "
                     "sealed arrays mapped in place, spill file in the cache format, freed space reused, side-by-side seals "
                     "within the budget, spill capped and its directory made only when used" << std::endl;
    return failures == 0 ? 0 : 1;
}
