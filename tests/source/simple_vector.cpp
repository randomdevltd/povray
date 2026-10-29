#include "core/support/simplevector.h"
#include <cassert>
#include <iostream>
using namespace pov;
template<typename V> static void Check(const V& v, std::initializer_list<int> expected)
{
    assert(v.size() == expected.size());
    size_t i = 0;
    for (int e : expected)
        assert(v[i++] == e);
}
int main()
{
    SimpleVector<int> grown;
    for (int i = 0; i < 8; ++i)
        grown.push_back(i);
    assert(grown.size() == grown.capacity());
    grown.insert(grown.begin() + 3, 99);
    Check(grown, {0, 1, 2, 99, 3, 4, 5, 6, 7});
    grown.insert(grown.begin(), -1);
    Check(grown, {-1, 0, 1, 2, 99, 3, 4, 5, 6, 7});
    SimpleVector<int> empty;
    empty.insert(empty.begin(), 5);
    Check(empty, {5});
    FixedSimpleVector<int, 8> fixed;
    for (int i = 0; i < 5; ++i)
        fixed.push_back(i);
    fixed.insert(fixed.begin() + 1, 42);
    Check(fixed, {0, 42, 1, 2, 3, 4});
    fixed.insert(fixed.end(), 7);
    Check(fixed, {0, 42, 1, 2, 3, 4, 7});
    fixed.insert(fixed.begin(), 8);
    Check(fixed, {8, 0, 42, 1, 2, 3, 4, 7});
    std::cout << "simple_vector: ok" << std::endl;
    return 0;
}
