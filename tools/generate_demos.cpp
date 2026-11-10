#include "gb/demo_projects.h"
#include "gb/project_io.h"
#include "gb/validate.h"
#include <cstdio>
#include <filesystem>
int main(int argc,char** argv){
    const char* destination=argc>1?argv[1]:"examples/projects";
    std::filesystem::create_directories(destination);
    static gb::Project project;static uint8_t buffer[gb::kProjectBufferSize];
    for(unsigned i=0;i<gb::kDemoCount;++i){
        if(!gb::makeDemoProject(i,project)||!gb::isValidProject(project))return 1;
        char path[96];snprintf(path,sizeof(path),"%s/%s.gbp",destination,gb::demoName(i));
        const auto size=gb::serializeProject(project,buffer,sizeof(buffer));FILE* f=fopen(path,"wb");if(!f)return 1;
        const bool ok=fwrite(buffer,1,size,f)==size;const bool closed=fclose(f)==0;if(!ok||!closed)return 1;
        unsigned bars=0;for(unsigned row=0;row<project.songLength;++row)bars+=project.song[row].repeats;
        printf("%s: %zu bytes, %u BPM, %u song entries, %u bars, master 5%%\n",path,size,project.bpm,project.songLength,bars);
    }
}
