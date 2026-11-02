#pragma once
#include "sr_settings.h"
#include "nr_settings.h"
#include "fg_settings.h"
#include <filesystem>
namespace ac7 {
SrSettings loadSrSettings(const std::filesystem::path&,unsigned&invalid,SrSettings current={});
NrSettings loadNrSettings(const std::filesystem::path&,unsigned&invalid,const NrSettings&current={});
FgSettings loadFgSettings(const std::filesystem::path&,unsigned&invalid,FgSettings current={});
}
