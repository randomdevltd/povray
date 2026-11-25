// Unit tests for the region schedule's planner (backend/scene/regionschedule.h): the Hilbert order, clusters that fit,
// every tile once, and far fewer section loads than line order under the same capacity.
#include "backend/scene/regionschedule.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace pov;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); failures++; } } while (0)

// Loads when each cluster runs `levels` times in a row under an LRU of `capacity`: how the scheduled levels run.
static std::uint64_t ClusterMajorLoads(const std::vector<RegionTile>& tiles, const std::vector<std::uint64_t>& bytes, const RegionPlan& plan,
                                       std::uint64_t capacity, int levels)
{
    std::vector<std::uint32_t> order;
    for (size_t k = 0; k < plan.clusterStart.size(); k++)
    {
        const size_t end = (k + 1 < plan.clusterStart.size()) ? plan.clusterStart[k + 1] : plan.order.size();
        for (int l = 0; l < levels; l++)
            order.insert(order.end(), plan.order.begin() + plan.clusterStart[k], plan.order.begin() + end);
    }
    return RegionLruLoads(tiles, bytes, order, capacity, 1);
}

int main()
{
    // the Hilbert curve visits every cell of an 8 x 8 grid once, each step to a neighbour
    {
        std::vector<std::pair<int, int>> at(64, std::make_pair(-1, -1));
        for (std::uint32_t y = 0; y < 8; y++)
            for (std::uint32_t x = 0; x < 8; x++)
            {
                const std::uint64_t d = RegionHilbert(8, x, y);
                CHECK(d < 64, "hilbert %u,%u -> %llu", x, y, (unsigned long long)d);
                if (d < 64)
                {
                    CHECK(at[d].first < 0, "hilbert %llu twice", (unsigned long long)d);
                    at[d] = std::make_pair(int(x), int(y));
                }
            }
        for (int d = 1; d < 64; d++)
            CHECK(std::abs(at[d].first - at[d - 1].first) + std::abs(at[d].second - at[d - 1].second) == 1, "hilbert step %d jumps", d);
    }

    // a frame of 32 x 16 tiles with a row of 64 objects across each half: line order meets every object of a half on every
    // row, so a cache holding a fifth of them reloads each object once per row and level
    const std::uint32_t W = 32, H = 16;
    std::vector<RegionTile> tiles(W * H);
    std::vector<std::uint64_t> bytes;
    for (std::uint32_t half = 0; half < 2; half++)
        for (std::uint32_t o = 0; o < 64; o++)
        {
            const std::uint32_t id = std::uint32_t(bytes.size());
            bytes.push_back(1000 + 37 * ((o * 7 + half) % 11));
            // each object spans 2 columns and the half's 8 rows, overlapping its neighbour by a column
            const std::uint32_t x0 = (o / 2) % W;
            for (std::uint32_t y = half * 8; y < half * 8 + 8; y++)
                for (std::uint32_t x = x0; (x < x0 + 2) && (x < W); x++)
                    tiles[y * W + x].blocks.push_back(id);
        }
    for (std::uint32_t y = 0; y < H; y++)
        for (std::uint32_t x = 0; x < W; x++)
        {
            RegionTile& t = tiles[y * W + x];
            std::sort(t.blocks.begin(), t.blocks.end());
            t.blocks.erase(std::unique(t.blocks.begin(), t.blocks.end()), t.blocks.end());
            t.hilbert = RegionHilbert(32, x, y);
        }
    std::uint64_t all = 0;
    for (std::uint64_t b : bytes)
        all += b;
    const std::uint64_t capacity = all / 5;
    const RegionPlan plan = PlanRegions(tiles, bytes, capacity, std::vector<char>());

    // every tile exactly once, clusters ascending
    std::set<std::uint32_t> seen(plan.order.begin(), plan.order.end());
    CHECK(plan.order.size() == tiles.size() && seen.size() == tiles.size(), "order holds %zu tiles, %zu distinct, of %zu",
          plan.order.size(), seen.size(), tiles.size());
    CHECK(!plan.clusterStart.empty() && plan.clusterStart[0] == 0, "first cluster starts at 0");
    CHECK(plan.clusterBlocks.size() == plan.clusterStart.size(), "%zu clusters, %zu working sets", plan.clusterStart.size(), plan.clusterBlocks.size());
    for (size_t k = 1; k < plan.clusterStart.size(); k++)
        CHECK(plan.clusterStart[k] > plan.clusterStart[k - 1], "cluster %zu empty", k);
    // each cluster's sections fit the capacity, unless it is a single tile that alone passes it
    for (size_t k = 0; k < plan.clusterBlocks.size(); k++)
    {
        std::uint64_t held = 0;
        for (std::uint32_t b : plan.clusterBlocks[k])
            held += bytes[b];
        const size_t end = (k + 1 < plan.clusterStart.size()) ? plan.clusterStart[k + 1] : plan.order.size();
        CHECK(held <= capacity || end - plan.clusterStart[k] == 1, "cluster %zu holds %llu of %llu", k, (unsigned long long)held, (unsigned long long)capacity);
    }
    CHECK(plan.unionBytes == all, "union %llu of %llu", (unsigned long long)plan.unionBytes, (unsigned long long)all);
    // the design's target: bytes loaded within 1.5x of the footprint union
    CHECK(plan.plannedBytes <= all + all / 2, "planned %llu, union %llu", (unsigned long long)plan.plannedBytes, (unsigned long long)all);

    // three levels: line order reloads each object per row and level; cluster-major loads them about once
    std::vector<std::uint32_t> line(tiles.size());
    for (std::uint32_t i = 0; i < line.size(); i++)
        line[i] = i;
    const std::uint64_t lineLoads = RegionLruLoads(tiles, bytes, line, capacity, 3);
    const std::uint64_t scheduled = ClusterMajorLoads(tiles, bytes, plan, capacity, 3);
    std::printf("region_schedule: %zu tiles, %zu clusters, union %llu, planned %llu, cluster-major LRU %llu, line-order LRU %llu (%.1fx)\n",
                tiles.size(), plan.clusterStart.size(), (unsigned long long)all, (unsigned long long)plan.plannedBytes,
                (unsigned long long)scheduled, (unsigned long long)lineLoads, double(lineLoads) / double(scheduled));
    CHECK(scheduled <= all + all / 2, "cluster-major loads %llu past 1.5x the union %llu", (unsigned long long)scheduled, (unsigned long long)all);
    CHECK(lineLoads > 4 * scheduled, "line order %llu not well past the schedule %llu", (unsigned long long)lineLoads, (unsigned long long)scheduled);

    // all of it fits: one cluster, loads the union once, nothing resident loaded again
    {
        const RegionPlan one = PlanRegions(tiles, bytes, all, std::vector<char>());
        CHECK(one.clusterStart.size() == 1, "%zu clusters when everything fits", one.clusterStart.size());
        CHECK(one.plannedBytes == all, "planned %llu of %llu when everything fits", (unsigned long long)one.plannedBytes, (unsigned long long)all);
        const RegionPlan warm = PlanRegions(tiles, bytes, all, std::vector<char>(bytes.size(), 1));
        CHECK(warm.plannedBytes == 0, "planned %llu with everything resident", (unsigned long long)warm.plannedBytes);
    }

    // tiles without footprints, and a tile larger than the capacity, still all scheduled
    {
        std::vector<RegionTile> sparse(6);
        std::vector<std::uint64_t> big = { 500, 10, 10 };
        sparse[2].blocks = { 0, 1 };
        sparse[4].blocks = { 2 };
        for (std::uint32_t i = 0; i < sparse.size(); i++)
            sparse[i].hilbert = i;
        const RegionPlan p = PlanRegions(sparse, big, 100, std::vector<char>());
        std::set<std::uint32_t> got(p.order.begin(), p.order.end());
        CHECK(p.order.size() == 6 && got.size() == 6, "sparse order holds %zu", p.order.size());
        CHECK(p.order[0] == 2, "the largest footprint starts, got tile %u", p.order[0]);
        const RegionPlan none = PlanRegions(std::vector<RegionTile>(), big, 100, std::vector<char>());
        CHECK(none.order.empty() && none.clusterStart.empty(), "no tiles, no plan");
    }

    // the uses: each section's clusters ascending, as eviction reads them
    {
        const std::vector<std::vector<std::uint32_t>> uses = RegionUses(plan, bytes.size());
        for (size_t b = 0; b < uses.size(); b++)
        {
            CHECK(!uses[b].empty(), "section %zu used by no cluster", b);
            for (size_t i = 1; i < uses[b].size(); i++)
                CHECK(uses[b][i] > uses[b][i - 1], "section %zu uses out of order", b);
        }
    }

    // a saved schedule is followed only when it is a permutation of the blocks with cluster starts rising from 0
    {
        const std::vector<int> good = { 1, 2, 0, 2, 3, 1, 0, 2 };
        CHECK(RegionScheduleValid(good, 4), "a valid schedule refused");
        CHECK(!RegionScheduleValid(good, 5), "a schedule of another block count accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 2, 3, 1, 1, 2 }, 4), "a duplicate serial accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 2, 3, 1, 4, 2 }, 4), "a serial past the blocks accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 2, 3, 1, -1, 2 }, 4), "a negative serial accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 1, 2, 3, 1, 0, 2 }, 4), "a first cluster not at 0 accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 0, 3, 1, 0, 2 }, 4), "an empty cluster accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 4, 3, 1, 0, 2 }, 4), "a cluster start past the order accepted");
        CHECK(!RegionScheduleValid({ 2, 2, 0, 2, 3, 1, 0, 2 }, 4), "another format accepted");
        CHECK(!RegionScheduleValid({ 1, 0, 3, 1, 0, 2 }, 4), "no clusters accepted");
        CHECK(!RegionScheduleValid({ 1, 2, 0, 2, 3, 1, 0 }, 4), "a short order accepted");
        CHECK(!RegionScheduleValid({}, 4), "an empty schedule accepted");
    }

    if (failures)
    {
        std::printf("region_schedule: %d failures\n", failures);
        return 1;
    }
    std::printf("region_schedule: ok\n");
    return 0;
}
