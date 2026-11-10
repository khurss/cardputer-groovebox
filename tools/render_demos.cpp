// Render full original demo arrangements at the same 5% ceiling as hardware tests.
#include "gb/demo_projects.h"
#include "gb/engine.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <cstdint>
void u16(FILE* f,uint16_t n){fputc(n&255,f);fputc(n>>8,f);}void u32(FILE* f,uint32_t n){u16(f,n);u16(f,n>>16);}
int main(int argc,char** argv){const char* dir=argc>1?argv[1]:"build/demo-audio";std::filesystem::create_directories(dir);static gb::Project p;
 for(unsigned demo=0;demo<gb::kDemoCount;++demo){gb::makeDemoProject(demo,p);gb::Engine e(p,32000);gb::Command cmd;cmd.type=gb::CommandType::SetOutputLimit;cmd.value=5;e.handle(cmd);cmd.type=gb::CommandType::PlaySong;cmd.a=0;e.handle(cmd);
 char file[160];snprintf(file,sizeof(file),"%s/%s.wav",dir,gb::demoName(demo));FILE* f=fopen(file,"wb");if(!f)return 1;
 fwrite("RIFF",1,4,f);u32(f,0);fwrite("WAVEfmt ",1,8,f);u32(f,16);u16(f,1);u16(f,1);u32(f,32000);u32(f,64000);u16(f,2);u16(f,16);fwrite("data",1,4,f);u32(f,0);
 int16_t block[512];uint32_t frames=0,tail=0;int peak=0;uint64_t energy=0;
 const uint32_t limit=32000*240*83/p.bpm;
 while(frames<limit && (e.playing() || tail<32000)){bool playing=e.playing();e.render(block,256);if(!playing)tail+=256;
 for(int i=0;i<256;++i){int value=block[i*2];peak=std::max(peak,std::abs(value));energy+=int64_t(value)*value;u16(f,block[i*2]);}frames+=256;}
 fseek(f,4,SEEK_SET);u32(f,36+frames*2);fseek(f,40,SEEK_SET);u32(f,frames*2);const bool ok=fclose(f)==0;
 printf("%s duration=%.2fs peak=%d mean_power=%llu events_overflow=%u\n",file,frames/32000.,peak,(unsigned long long)(energy/frames),e.eventOverflows());
 if(!ok || peak>1557 || !energy || e.eventOverflows() || e.playing())return 1;
 }return 0;}
