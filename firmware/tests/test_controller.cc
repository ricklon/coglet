#include <array>
#include <vector>
#include <cmath>
#include <mutex>
#include <string>
#include <fake_idf.h>
#include "cJSON.h"
#define private public
#include "coglet_controller.h"
#undef private
#include <cassert>
#include <iostream>
#include <functional>
#include <fstream>
#include <cstdint>

static void Reject(const std::function<void()>& f) {
    bool rejected=false; try {f();} catch(...) {rejected=true;} assert(rejected);
}
int main(int argc, char** argv) {
    auto& c=CogletController::Instance();
    // Matches the S3 profile wiring that fake_idf.h accepts: SDA D4, SCL D5.
    c.Start({I2C_NUM_1, GPIO_NUM_5, GPIO_NUM_6, GPIO_NUM_9, 0x40});
    assert(c.ready_ && c.released_ && fake_oe==1);
    // Warm-start sequence disables outputs before MODE1 writes; no PWM pose.
    assert(fake_writes.front()==std::vector<uint8_t>({0xfd,0x10}));
    for (const auto& w:fake_writes) assert(w.size()==2);
    Reject([&]{c.Command("gaze 0 0");});
    Reject([&]{c.Command("engage");});
    Reject([&]{c.Command("engage");});
    // Roles bottom-up; index 5 is the shared lid servo, whose window runs
    // backwards (closed to open).
    const char* roles[]={"base","neck_tilt","neck_roll","eye_pan","eye_tilt",
                         "lids","jaw","ear_left","ear_right"};
    for(int i=0;i<9;++i) {
        auto cmd="configure "+std::string(roles[i])+" "+std::to_string(i)+(i==5?" 90 20":" 40 140")+" 1000 2000 0 1";
        c.Command(cmd);
    }
    Reject([&]{c.Command("configure jaw 0 40 140 1000 2000 0 1");});
    Reject([&]{c.Command("configure jaw 6 nan 140 1000 2000 0 1");});
    Reject([&]{c.Command("configure jaw 6 40 140 1000 2000 2000 1");});
    c.Command("save"); c.cal_.axes[0].channel=-1; c.Load(); assert(c.Valid(c.cal_,true));
    c.Command("engage");
    for(float a:c.commanded_) assert(std::isnan(a));
    fake_writes.clear(); c.Tick(100000); assert(fake_writes.empty()); // engage doesn't blink/move
    Reject([&]{c.Command("blink");});
    Reject([&]{c.Command("gaze 101 0");});
    c.Command("gaze 0 -100");
    assert(std::abs(c.commanded_[5]-(90+(20-90)*.925f*.2f))<.001f);
    // The neck, jaw and ears have no choreography and must stay put.
    for(int i:{0,1,2,6,7,8}) assert(std::isnan(c.commanded_[i]));
    c.Command("gaze 0 100"); assert(std::abs(c.commanded_[5]-(90+(20-90)*.925f))<.001f);
    c.Command("blink"); assert(c.commanded_[5]==90);
    c.Tick(70); assert(!c.blink_until_);
    // Each timed animation completes using only the three motion axes.
    for(const char* name:{"look","roll","side_eye","wink","surprise","sleepy"}) {
        c.Command(std::string("animate ")+name);
        for(int i=0;i<1000 && c.animation_>=0;++i) {fake_now+=10;assert(c.Tick(fake_now)==0);}
        assert(c.animation_<0);
        for(int i:{0,1,2,6,7,8}) assert(std::isnan(c.commanded_[i]));
    }
    c.Command("animate roll"); c.Command("stop"); assert(c.animation_<0);
    // The low-level cache must not record failed writes.
    float before=c.commanded_[3]; fake_fail_after=0;
    assert(c.Write(3,before==60?61:60)!=0 && c.commanded_[3]==before);
    Reject([&]{c.Command("gaze -100 0");});
    assert(c.released_ && c.last_error_!=0 && c.release_error_!=0);
    for(float a:c.commanded_) assert(std::isnan(a));
    fake_fail_after=-1; c.Command("release");
    auto count=fake_writes.size(); c.Tick(fake_now+100000);assert(fake_writes.size()==count);
    c.Command("engage"); c.Command("gaze 0 0");
    // A disconnected PCA is detected even with identical cached targets.
    fake_probe_error=-1; Reject([&]{c.Command("gaze 0 0");});assert(c.released_);
    fake_probe_error=0; c.Command("engage"); c.Command("animate look");
    fake_fail_after=0;c.Fail(c.Tick(fake_now+100));assert(c.released_ && c.animation_<0);
    fake_fail_after=-1;c.Command("release");c.Command("calibrate");
    c.Command("servo jaw 90");c.Command("jog jaw 2");
    Reject([&]{c.Command("jog jaw 10");});
    assert(c.commanded_[6]==92 && std::isnan(c.commanded_[0]));
    if (argc>1) std::ofstream(argv[1]) << c.Command("export");
    auto invalid=c.cal_; invalid.axes[0].confirmed=2; assert(!c.Valid(invalid,false));
    invalid=c.cal_; invalid.axes[0].trim_us=INT32_MAX; assert(!c.Valid(invalid,false));
    fake_nvs.assign(3,0xff);
    CogletController fresh; fresh.Load(); assert(!fresh.Valid(fresh.cal_,true));
    fake_fail_after=0; assert(fresh.InitHardware()!=ESP_OK && fresh.released_);
    fake_fail_after=-1; // the InitHardware check above left I2C failing
    // Endpoint hunting: calibration-only, bounded step, and a marked endpoint
    // drops confirmation until a human has watched it again.
    Reject([&]{c.Command("explore lids junk");});
    c.Command("explore jaw");
    assert(c.exploring_==6 && c.explore_angle_==90 && std::isnan(c.commanded_[6]));
    Reject([&]{c.Command("nudge 10");});
    c.Command("nudge 5"); assert(c.explore_angle_==95);
    c.Command("mark low");
    assert(c.cal_.axes[6].low==95 && !c.cal_.axes[6].confirmed);
    c.Command("nudge -5"); c.Command("mark high");
    assert(c.cal_.axes[6].high==90);
    c.Command("confirm jaw");
    assert(c.cal_.axes[6].confirmed==1 && c.exploring_<0);
    Reject([&]{c.Command("mark low");});      // nothing being explored
    // Nudging cannot leave the hard pulse bound even with a huge request.
    c.Command("explore jaw");
    for(int i=0;i<60;++i) c.Command("nudge -5");
    assert(c.explore_angle_==0);
    for(int i=0;i<80;++i) c.Command("nudge 5");
    assert(c.explore_angle_==180);
    // identify wiggles a raw channel that no role owns yet.
    fake_writes.clear();
    c.Command("identify 11");
    assert(!fake_writes.empty() && fake_writes.front().front()==6+4*11);
    Reject([&]{c.Command("identify 16");});
    // A partly built mechanism: base and tilt fitted, no lids yet.
    c.Command("release"); c.Command("confirm jaw");
    auto full=c.cal_;
    // No lid servo fitted yet: gaze still works, blink has nothing to move.
    c.cal_.axes[5].channel=-1; c.cal_.axes[5].confirmed=0;
    assert(c.Valid(c.cal_,true));
    c.Command("engage"); c.Command("gaze 0 0");
    assert(std::isnan(c.commanded_[5]));
    Reject([&]{c.Command("blink");});
    assert(c.next_blink_==INT64_MAX);
    c.Command("release");
    // Bottom-up: base fitted but no eyes yet. Engaging is fine; gaze is not.
    c.cal_=full;
    for(int i:{3,4,5}) {c.cal_.axes[i].channel=-1; c.cal_.axes[i].confirmed=0;}
    assert(c.Valid(c.cal_,true));
    c.Command("engage");
    Reject([&]{c.Command("gaze 0 0");});
    Reject([&]{c.Command("animate look");});
    // Nothing fitted at all is not something to engage.
    auto bare=c.cal_;
    for(auto& a:bare.axes) {a.channel=-1; a.confirmed=0;}
    assert(!c.Valid(bare,true));
    // An assigned but unconfirmed role blocks engagement.
    auto lame=full; lame.axes[7].confirmed=0; assert(!c.Valid(lame,true));
    c.Command("release"); c.cal_=full;
    // fit assigns a role to its standard channel, unconfirmed, with no range.
    c.cal_.axes[6].channel=-1; c.cal_.axes[6].confirmed=0;
    c.Command("fit jaw");
    assert(c.cal_.axes[6].channel==6 && !c.cal_.axes[6].confirmed);
    assert(c.cal_.axes[6].low==90 && c.cal_.axes[6].high==90);
    Reject([&]{c.Command("fit tail");});
    c.cal_.axes[7].channel=6; c.cal_.axes[6].channel=-1;
    Reject([&]{c.Command("fit jaw");});       // channel 6 already taken
    c.Command("calibrate"); Reject([&]{c.Command("fit jaw");}); // released only
    c.Command("release"); c.cal_=full;
    // test sweeps a role through its window on the motion timer.
    Reject([&]{c.Command("test jaw");});      // calibration mode only
    c.Command("calibrate");
    Reject([&]{c.Command("test tail");});
    c.Command("test jaw"); assert(c.testing_==6);
    {
        float lo=std::min(c.cal_.axes[6].low,c.cal_.axes[6].high);
        float hi=std::max(c.cal_.axes[6].low,c.cal_.axes[6].high);
        float seen_lo=1e9, seen_hi=-1e9;
        for(int t=0;t<1000 && c.testing_>=0;++t) {
            fake_now+=10; assert(c.Tick(fake_now)==0);
            float d=c.commanded_[6]; assert(std::isfinite(d) && d>=lo && d<=hi);
            seen_lo=std::min(seen_lo,d); seen_hi=std::max(seen_hi,d);
        }
        assert(c.testing_<0 && seen_lo==lo && seen_hi==hi);
        assert(c.commanded_[6]==(lo+hi)/2);        // ends centred
    }
    // With no role it visits every calibrated role, base upward.
    c.Command("test");
    {
        std::vector<int> order;
        for(int t=0;t<20000 && c.testing_>=0;++t) {
            if (order.empty() || order.back()!=c.testing_) order.push_back(c.testing_);
            fake_now+=10; assert(c.Tick(fake_now)==0);
        }
        std::vector<int> expect;
        for(int i=0;i<9;++i) if (c.Testable(i)) expect.push_back(i);
        assert(order==expect && !expect.empty());
    }
    // Release, stop and other calibration moves all end it.
    c.Command("test jaw"); fake_now+=10; c.Tick(fake_now);
    c.Command("release"); assert(c.testing_<0 && c.released_);
    Reject([&]{c.Command("test jaw");});
    c.Command("calibrate"); c.Command("test jaw"); c.Command("stop"); assert(c.testing_<0);
    c.Command("test jaw"); c.Command("explore jaw"); assert(c.testing_<0);
    c.Command("release"); c.cal_=full;
    // Head, jaw, ears and lids: fractions of each calibrated window.
    Reject([&]{c.Command("head 0 0 0");});          // released
    c.Command("engage");
    c.Command("head -100 100 0");
    {
        auto at=[&](int i,float f){const auto& a=c.cal_.axes[i]; return a.low+(a.high-a.low)*f;};
        assert(c.commanded_[0]==at(0,0) && c.commanded_[1]==at(1,1) && c.commanded_[2]==at(2,.5f));
        c.Command("jaw 100"); assert(c.commanded_[6]==at(6,1));
        c.Command("ears 0 100"); assert(c.commanded_[7]==at(7,0) && c.commanded_[8]==at(8,1));
        c.Command("lids 0"); assert(c.commanded_[5]==at(5,0));
        // A gaze leaves the head alone and hands the lids back to tracking.
        c.Command("gaze 0 0"); assert(c.commanded_[0]==at(0,0) && c.pose_[5]!=c.pose_[5]);
    }
    Reject([&]{c.Command("head 0 0");});
    Reject([&]{c.Command("head 0 0 101");});
    Reject([&]{c.Command("jaw -1");});
    Reject([&]{c.Command("ears 50");});
    Reject([&]{c.Command("lids 50 50");});
    // Every animation runs to completion. Head animations leave the eyes and
    // lids alone; eye animations leave the head, jaw and ears alone.
    for(const auto& anim:CogletController::Animations()) {
        c.Command("gaze 0 0"); c.Command("head 0 0 0"); c.Command("jaw 0"); c.Command("ears 50 50");
        auto before=c.commanded_;
        c.Command(std::string("animate ")+anim.name);
        for(int t=0;t<2000 && c.animation_>=0;++t) {fake_now+=10;assert(c.Tick(fake_now)==0);}
        assert(c.animation_<0);
        std::vector<int> untouched = anim.eyes ? std::vector<int>{0,1,2,6,7,8} : std::vector<int>{3,4,5};
        for(int i:untouched) assert(c.commanded_[i]==before[i]);
    }
    Reject([&]{c.Command("animate dance");});
    // A partly built head: only the jaw fitted. Head and eye commands have
    // nothing to move; jaw and talk still work.
    c.Command("release");
    for(int i=0;i<9;++i) if(i!=6) {c.cal_.axes[i].channel=-1; c.cal_.axes[i].confirmed=0;}
    c.Command("engage");
    Reject([&]{c.Command("head 0 0 0");});
    Reject([&]{c.Command("animate nod");});
    Reject([&]{c.Command("animate look");});
    c.Command("jaw 50"); c.Command("animate talk");
    // Release forgets the pose, so the next engage doesn't resume it.
    c.Command("release"); assert(std::isnan(c.pose_[6]));
    c.cal_=full;
    std::cout<<"Coglet host safety and motion tests passed (fake I2C/NVS; no hardware).\n";
}
