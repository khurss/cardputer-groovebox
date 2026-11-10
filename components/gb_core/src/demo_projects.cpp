#include "gb/demo_projects.h"
#include "gb/commands.h"
#include <algorithm>
#include <new>
namespace gb {
namespace {
void note(InstrumentPattern& p,int start,int length,int pitch,int velocity){
    if(p.noteCount<kMaxNotes)p.notes[p.noteCount++]={uint16_t(start),uint16_t(length),uint8_t(pitch),uint8_t(velocity)};
}
void hit(DrumPattern& p,int lane,int step,int velocity){p.velocity[lane][step]=velocity;}
}
const char* demoName(unsigned i){constexpr const char* names[]={"neon-drive","sunset-arcade","midnight-run"};return i<kDemoCount?names[i]:"";}
bool makeDemoProject(unsigned index,Project& p){
    if(index>=kDemoCount)return false;
    new (&p) Project{};initProject(p);p.masterVolume=5;
    constexpr int tempos[]={108,92,122};p.bpm=tempos[index];p.swing=index==1?54:50;
    // Root motion: A minor / F / C / G; D minor / Bb / F / C; E minor / C / G / D.
    constexpr int roots[3][4]={{33,29,36,31},{38,34,29,36},{28,24,31,26}};
    constexpr int thirds[]={3,4,4,4};
    p.synths[0]={Waveform::Square,1,34,76,8,45,24,12,86};
    p.synths[1]={Waveform::Saw,42,45,84,22,54,10,0,48};
    p.synths[2]={Waveform::Triangle,1,42,22,8,100,5,0,100};
    p.synths[3]={Waveform::Saw,4,32,90,15,84,22,26,76};
    if(index==1){p.synths[1].wave=Waveform::Triangle;p.synths[1].attack=55;p.synths[3].wave=Waveform::Sine;p.synths[3].level=100;p.synths[3].glide=38;}
    if(index==2){p.synths[0].wave=Waveform::Saw;p.synths[0].cutoff=53;p.synths[2].wave=Waveform::Square;p.synths[2].level=65;p.synths[3].resonance=42;}
    const int levels[]={86,82,58,66,74,100};for(int t=0;t<kTracks;++t)p.mixer[t].volume=levels[t];
    p.sounds[0]={48,72,46,108};p.sounds[1]={67,88,74,98};p.sounds[2]={58,72,72,75};p.sounds[3]={76,30,90,62};p.sounds[4]={62,55,82,55};p.sounds[5]={52,70,40,72};
    for(int pattern=0;pattern<4;++pattern){auto& d=p.drums[pattern];
        if(pattern==3){hit(d,3,2,40);hit(d,3,10,46);hit(d,6,12,52);continue;}
        for(int step=0;step<16;++step){
            if(step%4==0 && (pattern>0 || step==0 || step==8))hit(d,0,step,step%8?100:116);
            if(step==4 || step==12){hit(d,1,step,pattern?110:64);if(pattern==2)hit(d,2,step,80);}
            if(step%2==0)hit(d,3,step,step%4?65:47);
            if(pattern==2 && (step==6||step==14))hit(d,4,step,66);
        }
        if(index==2 && pattern>0){hit(d,0,10,88);hit(d,0,15,70);}
        if(pattern==2){hit(d,1,15,60);hit(d,5,14,65);}
    }
    for(int chord=0;chord<4;++chord){const int root=roots[index][chord],third=thirds[chord];
        for(int variant=0;variant<2;++variant){const int pat=chord+variant*4;
            auto& bass=p.instruments[0][pat];auto& pad=p.instruments[1][pat];auto& arp=p.instruments[2][pat];
            for(int step=0;step<16;step+=2){int offset=step==14?7:((step==6||step==10)&&variant)?12:0;note(bass,step*24,variant?36:30,root+offset,step%4?76:102);}
            note(pad,0,380,root+24+third,variant?70:56);note(pad,0,380,root+31,variant?64:51);
            const int tones[]={0,third,7,12,7,third,12,7};
            for(int step=0;step<16;step+=(index==1?2:1)){const int degree=(step+chord+(variant?2:0))%8;note(arp,step*24,index==1?32:15,root+36+tones[degree],step%4?60:86);}
        }
        // Distinct lead and breakdown phrases, rather than a sustained test chord.
        const int melodyA[]={7,12,14,12,7,5,third,0};
        const int melodyB[]={12,7,third,7,14,12,7,third};
        for(int k=0;k<8;++k){int pitch=root+36+(index==1?melodyB[k]:melodyA[(k+chord)%8]);note(p.instruments[3][chord],k*48,k==7?45:38,pitch,k%2?82:98);}
        note(p.instruments[3][chord+4],24,120,root+48,66);note(p.instruments[3][chord+4],216,150,root+43,58);
        for(int k=0;k<4;++k)note(p.instruments[2][chord+8],k*96,40,root+36+(k%2?7:third),50);
        // Intro, verse, chorus, breakdown. Every scene is one bar; repetitions
        // below determine phrase length and keep editing examples approachable.
        p.scenes[chord].pattern[0]=0;p.scenes[chord].pattern[2]=chord;p.scenes[chord].pattern[3]=chord+8;
        p.scenes[chord+4].pattern[0]=1;p.scenes[chord+4].pattern[1]=chord;p.scenes[chord+4].pattern[2]=chord;p.scenes[chord+4].pattern[3]=chord;
        p.scenes[chord+8].pattern[0]=2;p.scenes[chord+8].pattern[1]=chord+4;p.scenes[chord+8].pattern[2]=chord+4;p.scenes[chord+8].pattern[3]=chord+4;p.scenes[chord+8].pattern[4]=chord;
        p.scenes[chord+12].pattern[0]=3;p.scenes[chord+12].pattern[2]=chord;p.scenes[chord+12].pattern[3]=chord+8;p.scenes[chord+12].pattern[4]=chord+4;
    }
    const int sections[]={0,4,4,8,12,8,0};const int repeats[]={2,4,2,4,2,4,2};
    for(int section=0;section<7;++section)for(int chord=0;chord<4;++chord)p.song[p.songLength++]={uint8_t(sections[section]+chord),uint8_t(repeats[section])};
    p.song[p.songLength++]={12,2};
    // Final cadence: dedicated last intro scene retains the final harmony for
    // two bars, then the sequencer ends the song (no automatic loop).
    return true;
}
}
