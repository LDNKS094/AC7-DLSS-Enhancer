#pragma once

namespace ac7 {
struct FgSettings {
    // shortcut: menu/pause state is not mapped; request FG only in gameplay until native state integration.
    bool enabled=false;
    unsigned multiplier=0;
    bool operator==(const FgSettings&other)const{return enabled==other.enabled&&multiplier==other.multiplier;}
};
}
