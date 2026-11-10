#include "gb/arrangement_ui.h"
#include "test.h"
#include <cstring>
#include <string>
#include <vector>
using namespace gb;
namespace {
struct Screen : Canvas {
    struct Text { int x,y;std::string value;uint16_t ink; };
    struct Rect { int x,y,w,h;uint16_t ink; };
    std::vector<Text> labels;std::vector<Rect> rectangles;bool valid=true;
    void fillRect(int x,int y,int w,int h,uint16_t ink) override {
        valid &= x>=0 && y>=0 && w>0 && h>0 && x+w<=240 && y+h<=135;
        rectangles.push_back({x,y,w,h,ink});
    }
    void text(int x,int y,const char* value,uint16_t ink) override {
        valid &= x>=0 && y>=0 && x+int(strlen(value))*6<=240 && y+8<=135;
        labels.push_back({x,y,value,ink});
    }
    bool liveRow(int row) const {
        char text[8];snprintf(text,sizeof(text),"%02d>",row+1);
        for(const auto& label:labels)if(label.value==text && label.y>=31 && label.ink==color::Playhead)return true;
        return false;
    }
    bool playMarker(int row,int count) const {
        const int x=4+row*232/count;
        for(const auto& r:rectangles)if(r.x==x && r.y==17 && r.ink==color::Playhead)return true;
        return false;
    }
};
bool sink(void* context,const Command&) { ++*static_cast<int*>(context);return true; }
uint32_t status(int scene,int row=0,bool song=true) { return scene|(song?1u<<9:0)|(row<<10)|(1u<<16); }
}
TEST(arrangementFollowsActualSongRowsAndSceneJumpsWithoutEditingSelection) {
    Project p;initProject(p);p.songLength=64;
    for(int i=0;i<64;++i)p.song[i]={uint8_t((i*7)%16),1};
    const auto before=p;int commands=0;ArrangementUi ui(p,sink,&commands);
    for(int i=0;i<64;++i) {
        ui.setStatus(status(p.song[i].scene,i));
        for(int screen: {2,3}) {
            const int live=screen==2?p.song[i].scene:i;const auto v=ui.view(screen,true);
            CHECK_EQ(v.following,true);CHECK_EQ(v.pinned,false);
            CHECK_EQ(live>=v.first && live<v.first+v.count,true);
            Screen c;ui.draw(c,screen,true);CHECK_EQ(c.valid,true);CHECK_EQ(c.liveRow(live),true);
            CHECK_EQ(c.playMarker(live,screen==2?16:64),true);
        }
    }
    CHECK_EQ(ui.scene(),0);CHECK_EQ(ui.songEntry(),0);CHECK_EQ(commands,0);
    CHECK_EQ(memcmp(&before,&p,sizeof(p)),0);
}
TEST(browsingKeepsThePlayingRowPinnedAndFollowDoesNotMoveTheEditCursor) {
    Project p;initProject(p);p.songLength=64;int commands=0;ArrangementUi ui(p,sink,&commands);
    ui.setStatus(status(15,63));
    for(int screen: {2,3}) {
        for(int selected=0;selected<(screen==2?16:64);++selected) {
            ui.onKey(screen,selected?'.':',',true,false);
            const auto v=ui.view(screen,true);
            CHECK_EQ(v.following,false);CHECK_EQ(selected>=v.first && selected<v.first+v.count,true);
            Screen c;ui.draw(c,screen,true);CHECK_EQ(c.valid,true);CHECK_EQ(c.liveRow(screen==2?15:63),true);
        }
    }
    ui.setStatus(status(0,0));
    CHECK_EQ(ui.view(2,true).pinned,true);CHECK_EQ(ui.view(3,true).pinned,true);
    ui.onKey(2,'f',false,false);ui.onKey(3,'f',false,false);
    CHECK_EQ(ui.view(2,true).following,true);CHECK_EQ(ui.view(3,true).following,true);
    CHECK_EQ(ui.scene(),15);CHECK_EQ(ui.songEntry(),63);CHECK_EQ(commands,0);
    ui.setStatus(0);
    CHECK_EQ(ui.view(2,false).live,-1);CHECK_EQ(ui.view(3,false).live,-1);
    CHECK_EQ(ui.view(2,false).first<=15,true);CHECK_EQ(ui.view(3,false).first,60);
    ui.onKey(3,';',true,false);ui.setStatus(status(7,8));
    CHECK_EQ(ui.view(3,true).following,true);CHECK_EQ(ui.songEntry(),62);
}
TEST(queuedSceneDoesNotReplacePlayingSceneAndSceneLoopDoesNotFakeSongPosition) {
    Project p;initProject(p);p.songLength=0;int commands=0;ArrangementUi ui(p,sink,&commands);
    ui.setStatus(status(2,0,false)|(16u<<4));
    Screen scenes;ui.draw(scenes,2,true);
    CHECK_EQ(scenes.liveRow(2),true);CHECK_EQ(scenes.playMarker(2,16),true);
    CHECK_EQ(scenes.playMarker(15,16),false);
    bool queued=false;for(const auto& r:scenes.rectangles)queued|=r.x==4+15*232/16 && r.y==24 && r.ink==color::StepAccent;
    CHECK_EQ(queued,true);
    Screen song;ui.draw(song,3,true);CHECK_EQ(song.valid,true);CHECK_EQ(ui.view(3,true).live,-1);
    ui.setStatus(status(15,0,false));Screen next;ui.draw(next,2,true);
    CHECK_EQ(next.liveRow(15),true);CHECK_EQ(next.playMarker(15,16),true);
}
