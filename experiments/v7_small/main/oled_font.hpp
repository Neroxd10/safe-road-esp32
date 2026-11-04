#pragma once
#include <cstdint>
namespace hardware {
inline void glyph(char c,uint8_t *out){
 switch(c){
 case 'A': {const uint8_t v[5]={126,9,9,9,126};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'B': {const uint8_t v[5]={127,73,73,73,54};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'C': {const uint8_t v[5]={62,65,65,65,65};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'D': {const uint8_t v[5]={127,65,65,65,62};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'E': {const uint8_t v[5]={127,73,73,73,65};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'F': {const uint8_t v[5]={127,9,9,9,1};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'G': {const uint8_t v[5]={62,65,73,73,121};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'H': {const uint8_t v[5]={127,8,8,8,127};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'I': {const uint8_t v[5]={65,65,127,65,65};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'J': {const uint8_t v[5]={48,64,65,63,1};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'K': {const uint8_t v[5]={127,8,20,34,65};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'L': {const uint8_t v[5]={127,64,64,64,64};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'M': {const uint8_t v[5]={127,2,12,2,127};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'N': {const uint8_t v[5]={127,2,4,8,127};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'O': {const uint8_t v[5]={62,65,65,65,62};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'P': {const uint8_t v[5]={127,9,9,9,6};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'Q': {const uint8_t v[5]={62,65,81,33,94};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'R': {const uint8_t v[5]={127,9,25,41,70};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'S': {const uint8_t v[5]={70,73,73,73,49};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'T': {const uint8_t v[5]={1,1,127,1,1};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'U': {const uint8_t v[5]={63,64,64,64,63};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'V': {const uint8_t v[5]={31,32,64,32,31};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'W': {const uint8_t v[5]={63,64,56,64,63};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'X': {const uint8_t v[5]={99,20,8,20,99};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'Y': {const uint8_t v[5]={3,4,120,4,3};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case 'Z': {const uint8_t v[5]={97,81,73,69,67};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '0': {const uint8_t v[5]={62,81,73,69,62};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '1': {const uint8_t v[5]={0,66,127,64,0};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '2': {const uint8_t v[5]={66,97,81,73,70};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '3': {const uint8_t v[5]={65,73,73,73,54};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '4': {const uint8_t v[5]={24,20,18,127,16};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '5': {const uint8_t v[5]={79,73,73,73,49};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '6': {const uint8_t v[5]={62,73,73,73,48};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '7': {const uint8_t v[5]={1,113,9,5,3};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '8': {const uint8_t v[5]={54,73,73,73,54};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '9': {const uint8_t v[5]={6,73,73,73,62};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case ':': {const uint8_t v[5]={0,0,54,0,0};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '-': {const uint8_t v[5]={8,8,8,8,8};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case '/': {const uint8_t v[5]={64,48,8,6,1};for(int i=0;i<5;i++)out[i]=v[i];break;}
 case ' ': {const uint8_t v[5]={0,0,0,0,0};for(int i=0;i<5;i++)out[i]=v[i];break;}
 default:for(int i=0;i<5;i++)out[i]=0;
 }
}
}
