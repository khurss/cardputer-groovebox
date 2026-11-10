#include "gb/arrangement_ui.h"
#include "gb/ui_style.h"

#include <algorithm>
#include <cstdio>
namespace gb {
namespace {
Command command(CommandType type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint16_t value = 0) {
    Command cmd;
    cmd.type = type;
    cmd.a = a;
    cmd.b = b;
    cmd.c = c;
    cmd.value = value;
    return cmd;
}
int clamp(int v, int low, int high) { return std::max(low, std::min(v, high)); }
}  // namespace
void ArrangementUi::apply(Command c, Command inverse) {
    if (!sink_(context_, c))
        return;
    applyEdit(p_, c);
    undo_ = inverse;
    undoAvailable_ = true;
    ++edits_;
    dirty_ = true;
}
void ArrangementUi::undo() {
    if (undoAvailable_ && sink_(context_, undo_)) {
        applyEdit(p_, undo_);
        undoAvailable_ = false;
        ++edits_;
    }
}
void ArrangementUi::adjustMaster(int delta) {
    const int old = p_.masterVolume, next = clamp(old + delta, 0, 100);
    if (next != old)
        apply(command(CommandType::SetMasterVolume, 0, 0, 0, next),
              command(CommandType::SetMasterVolume, 0, 0, 0, old));
}
void ArrangementUi::onKey(uint8_t screen, char code, bool fn, bool shift) {
    dirty_ = true;
    if (screen == 2 || screen == 3) {
        bool& follow = screen == 2 ? followScenes_ : followSong_;
        if (code == 'f' && !fn) { follow = true; return; }
        // Browsing/editing preserves the user's cursor; a distant live row is pinned.
        const bool arrow = fn && (code == ';' || code == '.' || code == ',' || code == '/');
        if (arrow || code == '-' || code == '=' || code == key::Backspace ||
            (fn && (code == 'z' || code == 'c' || code == 'v' || code == key::Enter)) ||
            (screen == 2 && (code == '[' || code == ']' || (code >= '1' && code <= '6')))) follow = false;
    }
    if (fn && code == 'z') {
        undo();
        return;
    }
    if (screen == 2)
        sceneKey(code, fn);
    else if (screen == 3)
        songKey(code, fn);
    else if (screen == 4)
        mixerKey(code, fn, shift);
}
void ArrangementUi::sceneKey(char code, bool fn) {
    if ((fn && code == ';') || code == '[')
        scene_ = clamp(scene_ - 1, 0, kScenes - 1);
    else if ((fn && code == '.') || code == ']')
        scene_ = clamp(scene_ + 1, 0, kScenes - 1);
    else if (fn && code == ',')
        column_ = clamp(column_ - 1, 0, kTracks - 1);
    else if (fn && code == '/')
        column_ = clamp(column_ + 1, 0, kTracks - 1);
    else if (code >= '1' && code <= '6' && !fn)
        column_ = code - '1';
    else if (fn && code == 'c') {
        clipboard_ = p_.scenes[scene_];
        clipboardFull_ = true;
    } else if (fn && code == 'v' && clipboardFull_) {
        auto c = command(CommandType::SetScene, scene_), old = c;
        for (int t = 0; t < kTracks; ++t) {
            c.scenePatterns[t] = clipboard_.pattern[t];
            old.scenePatterns[t] = p_.scenes[scene_].pattern[t];
        }
        apply(c, old);
    } else if (code == '-' || code == '=' || code == key::Backspace) {
        const int previous = p_.scenes[scene_].pattern[column_];
        const int next = code == key::Backspace ? -1 : clamp(previous + (code == '=' ? 1 : -1), -1, 15);
        if (next != previous)
            apply(command(CommandType::SetScenePattern, scene_, column_, static_cast<uint8_t>(next)),
                  command(CommandType::SetScenePattern, scene_, column_, static_cast<uint8_t>(previous)));
    }
}
void ArrangementUi::songKey(char code, bool fn) {
    if (fn && code == ';')
        entry_ = clamp(entry_ - 1, 0, std::min<int>(p_.songLength, kSongEntries - 1));
    else if (fn && code == '.')
        entry_ = clamp(entry_ + 1, 0, std::min<int>(p_.songLength, kSongEntries - 1));
    else if (fn && (code == ',' || code == '/'))
        songField_ = code == '/' ? 1 : 0;
    else if (fn && code == key::Enter && p_.songLength < kSongEntries) {
        const uint8_t at = std::min(entry_, p_.songLength);
        apply(command(CommandType::InsertSongEntry, at, scene_, 0, 1), command(CommandType::DeleteSongEntry, at));
        entry_ = at;
    } else if (code == key::Backspace && entry_ < p_.songLength) {
        const auto old = p_.song[entry_];
        apply(command(CommandType::DeleteSongEntry, entry_),
              command(CommandType::InsertSongEntry, entry_, old.scene, 0, old.repeats));
        entry_ = std::min<uint8_t>(entry_, p_.songLength ? p_.songLength - 1 : 0);
    } else if ((code == '-' || code == '=') && entry_ < p_.songLength) {
        const auto old = p_.song[entry_];
        auto next = old;
        const int delta = code == '=' ? 1 : -1;
        if (songField_ == 0)
            next.scene = clamp(next.scene + delta, 0, kScenes - 1);
        else
            next.repeats = clamp(next.repeats + delta, 1, 255);
        apply(command(CommandType::SetSongEntry, entry_, next.scene, 0, next.repeats),
              command(CommandType::SetSongEntry, entry_, old.scene, 0, old.repeats));
    }
}
void ArrangementUi::mixerKey(char code, bool fn, bool shift) {
    if (fn && code == ',') {
        mix_ = clamp(mix_ - 1, 0, kTracks);
        return;
    }
    if (fn && code == '/') {
        mix_ = clamp(mix_ + 1, 0, kTracks);
        return;
    }
    if (code == '=' || code == '-' || (fn && (code == ';' || code == '.'))) {
        const int delta = (code == '=' || code == ';' ? 1 : -1) * (shift ? 1 : 5);
        const auto type = mix_ == 0 ? CommandType::SetMasterVolume : CommandType::SetTrackVolume;
        const uint8_t track = mix_ ? mix_ - 1 : 0;
        const int old = mix_ ? p_.mixer[track].volume : p_.masterVolume;
        apply(command(type, track, 0, 0, clamp(old + delta, 0, 100)), command(type, track, 0, 0, old));
    } else if (mix_ && (code == 'm' || code == 's')) {
        const uint8_t track = mix_ - 1;
        const bool old = code == 'm' ? p_.mixer[track].mute : p_.mixer[track].solo;
        const auto type = code == 'm' ? CommandType::SetTrackMute : CommandType::SetTrackSolo;
        apply(command(type, track, 0, 0, !old), command(type, track, 0, 0, old));
    }
}
ArrangementView ArrangementUi::view(uint8_t screen,bool playing) const {
    ArrangementView v;
    const int selected=screen==2?scene_:entry_;
    if(playing && screen==2) v.live=status_&15;
    else if(playing && screen==3 && (status_&(1u<<9)) && int((status_>>10)&63)<p_.songLength)
        v.live=(status_>>10)&63;
    v.following=v.live>=0 && (screen==2?followScenes_:followSong_);
    if(v.following) v.first=(v.live/6)*6;
    else if(v.live>=0) {
        v.first=(selected/5)*5;
        v.pinned=v.live<v.first || v.live>=v.first+5;
        if(v.pinned) v.count=5;
    } else v.first=(selected/6)*6;
    return v;
}
void ArrangementUi::draw(Canvas& c,uint8_t screen,bool playing) const {
    c.fillRect(0,0,240,135,color::Background);char text[64];
    constexpr const char* names[]={"MAIN","DRUM","T1","T2","T3","T4","SMP"};
    const auto viewport=view(screen,playing);
    const auto minimap=[&](int total,int selected,int live,int queued) {
        for(int i=0;i<total;++i) {
            const int x=4+i*232/total,w=std::max(1,(i+1)*232/total-i*232/total-1);
            const int scene=screen==2?i:p_.song[i].scene;
            c.fillRect(x,20,w,4,style::tracks[scene%6]);
            if(i==selected)c.fillRect(x,26,w,2,color::Text);
            if(i==queued)c.fillRect(x,24,w,1,color::StepAccent);
            if(i==live) { c.fillRect(x,17,w,2,color::Playhead);c.fillRect(x,20,w,4,color::Playhead); }
        }
    };
    if(screen==2){
        char state[20];snprintf(state,sizeof(state),playing?"PLAY %02u":"STOP",unsigned(status_&15)+1);
        snprintf(text,sizeof(text),"SCENES EDIT %02u",scene_+1);style::header(c,text,state,color::Playhead);
        minimap(kScenes,scene_,viewport.live,playing?int((status_>>4)&31)-1:-1);
        for(int t=0;t<kTracks;++t)c.text(43+t*32,31,names[t+1],style::tracks[t]);
        const auto row=[&](int index,int y,bool pinned) {
            const bool live=viewport.live==index,queued=playing && int((status_>>4)&31)==index+1;
            snprintf(text,sizeof(text),"%02d%c",index+1,live?'>':queued?'+':' ');c.text(4,y+2,text,live?color::Playhead:queued?color::StepAccent:color::Muted);
            for(int t=0;t<kTracks;++t){int x=41+t*32,pat=p_.scenes[index].pattern[t];bool active=index==scene_ && t==column_;
                c.fillRect(x,y,30,11,active?color::LaneSelected:color::Panel);
                if(active)style::frame(c,x,y,30,11,style::tracks[t]);
                if(pat<0)snprintf(text,sizeof(text),"--");else snprintf(text,sizeof(text),"%02d",pat+1);
                c.text(x+8,y+2,text,pat<0?color::Muted:style::tracks[t]);
            }
            if(live)c.fillRect(1,y,2,11,color::Playhead);
            if(pinned)c.fillRect(4,y+11,230,1,color::Playhead);
        };
        if(viewport.pinned)row(viewport.live,42,true);
        for(int r=0;r<viewport.count && viewport.first+r<kScenes;++r)
            row(viewport.first+r,42+(r+viewport.pinned)*12,false);
        style::footer(c,viewport.following?"FOLLOW  Arrows edit  Enter launch":"F follow  Arrows edit  Enter launch");
    }else if(screen==3){
        char state[20];if(viewport.live>=0)snprintf(state,sizeof(state),"PLAY %02u",unsigned(viewport.live)+1);else snprintf(state,sizeof(state),playing?"SCENE":"STOP");
        snprintf(text,sizeof(text),"SONG EDIT %02u/%u",entry_+1,p_.songLength);style::header(c,text,state,color::Playhead);
        minimap(p_.songLength,entry_,viewport.live,-1);
        const auto row=[&](int i,int y,bool pinned) {
            const bool selected=i==entry_,live=viewport.live==i;
            c.fillRect(3,y,234,13,selected?color::LaneSelected:color::Panel);
            snprintf(text,sizeof(text),"%02d%c",i+1,live?'>':' ');c.text(7,y+3,text,live?color::Playhead:color::Muted);
            if(i==p_.songLength){c.text(34,y+3,"+ ADD SCENE",color::StepAccent);return;}
            c.fillRect(32,y+3,3,8,style::tracks[p_.song[i].scene%6]);
            snprintf(text,sizeof(text),"SCENE %02u",p_.song[i].scene+1);c.text(41,y+3,text,selected&&!songField_?color::StepAccent:color::Text);
            snprintf(text,sizeof(text),"x%u",p_.song[i].repeats);c.text(173,y+3,text,selected&&songField_?color::StepAccent:color::Muted);
            if(live)c.fillRect(3,y,2,13,color::Playhead);
            if(pinned)c.fillRect(3,y+13,234,1,color::Playhead);
        };
        if(viewport.pinned)row(viewport.live,31,true);
        for(int r=0;r<viewport.count && viewport.first+r<kSongEntries && viewport.first+r<=p_.songLength;++r)
            row(viewport.first+r,31+(r+viewport.pinned)*14,false);
        if(!p_.songLength)c.text(9,66,"Opt > Actions > Insert scene",color::Muted);
        style::footer(c,viewport.following?"FOLLOW  Fn Enter add  Enter play":"F follow  Fn Enter add  Enter play");
    }else{
        style::header(c,"MIXER",playing?"PLAY":"STOP");
        for(int i=0;i<=kTracks;++i){
            const int x=i*34,volume=i?p_.mixer[i-1].volume:p_.masterVolume;const bool selected=i==mix_,mute=i&&p_.mixer[i-1].mute,solo=i&&p_.mixer[i-1].solo;
            const auto ink=mute?color::Muted:style::tracks[i];
            c.fillRect(x+1,19,32,99,selected?color::LaneSelected:color::Panel);
            if(selected)c.fillRect(x+1,19,32,2,ink);
            c.text(x+4,24,names[i],selected?color::Text:color::Muted);
            snprintf(text,sizeof(text),"%u",volume);c.text(x+7,36,text,ink);
            c.fillRect(x+15,50,3,46,color::StepOff);
            if(volume)c.fillRect(x+15,96-volume*46/100,3,std::max(1,volume*46/100),ink);
            for(int y=50;y<=96;y+=11)c.fillRect(x+6,y,5,1,color::StepOff);
            c.fillRect(x+10,94-volume*44/100,14,4,ink);
            c.fillRect(x+11,95-volume*44/100,12,1,color::Text);
            if(i){c.text(x+7,105,"M",mute?color::Warning:color::StepOff);c.text(x+21,105,"S",solo?color::StepAccent:color::StepOff);}
            else c.text(x+5,105,"VOL",color::Muted);
        }
        style::footer(c,"Arrows level  M mute S solo  Esc");
    }
}
}  // namespace gb
