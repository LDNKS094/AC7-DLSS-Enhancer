#pragma once
namespace ac7 {
// Edge-triggered chord; sampling while unfocused must not toggle the mode.
struct NrHotkey {
    bool enabled=true,held=false;
    bool pressed(bool down,bool foreground){const bool hit=down&&!held&&foreground;held=down;return hit;}
    bool update(bool down,bool foreground){const bool hit=pressed(down,foreground);if(hit)enabled=!enabled;return hit;}
};
}
