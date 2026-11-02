#pragma once

namespace ac7 {
struct FgSettings {
    // User intent is retained while the native game state temporarily prevents generation.
    bool enabled=false;
    // Auto targets game FPS cap (or refresh rate); fixed multipliers retain native frame limiting.
    unsigned multiplier=0;
    bool operator==(const FgSettings&other)const{return enabled==other.enabled&&multiplier==other.multiplier;}
};
}
