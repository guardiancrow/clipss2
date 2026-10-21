#include "platform/windows/capture_source.h"
#include "platform/windows/monitor_capture_source.h"
#include "platform/windows/monitor_selection_navigation.h"
#include <windows.h>
#include <iostream>
#include <array>
using namespace clipss2::platform::windows;
namespace
{
void require(bool b) { if (!b) throw std::runtime_error("Region geometry assertion failed."); }
template<class F> void rejects(F f) { bool failed=false; try { f(); } catch (const std::exception&) {failed=true;} require(failed); }
}
int main()
{
    try
    {
        require(next_monitor_index(0,1,false)==0 && next_monitor_index(0,1,true)==0);
        require(next_monitor_index(0,3,false)==1 && next_monitor_index(1,3,false)==2 && next_monitor_index(2,3,false)==0);
        require(next_monitor_index(0,3,true)==2 && next_monitor_index(2,3,true)==1 && next_monitor_index(1,3,true)==0);
        rejects([]{(void)next_monitor_index(0,0,false);}); rejects([]{(void)next_monitor_index(3,3,false);});
        const PixelRectangle b{-2560,-1440,0,0}, expected{160,100,1160,800};
        for (const auto& p:std::array<std::array<int,4>,4>{{{{-2400,-1340,-1400,-640}},{{-1400,-640,-2400,-1340}},
            {{-1400,-1340,-2400,-640}},{{-2400,-640,-1400,-1340}}}})
            require(region_crop(b,p[0],p[1],p[2],p[3])==expected);
        require(!region_crop(b,-2400,-1340,-2400,-640));
        require(!region_crop(b,-2400,-1340,-1400,-1340));
        require(region_crop(b,-2400,-1340,-2399,-1339)==PixelRectangle{160,100,161,101});
        require(region_crop(b,-2400,-1340,100,100)==PixelRectangle{160,100,2560,1440});
        require(region_crop(b,-2400,-1340,-4000,-4000)==PixelRectangle{0,0,160,100});
        for (int dpi:{96,120,144,192})
        { (void)dpi; require(region_crop(b,-2400,-1340,-1400,-640)==expected); } // Already physical: deliberately no DPI transform.
        const PixelRectangle extreme{INT32_MIN,INT32_MIN,-1,-1};
        require(region_crop(extreme,INT32_MIN,INT32_MIN,INT32_MAX,INT32_MAX)==PixelRectangle{0,0,INT32_MAX,INT32_MAX});
        rejects([] {validate_pixel_bounds({INT32_MIN,0,INT32_MAX,10});});
        rejects([&] { (void)region_crop(b,0,0,-100,-100); });
        for (const auto dimensions:std::array<std::array<int,2>,5>{{{1920,1080},{2560,1440},{3840,2160},{1440,2560},{2560,1440}}})
        {
            MonitorSource full{L"\\\\.\\DISPLAY1",{-dimensions[0],-dimensions[1],0,0},{}};
            validate_monitor_source(full);
            require(!full.crop && full.expected_bounds.width()==dimensions[0] && full.expected_bounds.height()==dimensions[1]);
        }
        MonitorSource source{L"\\\\.\\DISPLAY1",b,expected}; validate_monitor_source(source);
        source.crop=PixelRectangle{-1,0,1,1}; rejects([&]{validate_monitor_source(source);});
        source.crop=PixelRectangle{0,0,2561,1}; rejects([&]{validate_monitor_source(source);});
        source.crop=PixelRectangle{0,0,0,1}; rejects([&]{validate_monitor_source(source);});
        source.crop.reset(); source.device_name=L"primary"; rejects([&]{validate_monitor_source(source);});
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        POINT p{}; require(GetCursorPos(&p)!=FALSE);
        const auto monitor=MonitorFromPoint(p,MONITOR_DEFAULTTONULL); require(monitor!=nullptr);
        require(GetMonitorInfoW(monitor,&info)!=FALSE);
        source={info.szDevice,{info.rcMonitor.left,info.rcMonitor.top,info.rcMonitor.right,info.rcMonitor.bottom},PixelRectangle{0,0,1,1}};
        require(resolve_monitor_source(source)==reinterpret_cast<std::uintptr_t>(monitor));
        source.expected_topology=display_topology_snapshot();require(resolve_monitor_source(source)==reinterpret_cast<std::uintptr_t>(monitor));
        source.expected_topology+=".changed"; rejects([&]{(void)resolve_monitor_source(source);}); source.expected_topology.clear();
        for(const PixelRectangle big: {PixelRectangle{0,0,3840,2160},PixelRectangle{0,0,5120,2880},PixelRectangle{0,0,1440,2560}})
            require(region_crop(big,10,20,big.right-10,big.bottom-20)==PixelRectangle{10,20,big.right-10,big.bottom-20});
        source.expected_bounds.right-=1; rejects([&]{(void)resolve_monitor_source(source);});
        source.device_name=L"\\\\.\\DISPLAY999999"; rejects([&]{(void)resolve_monitor_source(source);});
        std::cout << "Region geometry/monitor identity: passed; four directions, clamps, negative origins, DPI-independent physical pixels, overflow and stale source\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
