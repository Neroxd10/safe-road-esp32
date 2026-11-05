#include "../main/network_policy.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main(){
 using networking::overlapping_subnets;
 constexpr unsigned hotspot=0xc0a80401,mask=0xffffff00;
 assert(overlapping_subnets(hotspot,mask,0xc0a80442,mask));
 assert(!overlapping_subnets(hotspot,mask,0xc0a80142,mask));
 assert(overlapping_subnets(hotspot,mask,0xc0a80142,0xffff0000));
 assert(overlapping_subnets(hotspot,0xffff0000,0xc0a80142,mask));
 assert(!overlapping_subnets(hotspot,mask,0x0a000042,mask));
 assert(!overlapping_subnets(hotspot,mask,0xc0a80501,mask));
 assert(!overlapping_subnets(hotspot,mask,0xc0a803ff,mask));
 assert(overlapping_subnets(hotspot,mask,0xc0a804ff,0xffffffff));
 std::cout<<"PASS: distinct, same, nested and boundary IPv4 subnet checks\n";
}
