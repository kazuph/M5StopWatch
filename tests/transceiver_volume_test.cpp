#include "../main/apps/app_transceiver/model/volume.h"
#include <cassert>

int main()
{
    using transceiver::volume::nextPreset;

    assert(nextPreset(0) == 25);
    assert(nextPreset(25) == 50);
    assert(nextPreset(50) == 100);
    assert(nextPreset(100) == 0);
    assert(nextPreset(80) == 100);
    assert(nextPreset(-1) == 0);
    assert(nextPreset(101) == 0);

    return 0;
}
