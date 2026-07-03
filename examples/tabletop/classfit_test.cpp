// classfit_test.cpp — roll random characters, let the data pick the class.
//   g++ -std=c++17 classfit_test.cpp -o /tmp/cf && /tmp/cf [count] [seed]
#include "classfit.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>

int main(int argc, char** argv) {
    int count = argc > 1 ? std::atoi(argv[1]) : 14;
    unsigned seed = argc > 2 ? (unsigned)std::atoi(argv[2]) : std::random_device{}();
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d6(1, 6);
    auto roll4d6 = [&]() { int a=d6(rng),b=d6(rng),c=d6(rng),e=d6(rng); return a+b+c+e-std::min({a,b,c,e}); };
    auto roll3d6 = [&]() { return d6(rng)+d6(rng)+d6(rng); };

    printf("seed %u\n", seed);
    printf("STR DEX CON INT WIS CHA | brv tmp pty cur hon dil soc | best fit (runners-up)\n");
    printf("------------------------------------------------------------------------------------------\n");
    for (int n = 0; n < count; ++n) {
        std::array<int,6> ab;               // straight down: STR DEX CON INT WIS CHA
        for (auto& x : ab) x = roll4d6();
        rpgc::Character p;
        p.bravery=roll4d6(); p.narcissism=roll3d6(); p.willToPower=roll3d6(); p.temper=roll3d6();
        p.carnality=roll3d6(); p.cruelty=roll3d6(); p.sociability=roll3d6(); p.skepticism=roll3d6();
        p.honor=roll3d6(); p.piety=roll3d6(); p.greed=roll3d6(); p.diligence=roll3d6();
        p.compassion=roll3d6(); p.curiosity=roll3d6();

        auto rank = rpgcf::rankClasses(ab, p);
        printf("%3d %3d %3d %3d %3d %3d | %3d %3d %3d %3d %3d %3d %3d | %-9s (%s %.0f, %s %.0f)\n",
               ab[0],ab[1],ab[2],ab[3],ab[4],ab[5],
               p.bravery,p.temper,p.piety,p.curiosity,p.honor,p.diligence,p.sociability,
               rank[0].cls.c_str(), rank[1].cls.c_str(), rank[1].total, rank[2].cls.c_str(), rank[2].total);
    }
    return 0;
}
