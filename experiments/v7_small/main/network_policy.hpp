#pragma once
#include <cstdint>
namespace networking {
// True when two IPv4 prefixes overlap; works in either consistent byte order.
inline bool overlapping_subnets(uint32_t first,uint32_t first_mask,uint32_t second,uint32_t second_mask){
    return ((first^second)&first_mask&second_mask)==0;
}
}
