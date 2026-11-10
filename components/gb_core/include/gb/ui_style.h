#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include "gb/ui_types.h"
namespace gb::style {
constexpr uint16_t tracks[] = {0x07ff, 0xf81f, 0xffc0, 0x07f0, 0x7bff, 0xfd20, 0x07ff};
inline void line(Canvas& c, int x0, int y0, int x1, int y1, uint16_t ink) {
    const int dx=std::abs(x1-x0), sx=x0<x1?1:-1, dy=-std::abs(y1-y0), sy=y0<y1?1:-1;
    int error=dx+dy;
    for (;;) {
        if(x0>=0 && x0<240 && y0>=0 && y0<135)c.fillRect(x0,y0,1,1,ink);
        if(x0==x1 && y0==y1)break;
        int e=error*2; if(e>=dy){error+=dy;x0+=sx;} if(e<=dx){error+=dx;y0+=sy;}
    }
}
inline void frame(Canvas& c,int x,int y,int w,int h,uint16_t ink) {
    c.fillRect(x,y,w,1,ink);c.fillRect(x,y+h-1,w,1,ink);c.fillRect(x,y,1,h,ink);c.fillRect(x+w-1,y,1,h,ink);
}
inline void header(Canvas& c,const char* title,const char* status,uint16_t accent=color::StepOn) {
    c.fillRect(0,0,240,16,color::Panel);c.fillRect(0,0,3,16,accent);
    c.text(7,4,title,color::Text);
    if(status)c.text(237-int(std::char_traits<char>::length(status))*6,4,status,accent);
}
inline void footer(Canvas& c,const char* hint) {
    c.fillRect(0,122,240,13,color::Panel);c.text(3,125,hint,color::Muted);
}
inline void badge(Canvas& c,int x,int y,const char* text,uint16_t ink) {
    int w=int(std::char_traits<char>::length(text))*6+6;c.fillRect(x,y,w,11,color::Panel);c.text(x+3,y+2,text,ink);
}
}
