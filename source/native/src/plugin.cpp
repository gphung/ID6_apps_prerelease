// Native NBT EVO RA plugin. Models are touched only on the HMI dispatcher.
// Network diagnostics run in the separate collector process, never in HmiMain3D.
#include "plugin_factory.h"
#include "gauge_core.h"
#include "n55_metrics.h"
#include "dial_scales.h"
#include "diagnostic_worker.h"
#include "diagnostic_gateway.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__arm__)
// C++98 target ABI gates. Host builds have 64-bit pointers and cannot prove these.
typedef char bd_pointer_size[(sizeof(void*)==4) ? 1 : -1];
typedef char bd_parcel_size[(sizeof(bmw::Parcel)==20) ? 1 : -1];
typedef char bd_message_size[(sizeof(bmw::Message)==80 && __alignof__(bmw::Message)==8) ? 1 : -1];
typedef char bd_std_string_size[(sizeof(std::string)==4) ? 1 : -1];
typedef char bd_delegate_size[(sizeof(bmw::Delegate<bool,const bmw::Parcel&>)==8) ? 1 : -1];
#endif

namespace {
// In the installed AppFramework, this message reads a UInt32 key from its
// Parcel and dispatches through the primary invokeDataUpdate virtual.
const unsigned kDataUpdateMessage = 0x40000003u;
const unsigned kPollKey = 0x4244504cu; // BDPL, private to this app
const unsigned kActivePollIntervalMs = 25u;
const unsigned kIdlePollIntervalMs = 250u;
const char kCollector[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/boostdeck-collector-qnx";
const char kProfileFile[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/profile.txt";
#ifdef PERF_LAB
// N55 unit choices, saved when changed: "<c|f> <psi|bar>". Defaults: Celsius and psi.
const char kUnitsFile[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/units.txt";
const char kUnitsTemp[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/units.tmp";
// N55 layout (tools/make_n55_layout.py) OPTIONS actions.
const unsigned kCelsiusAction = 70u, kFahrenheitAction = 71u, kPsiAction = 72u, kBarAction = 73u;
// N55 tiles (tools/make_n55_layout.py --tiles, tools/n55_table.py): eight keys, "-" for blank.
const char kTilesFile[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/tiles.txt";
const char kTilesTemp[] = "/var/opt/hmi/data/ID5/Plugins/BoostDeck/tiles.tmp";
const unsigned kCaptionModel = 120u, kButtonModel = 130u, kHeaderModel = 140u, kTilesHeaderModel = 148u;
const unsigned kResetAction = 170u, kPickFirst = 1000u, kPickBlank = 63u;
// Images (tools/make_n55_tiles_art.py): captions, and bars that start at the zero mark.
const unsigned kTilesBackground = 56302u;
const unsigned kCaptionFirst = 57000u, kCaptionBlank = 57063u;
const unsigned kZeroFirst = 58000u, kZeroSet = 600u;
const unsigned kRed = 7u;   // kColorNames
typedef char tile_steps_check[kDialSteps == NM_STEPS ? 1 : -1];
#endif

// profile.txt holding "n55" selects the F87 M2 N55 reads (SIGNALS.md). Anything else keeps the reference.
bool n55Profile() {
    FILE* file = fopen(kProfileFile, "r");
    if (!file) return false;
    char profile[8];
    const bool n55 = fscanf(file, "%7s", profile) == 1 && !strcmp(profile, "n55");
    fclose(file);
    return n55;
}
const unsigned kBarCount = sizeof(kDialScales)/sizeof(kDialScales[0]);
// N55: the eight tiles show the metrics chosen in OPTIONS > Tiles (n55_metrics.h), and the tall
// Active Ethanol tile is the ENGINE warm-up panel: engine oil temperature as a 100-300 F
// thermometer and the band name, in the band colour (bd_oil_band). The OIL TEMP tile takes the
// band colour too. Palettes index kColorNames.
#ifdef PERF_LAB
const double kOilMinimumC = (100.0 - 32) / 1.8, kOilMaximumC = (300.0 - 32) / 1.8;
const unsigned kOilPalette[] = {1u /* Blue */, 0u /* BMW Orange */, 2u /* Green */, 7u /* Red */};
const char* const kOilBandNames[] = {"Cold", "Warm", "Ready", "Hot"};
#endif
#ifdef PERF_LAB
typedef char dial_count_check[kBarCount + 1 == BD_COUNT ? 1 : -1];
const unsigned kEthanolFirst = 56100u;
const unsigned kEthanolSteps = 20u;
#else
typedef char dial_count_check[kBarCount == BD_COUNT ? 1 : -1];
#endif
}

class BoostDeck : public bmw::PluginApp {
    int fd_;
    unsigned poll_generation_;
    bd_frame frame_;
    uint64_t received_;
    pid_t collector_;
    bool stopping_;
    bool n55_;
    int oil_band_;
    bool gauges_visible_;
    unsigned color_;
#ifdef PERF_LAB
    bool metric_;
    bool celsius_;      // N55: temperature tiles in C
    bool bar_;          // N55: pressure tiles in bar
    void loadUnits();
    void saveUnits() const;
    // N55 tiles: the metric at each position, and the latest value of each metric.
    struct Live { double value, aux; uint64_t sampled; bool valid; };
    int tiles_[NM_TILES];
    Live live_[NM_COUNT];
    double knock_count_;
    uint64_t knock_at_, discharge_since_;
    unsigned captions_[NM_TILES];
    bool bar_shown_[NM_TILES];
    char metric_list_[NM_MAX_FRAME*16];
    void loadTiles();
    void saveTiles() const;
    void assignTile(unsigned position, int metric);
    void refreshTileMenus();
    void refreshTiles(uint64_t now);
    void takeFrame(const nm_frame& frame);
    bool fresh(unsigned id, uint64_t now) const {
        const Live& l = live_[id];
        return l.valid && now >= l.sampled && now - l.sampled < nm_stale_ms(id);
    }
#endif
    uint64_t retry_at_;
    void startVisibleCollector();
    void selectColor(unsigned color);
    char labels_[BD_COUNT][128];
    unsigned images_[BD_COUNT];
    void postPoll();
public:
    BoostDeck(bmw::IDispatcher& d, bmw::BasicString<char> name, unsigned maj, unsigned mn,
              bmw::RHMIInterfaceVersion rhmi, const bmw::Bundle& cfg, const bmw::Bundle& res)
        : bmw::PluginApp(d,name,maj,mn,rhmi,cfg,res), fd_(-1), poll_generation_(0),
          received_(0), collector_(-1), stopping_(false), n55_(false), oil_band_(-1), gauges_visible_(false),
          color_(0),
#ifdef PERF_LAB
          metric_(false), celsius_(true), bar_(false),
#endif
          retry_at_(0) {
        resetDisplay();
    }
    virtual ~BoostDeck() { shutdown(); }
    virtual bool onInit();
    virtual bool onFinalize() { shutdown(); return true; }
    virtual bool invokeDataUpdate(unsigned key, const bmw::Parcel& data);
    bool gaugeVisibility(const bmw::Parcel&);
    template <unsigned Color> bool changeColor(const bmw::Parcel&) {
        selectColor(Color);
        return true;
    }
#ifdef PERF_LAB
    template <bool Metric> bool changeUnits(const bmw::Parcel&) {
        metric_=Metric;
        refresh();
        return true;
    }
    template <bool Celsius> bool changeTemperature(const bmw::Parcel&) {
        celsius_=Celsius;
        saveUnits();
        refresh();
        return true;
    }
    template <bool Bar> bool changePressure(const bmw::Parcel&) {
        bar_=Bar;
        saveUnits();
        refresh();
        return true;
    }
    template <unsigned Position, int Metric> bool pick(const bmw::Parcel&) {
        assignTile(Position, Metric);
        return true;
    }
    bool resetTiles(const bmw::Parcel&) {
        memcpy(tiles_, nm_default_tiles, sizeof tiles_);
        assignTile(0, tiles_[0]);
        return true;
    }
#endif
    void reapCollector() {
        if (collector_ <= 0) return;
        pid_t ended = waitpid(collector_, 0, WNOHANG);
        if (ended == collector_ || (ended < 0 && errno == ECHILD)) {
            collector_ = -1;
            stopping_ = false;
        }
    }
    void stopCollector() {
        reapCollector();
        if (collector_ > 0 && !stopping_) {
            kill(collector_, SIGTERM);
            stopping_ = true;
        }
    }
    void shutdown() {
        gauges_visible_=false;
        stopCollector();
        if (fd_>=0) { close(fd_); fd_=-1; }
    }
    void resetDisplay() {
        memset(&frame_,0,sizeof frame_); memset(labels_,0,sizeof labels_);
        memset(images_,0,sizeof images_);
        received_=0;
#ifdef PERF_LAB
        memset(live_,0,sizeof live_);
        knock_count_=0; knock_at_=0; discharge_since_=0;
        memset(captions_,0,sizeof captions_);
#endif
    }
    void refresh();
    void poll();
};

bool BoostDeck::onInit() {
    shutdown();
    resetDisplay();
    n55_=n55Profile();
    oil_band_=-1;
#ifdef PERF_LAB
    // The N55 profile has no Active Ethanol (an MHD+ MG1 read); its tile is the ENGINE panel, and
    // the tile captions are images over a background without them.
    if (n55_) {
        updateModel(69u,bmw::ImageID(kTilesBackground));
        loadTiles();
        for (unsigned p=0;p<NM_TILES;++p) bar_shown_[p]=true;
    }
#endif
    struct sockaddr_in a;
    fd_=socket(AF_INET,SOCK_DGRAM,0);
    memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_port=htons(BD_UDP_PORT);
    a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (fd_>=0 && (fcntl(fd_,F_SETFL,O_NONBLOCK)<0 || fcntl(fd_,F_SETFD,FD_CLOEXEC)<0 ||
        bind(fd_,reinterpret_cast<struct sockaddr*>(&a),sizeof a)<0)) {
        close(fd_); fd_=-1;
    }
    bmw::Delegate<bool,const bmw::Parcel&> action;
    action.m_obj=this;
    // BMW sends event 11 with a bool when this HMI state becomes visible or hidden.
    action.m_exec=&bmw::Delegate<bool,const bmw::Parcel&>::execImpl<BoostDeck,&BoostDeck::gaugeVisibility>;
    addHandler(100u,11u,action);
    typedef bmw::Delegate<bool,const bmw::Parcel&> Action;
    typedef bool (*Handler)(void*,const bmw::Parcel&);
    static const Handler colorHandlers[] = {
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<0> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<1> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<2> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<3> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<4> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<5> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<6> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<7> >,
        &Action::execImpl<BoostDeck,&BoostDeck::changeColor<kMulticolor> >
    };
    typedef char color_handler_count[(sizeof colorHandlers/sizeof colorHandlers[0] ==
                                      sizeof kColorNames/sizeof kColorNames[0]) ? 1 : -1];
    (void)sizeof(color_handler_count);
    for (unsigned i=0;i<sizeof colorHandlers/sizeof colorHandlers[0];++i) {
        action.m_exec=colorHandlers[i];
        addHandler(40u+i,action);
    }
#ifdef PERF_LAB
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeUnits<false> >;
    addHandler(49u,action);
    action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeUnits<true> >;
    addHandler(59u,action);
    if (n55_) {
        loadUnits();
        action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeTemperature<true> >;
        addHandler(kCelsiusAction,action);
        action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changeTemperature<false> >;
        addHandler(kFahrenheitAction,action);
        action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changePressure<false> >;
        addHandler(kPsiAction,action);
        action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::changePressure<true> >;
        addHandler(kBarAction,action);
#define BD_PICK(p, m) &Action::execImpl<BoostDeck,&BoostDeck::pick<p,m> >
#define BD_PICK10(p, b) BD_PICK(p,b), BD_PICK(p,b+1), BD_PICK(p,b+2), BD_PICK(p,b+3), BD_PICK(p,b+4), \
                        BD_PICK(p,b+5), BD_PICK(p,b+6), BD_PICK(p,b+7), BD_PICK(p,b+8), BD_PICK(p,b+9)
#define BD_PICK_ROW(p) {BD_PICK10(p,0), BD_PICK10(p,10), BD_PICK10(p,20), BD_PICK10(p,30), BD_PICK(p,40), \
                        BD_PICK(p,NM_BLANK)}
        // One action per position and metric (and blank): a picked metric needs no second step.
        static const Handler pickHandlers[NM_TILES][NM_COUNT+1] = {
            BD_PICK_ROW(0), BD_PICK_ROW(1), BD_PICK_ROW(2), BD_PICK_ROW(3),
            BD_PICK_ROW(4), BD_PICK_ROW(5), BD_PICK_ROW(6), BD_PICK_ROW(7)
        };
#undef BD_PICK_ROW
#undef BD_PICK10
#undef BD_PICK
        typedef char pick_count[NM_COUNT == 41 && NM_TILES == 8 ? 1 : -1];
        (void)sizeof(pick_count);
        for (unsigned p=0;p<NM_TILES;++p) {
            for (unsigned m=0;m<=NM_COUNT;++m) {
                action.m_exec=pickHandlers[p][m];
                addHandler(kPickFirst+p*64u+(m<NM_COUNT ? m : kPickBlank),action);
            }
        }
        action.m_exec=&Action::execImpl<BoostDeck,&BoostDeck::resetTiles>;
        addHandler(kResetAction,action);
        refreshTileMenus();
    }
#endif
    selectColor(color_);
    bmw::Parcel visible(true);
    for (unsigned i=0;i<kBarCount;++i) setProperty(200u+i,3u,visible);
    refresh();
    if (fd_>=0) {
        ++poll_generation_;
        postPoll();
    }
    return true;
}

bool BoostDeck::gaugeVisibility(const bmw::Parcel& event) {
    bool visible;
    if (!event.readBool(visible) || gauges_visible_==visible) return true;
    gauges_visible_=visible;
    if (visible) {
        retry_at_=0;
        startVisibleCollector();
    } else {
        stopCollector();
        received_=0;
        memset(&frame_,0,sizeof frame_);
#ifdef PERF_LAB
        memset(live_,0,sizeof live_);
#endif
        refresh();
    }
    return true;
}

#ifdef PERF_LAB
void BoostDeck::loadUnits() {
    celsius_=true;
    bar_=false;
    FILE* file=fopen(kUnitsFile,"r");
    if (!file) return;
    char temperature[4], pressure[4];
    if (fscanf(file,"%3s %3s",temperature,pressure)==2) {
        if (!strcmp(temperature,"f")) celsius_=false;
        if (!strcmp(pressure,"bar")) bar_=true;
    }
    fclose(file);
}

// Written beside the plugin and renamed into place, so a reset never leaves half a file.
void BoostDeck::saveUnits() const {
    FILE* file=fopen(kUnitsTemp,"w");
    if (!file) return;
    const bool written=fprintf(file,"%s %s\n",celsius_ ? "c" : "f",bar_ ? "bar" : "psi")>0;
    if (fclose(file)==0 && written) rename(kUnitsTemp,kUnitsFile);
    else remove(kUnitsTemp);
}

// A missing or damaged tiles.txt gives the default layout (nm_parse_tiles).
void BoostDeck::loadTiles() {
    char text[512];
    size_t n=0;
    FILE* file=fopen(kTilesFile,"r");
    if (file) {
        n=fread(text,1,sizeof text,file);
        fclose(file);
    }
    nm_parse_tiles(text,n<sizeof text ? n : 0,tiles_);
}

void BoostDeck::saveTiles() const {
    char text[512];
    const int n=nm_format_tiles(tiles_,text,sizeof text);
    FILE* file=n>0 ? fopen(kTilesTemp,"w") : 0;
    if (!file) return;
    const bool written=fwrite(text,1,static_cast<size_t>(n),file)==static_cast<size_t>(n);
    if (fclose(file)==0 && written) rename(kTilesTemp,kTilesFile);
    else remove(kTilesTemp);
}

void BoostDeck::assignTile(unsigned position, int metric) {
    nm_assign(tiles_,position,metric);
    saveTiles();
    refreshTileMenus();
    // The collector reads only what the tiles show: start it again with the new list.
    stopCollector();
    retry_at_=0;
    memset(live_,0,sizeof live_);
    refresh();
}

void BoostDeck::refreshTileMenus() {
    updateModel(kTilesHeaderModel,bmw::BasicString<char>("Choose a tile to change"));
    for (unsigned p=0;p<NM_TILES;++p) {
        char name[32], header[64];
        if (tiles_[p]>=0) nm_display_name(static_cast<unsigned>(tiles_[p]),name,sizeof name);
        else strcpy(name,"(blank)");
        snprintf(header,sizeof header,"Showing: %s",name);
        updateModel(kButtonModel+p,bmw::BasicString<char>(name));
        updateModel(kHeaderModel+p,bmw::BasicString<char>(header));
    }
}

void BoostDeck::takeFrame(const nm_frame& frame) {
    for (unsigned i=0;i<NM_COUNT;++i) live_[i].valid=false;
    for (unsigned i=0;i<frame.count;++i) {
        const nm_entry& e=frame.entry[i];
        if (e.id>=NM_COUNT || e.age_ms>frame.sent_ms) continue;
        Live& l=live_[e.id];
        l.value=e.value; l.aux=e.aux; l.sampled=frame.sent_ms-e.age_ms; l.valid=true;
        if (e.id==NM_KNOCK) {
            if (e.aux>knock_count_) knock_at_=l.sampled;   // a new knock event
            knock_count_=e.aux;                            // a new collector counts from 0
        }
    }
    received_=frame.sent_ms;
}

void BoostDeck::refreshTiles(uint64_t now) {
    const bool running=fresh(NM_RPM,now) && live_[NM_RPM].value>400;
    for (unsigned p=0;p<NM_TILES;++p) {
        const int m=tiles_[p];
        char label[64]="";
        unsigned image=kBarFirst;
        const unsigned caption=m>=0 ? kCaptionFirst+static_cast<unsigned>(m) : kCaptionBlank;
        if (caption!=captions_[p]) {
            updateModel(kCaptionModel+p,bmw::ImageID(caption));
            captions_[p]=caption;
        }
        if ((m>=0)!=bar_shown_[p]) {
            bar_shown_[p]=m>=0;
            setProperty(200u+p,3u,bmw::Parcel(bar_shown_[p]));
        }
        if (m>=0) {
            const unsigned id=static_cast<unsigned>(m);
            const nm_metric& d=nm_metrics[id];
            const Live& l=live_[id];
            unsigned palette=color_==kMulticolor ? kMulticolor+p : color_;
            const bool ok=fresh(id,now);
            unsigned step=0;
            if (ok) {
                nm_format_value(id,l.value,l.aux,celsius_,bar_,label,sizeof label);
                step=nm_bar_step(id,l.value,kDialSteps);
                switch (d.rule) {
                case NR_OIL_BAND: if (oil_band_>=0) palette=kOilPalette[oil_band_]; break;
                case NR_KNOCK:
                    // Full red on a knock, empty 1 s later; the text stays live.
                    step=static_cast<unsigned>(nm_knock_fraction(knock_at_ && now>=knock_at_ ? now-knock_at_
                                                                 : static_cast<uint64_t>(NM_KNOCK_FADE_MS))*kDialSteps+.5);
                    palette=kRed;
                    break;
                case NR_MISFIRE: if (l.value>0) palette=kRed; break;
                case NR_BATTERY_V: if (nm_battery_v_red(l.value,running)) palette=kRed; break;
                case NR_BATTERY_A: if (nm_battery_a_red(l.value,running,now,&discharge_since_)) palette=kRed; break;
                case NR_BATTERY_PCT: if (nm_battery_pct_red(l.value)) palette=kRed; break;
                case NR_START_MARGIN: if (nm_start_margin_red(l.value)) palette=kRed; break;
                default: break;
                }
            } else strcpy(label,"--");
            if (d.bar==NB_ZERO) {
                const int set=nm_zero_set(id);
                if (!ok) step=nm_zero_step(id,kDialSteps);   // the empty track and its zero mark
                image=kZeroFirst+static_cast<unsigned>(set)*kZeroSet+palette*(kDialSteps+1)+step;
            } else if (step) image=kBarFirst+palette*kDialSteps+step;
        }
        if (image!=images_[p]) {
            updateModel(60u+p,bmw::ImageID(image));
            images_[p]=image;
        }
        if (strcmp(label,labels_[p])) {
            updateModel(50u+p,bmw::BasicString<char>(label));
            strcpy(labels_[p],label);
        }
    }
}
#endif

void BoostDeck::startVisibleCollector() {
    if (!gauges_visible_ || collector_>0 || fd_<0 ||
        bd_mono_ms()<retry_at_) return;
    const DiagnosticGateway gateway("/var/opt/hmi/data/ID5/Plugins/" "BoostDeck" "/gateway-ip.txt");
    // The ethanol query is an MHD+ MG1 layout, so the N55 profile does not send it.
    const bool n55=n55_;
    const char *args[8];
    unsigned k=0;
    args[k++]=kCollector; args[k++]="--host"; args[k++]=gateway.host(); args[k++]="--session";
#ifdef PERF_LAB
    if (n55) {
        // Each tile's metric once, oil temperature for ENGINE, and RPM for the battery warnings.
        int wanted[NM_MAX_FRAME];
        const unsigned count=nm_wanted(tiles_,wanted);
        size_t used=0;
        metric_list_[0]=0;
        for (unsigned i=0;i<count;++i) {
            const int n=snprintf(metric_list_+used,sizeof metric_list_-used,"%s%s",i ? "," : "",
                                 nm_metrics[wanted[i]].key);
            if (n<0 || static_cast<size_t>(n)>=sizeof metric_list_-used) return;
            used+=static_cast<size_t>(n);
        }
        args[k++]="--metrics"; args[k++]=metric_list_;
    }
    else args[k++]="--ethanol";
#else
    if (n55) { args[k++]="--profile"; args[k++]="n55"; }
#endif
    args[k]=0;
    collector_=launch(args,-1);
    retry_at_=bd_mono_ms()+2000;
}

void BoostDeck::selectColor(unsigned color) {
    color_=color;
    char label[48];
    snprintf(label,sizeof label,"Selected: %s",kColorNames[color_]);
    updateModel(110u,bmw::BasicString<char>(label));
    refresh();
}

void BoostDeck::postPoll() {
    // The framework copies the Message and Parcel into its own queue. Entries
    // carry only data, so clearing the queue needs no code from this module.
    bmw::Parcel data;
    data.writeUInt32(kPollKey);
    data.writeUInt32(poll_generation_);
    bmw::Message message(kDataUpdateMessage, data);
    post(message, collector_ > 0 ? kActivePollIntervalMs : kIdlePollIntervalMs);
}

bool BoostDeck::invokeDataUpdate(unsigned key, const bmw::Parcel& data) {
    if (key!=kPollKey) return bmw::PluginApp::invokeDataUpdate(key,data);
    unsigned generation;
    if (!data.readUInt32(generation)) return false;
    if (fd_<0 || generation!=poll_generation_) return true;
    poll();
    if (fd_>=0 && generation==poll_generation_) postPoll();
    return true;
}

void BoostDeck::poll() {
    reapCollector();
    startVisibleCollector();
    // A bounded non-blocking drain: at most 8 packets per UI tick.
    char data[static_cast<unsigned>(NM_MAX_PACKET) > static_cast<unsigned>(BD_MAX_PACKET) ? static_cast<unsigned>(NM_MAX_PACKET) : static_cast<unsigned>(BD_MAX_PACKET)];
    for (unsigned n=0;fd_>=0 && n<8;++n) {
        ssize_t k=recv(fd_,data,sizeof data,0);
        if (k<=0) break;
#ifdef PERF_LAB
        if (n55_) {
            nm_frame frame;
            if (gauges_visible_ && !stopping_ && nm_parse(data,static_cast<size_t>(k),&frame) &&
                frame.sent_ms && frame.sent_ms<=bd_mono_ms() && (!received_ || frame.sent_ms>=received_))
                takeFrame(frame);
            continue;
        }
#endif
        bd_frame next;
        if (gauges_visible_ && !stopping_ && bd_parse(data,static_cast<size_t>(k),&next) &&
            next.sent_ms && next.sent_ms<=bd_mono_ms() &&
            (!received_ || next.sent_ms>=received_)) {
            frame_=next; received_=next.sent_ms; // queued UDP does not reset sample age
        }
    }
    refresh();
}

void BoostDeck::refresh() {
    uint64_t elapsed=received_ ? bd_mono_ms()-received_ : static_cast<uint64_t>(BD_STALE_MS);
#ifdef PERF_LAB
    if (n55_) {
        const uint64_t now=bd_mono_ms();
        if (fresh(NM_OIL_TEMP,now)) oil_band_=bd_oil_band(live_[NM_OIL_TEMP].value,oil_band_);
        else oil_band_=-1;
        refreshTiles(now);
    }
    for (unsigned i=0;!n55_ && i<kBarCount;++i) {
#else
    for (unsigned i=0;i<kBarCount;++i) {
#endif
        char label[128];
        const DialScale scale = kDialScales[i];
        unsigned image = kBarFirst;
        if (received_ && bd_is_fresh(&frame_,i,elapsed)) {
            double x=frame_.value[i];
            const char *unit_space = *scale.unit ? " " : "";
            double reading = bd_display_value(i,x,0);
            if (i==BD_BOOST && fabs(reading)<0.05) reading=0;
#ifdef PERF_LAB
            if (metric_) {
                const char* units=i==BD_AFR ? "" : i==BD_TIMING ? "deg" :
                    i==BD_IAT || i==BD_TRANS || i==BD_COOLANT ? "\260C" : "bar";
                snprintf(label,sizeof label,"%.1f%s%s",bd_display_value(i,x,1),*units ? " " : "",units);
            } else snprintf(label,sizeof label,i==BD_RAIL ? "%.0f%s%s" : "%.1f%s%s",reading,unit_space,scale.unit);
#else
            const char *prefix = frame_.demo && i==BD_BOOST ? "Demo " : "";
            const char *format = i==BD_BOOST ? "%s%s   %+.1f%s%s"
                                : i==BD_RAIL ? "%s%s   %.0f%s%s"
                                : "%s%s   %.1f%s%s";
            snprintf(label,sizeof label,format,prefix,scale.name,reading,unit_space,scale.unit);
#endif
            double fraction=(x-scale.minimum)/(scale.maximum-scale.minimum);
            if (fraction<0) fraction=0;
            if (fraction>1) fraction=1;
            unsigned step=static_cast<unsigned>(fraction*kDialSteps+.5);
            if (step) image += (color_ == kMulticolor ? kMulticolor + i : color_)*kDialSteps+step;
        } else {
#ifdef PERF_LAB
            strcpy(label,"--");
#else
            snprintf(label,sizeof label,"%s   --",scale.name);
#endif
        }
        if (image!=images_[i]) {
            updateModel(60u+i,bmw::ImageID(image));
            images_[i]=image;
        }
        if (strcmp(label,labels_[i])) {
            updateModel(50u+i,bmw::BasicString<char>(label));
            strcpy(labels_[i],label);
        }
    }
#ifdef PERF_LAB
    char ethanol[128];
    unsigned ethanolImage=kEthanolFirst;
    if (n55_) {
        // ENGINE panel: oil thermometer in the band colour, and the band name.
        const bool valid = oil_band_>=0;
        if (valid) {
            double fraction=(live_[NM_OIL_TEMP].value-kOilMinimumC)/(kOilMaximumC-kOilMinimumC);
            if (fraction<0) fraction=0;
            if (fraction>1) fraction=1;
            const unsigned step=static_cast<unsigned>(fraction*kEthanolSteps+.5);
            if (step) ethanolImage+=kOilPalette[oil_band_]*kEthanolSteps+step;
            strcpy(ethanol,kOilBandNames[oil_band_]);
        } else strcpy(ethanol,"--");
    } else {
        const bool valid = received_ && bd_is_fresh(&frame_,BD_ETHANOL,elapsed) &&
            frame_.value[BD_ETHANOL]>=0 && frame_.value[BD_ETHANOL]<=100;
        if (valid) snprintf(ethanol,sizeof ethanol,"%.0f%%",frame_.value[BD_ETHANOL]);
        else strcpy(ethanol,"--");
        const unsigned step=valid ? static_cast<unsigned>(frame_.value[BD_ETHANOL]*kEthanolSteps/100.+.5) : 0;
        ethanolImage=kEthanolFirst+(step ? color_*kEthanolSteps+step : 0);
    }
    if (images_[BD_ETHANOL]!=ethanolImage) {
        updateModel(68u,bmw::ImageID(ethanolImage));
        images_[BD_ETHANOL]=ethanolImage;
    }
    if (strcmp(labels_[BD_ETHANOL],ethanol)) {
        updateModel(58u,bmw::BasicString<char>(ethanol));
        strcpy(labels_[BD_ETHANOL],ethanol);
    }
#endif
}

extern "C" {
PluginAppInfo pluginAppInfo = {1, "BoostDeck", "owner-development", 1,
                              create_app<BoostDeck>, destroy_app};
}
