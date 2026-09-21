#include <catch2/catch_test_macros.hpp>

// P0 placeholder proving the Catch2/CTest wiring works; real DSP tests land with
// each src/dsp class from P1 on.
TEST_CASE ("test harness is alive", "[sanity]")
{
    REQUIRE (1 + 1 == 2);
}
