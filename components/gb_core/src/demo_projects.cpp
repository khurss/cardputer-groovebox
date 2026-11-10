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
void danceDrums(DrumPattern& d,unsigned style,int phrase,int section) {
    const bool dnb=style>=2, drop=section==1, intro=section==2, breakdown=section==3;
    if (breakdown) {
        hit(d,6,3,42);hit(d,3,10,38);hit(d,7,14,46);
        if(phrase==3) { hit(d,1,12,55);hit(d,1,14,70);hit(d,1,15,90); }
        return;
    }
    for(int step=0;step<16;++step) {
        if(step%2==0 || (drop && (step+phrase)%4==3))
            hit(d,3,step,step%4==2?76:step%2?38:50);
        if(step==4 || step==12) {
            if(!intro || phrase>=2) hit(d,1,step,dnb?116:82);
            if(!dnb && drop) hit(d,2,step,64);
        }
    }
    if(!dnb) {
        for(int step=0;step<16;step+=4) hit(d,0,step,step==0?124:112);
        if(!intro) for(int step=2;step<16;step+=4) hit(d,4,step,drop?72:54);
        if(style==1 && !intro) {
            hit(d,5,3,67);hit(d,5,10,78);hit(d,7,7,62);hit(d,6,15,72);
            if(drop) { hit(d,5,14,85);hit(d,7,11,68); }
        } else if(drop) { hit(d,6,3+phrase*2,64);hit(d,7,11,58); }
    } else {
        // Broken kick placement, backbeats on 2/4 and low-velocity snare ghosts.
        hit(d,0,0,120);
        if(!intro) {
            hit(d,0,style==2?6:7,104);hit(d,0,10,phrase%2?96:111);
            if(phrase%2) hit(d,0,15,88);
            hit(d,1,3,36);hit(d,1,11,42);
            if(drop) { hit(d,1,9,43);hit(d,1,14,52);hit(d,4,6,60); }
            if(style==3) { hit(d,6,2,45);hit(d,7,14,48); }
        }
    }
    if(phrase==3 && !intro) { hit(d,1,14,74);hit(d,1,15,96);hit(d,5,13,66); }
}
void makeDanceProject(unsigned style,Project& p) {
    constexpr int tempos[]={132,140,174,170};p.bpm=tempos[style];
    p.swing=style==0?52:style==3?51:50;
    constexpr int roots[4][4]={{33,33,36,31},{29,29,25,27},{38,38,34,36},{31,27,34,29}};
    constexpr int thirds[4][4]={{3,3,4,4},{3,3,4,4},{3,3,4,4},{3,4,4,4}};
    // At most five written synth voices: bass, two-note stab/pad, hook, response.
    p.synths[0]={Waveform::Saw,0,32,32,3,54,82,18,95};
    p.synths[1]={Waveform::Square,0,27,16,5,55,24,0,58};
    p.synths[2]={Waveform::Triangle,0,22,10,4,97,8,0,74};
    p.synths[3]={Waveform::Saw,2,30,32,7,74,38,16,58};
    if(style==1) {
        p.synths[0]={Waveform::Square,0,25,48,3,38,45,8,94};
        p.synths[1]={Waveform::Saw,0,19,8,3,73,51,0,53};
        p.synths[2]={Waveform::Sine,0,20,12,4,100,0,0,90};
        p.synths[3]={Waveform::Triangle,6,44,70,12,62,16,28,60};
    } else if(style==2) {
        p.synths[0]={Waveform::Saw,0,34,86,4,48,52,22,96};
        p.synths[1]={Waveform::Square,3,28,28,6,43,22,0,50};
        p.synths[2]={Waveform::Triangle,0,24,14,4,91,18,0,76};
        p.synths[3]={Waveform::Saw,1,25,62,5,68,36,30,58};
    } else if(style==3) {
        p.synths[0]={Waveform::Sine,1,34,96,5,100,0,12,112};
        p.synths[1]={Waveform::Triangle,18,42,72,10,76,8,0,63};
        p.synths[2]={Waveform::Triangle,1,37,24,7,100,5,0,87};
        p.synths[3]={Waveform::Sine,3,32,80,8,100,0,28,88};
    }
    const int levels[]={92,86,56,64,62,100};
    for(int t=0;t<kTracks;++t)p.mixer[t].volume=levels[t];
    p.sounds[0]={uint8_t(style>=2?55:43),uint8_t(style>=2?45:66),60,112};
    p.sounds[1]={uint8_t(style>=2?75:64),uint8_t(style>=2?49:58),88,108};
    p.sounds[2]={61,48,76,62};p.sounds[3]={81,22,96,63};
    p.sounds[4]={70,40,86,58};p.sounds[5]={43,40,55,70};
    p.sounds[6]={73,28,74,65};p.sounds[7]={55,41,68,54};
    for(int t=0;t<kInstrumentTracks;++t) {
        p.keyboardLayout[t]=1;p.keyboardRoot[t]=roots[style][0]%12;p.keyboardScale[t]=2;
    }
    for(int phrase=0;phrase<4;++phrase) {
        const int root=roots[style][phrase],third=thirds[style][phrase];
        for(int section=0;section<4;++section) {
            const int pat=phrase+section*4;
            const bool drop=section==1,intro=section==2,breakdown=section==3;
            danceDrums(p.drums[pat],style,phrase,section);
            auto& bass=p.instruments[0][pat];auto& chord=p.instruments[1][pat];
            auto& hook=p.instruments[2][pat];auto& lead=p.instruments[3][pat];
            if(!intro && !breakdown) {
                if(style==0) {
                    const int steps[]={0,2,3,6,8,10,11,14};const int offsets[]={0,0,12,7,0,3,12,7};
                    for(int n=0;n<8;++n) {
                        const int degree=offsets[(n+phrase)%8];
                        note(bass,steps[n]*24,n==2||n==6?30:18,root+(degree==3?third:degree),n%3?83:113);
                    }
                    if(drop) { note(bass,5*24,18,root+12,86);note(bass,13*24,18,root+7,95); }
                } else if(style==1) {
                    const int steps[]={0,3,6,8,11,14},offsets[]={0,0,7,0,12,7};
                    for(int n=0;n<6;++n)note(bass,steps[n]*24,30,root+offsets[(n+phrase)%6],n%2?92:109);
                    if(drop)note(bass,15*24,20,root+12,94);
                } else {
                    const int steps[2][6]={{0,3,6,8,10,14},{0,3,7,10,12,15}};
                    const int offsets[2][6]={{0,0,7,12,0,10},{0,7,0,12,7,0}};
                    for(int n=0;n<6;++n) {
                        const int start=steps[style-2][n]*24;
                        const int end=n==5?384:steps[style-2][n+1]*24;
                        note(bass,start,end-start-4,root+offsets[style-2][(n+phrase)%6],n%2?91:112);
                    }
                }
            }
            if(style==3 || breakdown) {
                note(chord,0,372,root+24+third,breakdown?62:54);
                note(chord,0,372,root+31,breakdown?54:48);
            } else {
                const int steps[]={2,6,10,14};
                for(int n=0;n<4;++n) {
                    if(intro && n%2)continue;
                    note(chord,steps[n]*24,style==1?18:30,root+24+third,drop?75:56);
                    note(chord,steps[n]*24,style==1?18:30,root+31,drop?65:48);
                }
            }
            const int motifs[4][8]={{0,7,12,3,7,10,12,7},{0,0,7,0,12,7,3,7},
                                   {0,7,10,12,7,3,5,7},{7,12,14,12,7,3,5,7}};
            for(int n=0;n<8;++n) {
                if((intro || breakdown) && n%2)continue;
                if(!drop && !intro && !breakdown && n%2==1)continue;
                const int degree=motifs[style][(n+phrase)%8];
                note(hook,n*48+(style==1?12:0),style==3?36:18,
                     root+36+(degree==3?third:degree),n%2?58:76);
            }
            if(drop) {
                const int steps[]={1,5,9,13},offsets[]={12,7,third,10};
                for(int n=0;n<4;++n)note(lead,steps[n]*24,style==3?60:36,root+36+offsets[(n+phrase)%4],n%2?69:82);
            } else if(breakdown) {
                note(lead,24,132,root+43,62);note(lead,216,144,root+36+third,58);
            }
        }
        // Scene banks: intro, groove/build, drop, breakdown. All are one bar.
        for(int bank=0;bank<4;++bank) {
            const int sections[]={2,0,1,3};const int pat=phrase+sections[bank]*4;
            auto& scene=p.scenes[phrase+bank*4];scene.pattern[0]=pat;
            scene.pattern[2]=pat;scene.pattern[3]=pat;
            if(bank==1 || bank==2)scene.pattern[1]=pat;
            if(bank>=2)scene.pattern[4]=pat;
        }
    }
    const int sections[]={0,4,4,8,12,8,0};
    const int technoRepeats[]={4,4,2,6,2,6,3},dnbRepeats[]={4,4,4,8,4,8,3};
    for(int section=0;section<7;++section)for(int phrase=0;phrase<4;++phrase)
        p.song[p.songLength++]={uint8_t(sections[section]+phrase),uint8_t(style>=2?dnbRepeats[section]:technoRepeats[section])};
    p.song[p.songLength++]={12,4};
}
}
const char* demoName(unsigned i){constexpr const char* names[]={"neon-drive","sunset-arcade","midnight-run","circuit-pressure","concrete-pulse","night-transit","liquid-signal"};static_assert(sizeof(names)/sizeof(names[0])==kDemoCount);return i<kDemoCount?names[i]:"";}
bool makeDemoProject(unsigned index,Project& p){
    if(index>=kDemoCount)return false;
    new (&p) Project{};initProject(p);p.masterVolume=5;
    if(index>=3) { makeDanceProject(index-3,p);return true; }
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
