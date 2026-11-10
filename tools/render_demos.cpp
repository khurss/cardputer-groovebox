// Full arrangements and 20-second drop previews, always at a 5% output ceiling.
#include "gb/demo_projects.h"
#include "gb/engine.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cstdint>

namespace {
void u16(FILE* f,uint16_t n) { fputc(n&255,f);fputc(n>>8,f); }
void u32(FILE* f,uint32_t n) { u16(f,n);u16(f,n>>16); }
bool render(gb::Project& p,const std::filesystem::path& file,bool preview) {
    gb::Engine e(p,32000);
    gb::Command cmd;cmd.type=gb::CommandType::SetOutputLimit;cmd.value=5;e.handle(cmd);
    cmd.type=gb::CommandType::PlaySong;cmd.a=preview?12:0;e.handle(cmd);
    FILE* f=fopen(file.c_str(),"wb");if(!f)return false;
    fwrite("RIFF",1,4,f);u32(f,0);fwrite("WAVEfmt ",1,8,f);u32(f,16);
    u16(f,1);u16(f,1);u32(f,32000);u32(f,64000);u16(f,2);u16(f,16);
    fwrite("data",1,4,f);u32(f,0);
    int16_t block[512];uint32_t frames=0,tail=0;int peak=0;uint64_t energy=0;
    // Ten minutes is a failure guard, independent of a particular demo's length.
    const uint32_t limit=32000*(preview?20:600);
    while(frames<limit && (preview || e.playing() || tail<32000)) {
        const bool playing=e.playing();
        const auto count=std::min(256u,limit-frames);e.render(block,count);
        if(!playing)tail+=count;
        for(unsigned i=0;i<count;++i) {
            int value=block[i*2];peak=std::max(peak,std::abs(value));energy+=int64_t(value)*value;
            // Fade only the preview's edges; never normalize or raise output.
            if(preview) {
                const uint32_t remaining=limit-1-(frames+i);
                const unsigned fade=std::min({1600u,frames+i,remaining});
                value=value*int(fade)/1600;
            }
            u16(f,value);
        }
        frames+=count;
    }
    fseek(f,4,SEEK_SET);u32(f,36+frames*2);fseek(f,40,SEEK_SET);u32(f,frames*2);
    const bool ioOk=!ferror(f);const bool closed=fclose(f)==0;
    printf("%s duration=%.2fs peak=%d mean_power=%llu events_overflow=%u\n",
           file.c_str(),frames/32000.,peak,(unsigned long long)(energy/std::max(1u,frames)),e.eventOverflows());
    return ioOk && closed && peak<=1557 && energy && !e.eventOverflows() && (preview || !e.playing());
}
}
int main(int argc,char** argv) {
    const std::filesystem::path full=argc>1?argv[1]:"build/demo-audio";
    const std::filesystem::path previews=argc>2?argv[2]:"build/demo-previews";
    const unsigned first=argc>3?std::strtoul(argv[3],nullptr,10):0;
    if(first>=gb::kDemoCount)return 1;
    std::filesystem::create_directories(full);std::filesystem::create_directories(previews);
    static gb::Project p;
    for(unsigned demo=first;demo<gb::kDemoCount;++demo) {
        if(!gb::makeDemoProject(demo,p))return 1;
        const auto name=std::string(gb::demoName(demo))+".wav";
        if(!render(p,full/name,false) || !render(p,previews/name,true))return 1;
    }
}
