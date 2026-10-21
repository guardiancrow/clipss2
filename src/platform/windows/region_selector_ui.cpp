#include "audit/audit_policy.h"
#include "region_selector_ui.h"
#include "selector_window_resources.h"
#include "window_selection_input.h"
#include "display_monitor.h"
#include "monitor_capture_source.h"
#include "monitor_selection_navigation.h"
#include "ui_localization.h"
#include <dwmapi.h>
#include <iostream>
#include <sstream>

namespace clipss2::platform::windows
{
namespace
{
using namespace selector_resources;
constexpr COLORREF clear_key = RGB(1,2,3);
std::wstring ascii_wide(std::string_view value) { return {value.begin(),value.end()}; }
std::wstring jpeg_row(UiLanguage language,const JpegSessionPolicy& policy)
{
    using namespace codec::jpeg;
    const auto& selection=policy.selection();
    if (!std::holds_alternative<JpegProfileSelection>(selection))
        return L"P: JPEG: "+std::wstring(ui_string(language,UiStringId::profile_default))+L" (Turbo)";
    const auto& selected=std::get<JpegProfileSelection>(selection);
    UiStringId profile=selected.profile==JpegProfile::balanced?UiStringId::profile_balanced:
        selected.profile==JpegProfile::smallest?UiStringId::profile_smallest:UiStringId::profile_fast;
    std::wstring encoder;
    if (selected.backend==JpegBackendId::automatic)
    {
        encoder=ui_string(language,UiStringId::automatic);
        try { const auto id=resolve_jpeg_product_selection(selection).selection.backend;
            encoder+=L" ("+ascii_wide(id==JpegBackendId::turbo?"Turbo":jpeg_backend_name(id))+L")"; }
        catch(const std::invalid_argument&) { encoder+=L" ("+std::wstring(ui_string(language,UiStringId::unavailable))+L")"; }
    }
    else encoder=ascii_wide(selected.backend==JpegBackendId::turbo?"Turbo":jpeg_backend_name(selected.backend));
    return L"P: JPEG: "+std::wstring(ui_string(language,profile))+L"   E: "+
        std::wstring(ui_string(language,UiStringId::encoder))+L": "+encoder;
}
class RegionUi final
{
public:
    explicit RegionUi(ProductPreferences initial, bool monitor_mode=false) : initial_(std::move(initial)), format_(initial_.file_format), jpeg_(initial_.jpeg_selection), monitor_mode_(monitor_mode), language_(resolve_ui_language()) {}
    ~RegionUi() { close(); }
    void create(HINSTANCE instance, const wchar_t* overlay_class, const wchar_t* hud_class)
    {
        monitors_=enumerate_display_monitors();
        if (monitor_mode_)
        {
            std::sort(monitors_.begin(),monitors_.end(),[](const auto& a,const auto& b) {
                const auto& x=a.monitor_rectangle; const auto& y=b.monitor_rectangle;
                if (x.top!=y.top) return x.top<y.top;
                if (x.left!=y.left) return x.left<y.left;
                return a.device_name<b.device_name;
            });
            owner_=monitor_at(point());
            if (!owner_) throw std::runtime_error("Pointer is not on an enumerated monitor.");
        }
        topology_=display_topology_snapshot();
        for (const auto& m:monitors_)
        {
            const auto& r=m.monitor_rectangle;
            validate_pixel_bounds({r.left,r.top,r.right,r.bottom});
            OwnedWindow w;
            w.reset(CreateWindowExW(WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                overlay_class,monitor_mode_?L"clipss2 Monitor shield":L"clipss2 Region shield",WS_POPUP,r.left,r.top,r.right-r.left,r.bottom-r.top,
                nullptr,nullptr,instance,this));
            if (!w.get()) winrt::throw_last_error();
            if (!SetLayeredWindowAttributes(w.get(),clear_key,65,LWA_COLORKEY|LWA_ALPHA)) winrt::throw_last_error();
            ShowWindow(w.get(),SW_SHOWNOACTIVATE); overlays_.push_back(std::move(w));
        }
        hud_.reset(CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,hud_class,
            ui_string(language_,monitor_mode_?UiStringId::monitor_selection_title:UiStringId::region_selection_title).data(),
            WS_POPUP|WS_BORDER,0,0,0,0,nullptr,nullptr,instance,this));
        if (!hud_.get()) winrt::throw_last_error();
        ready_=true; layout(); ShowWindow(hud_.get(),SW_SHOW);
        (void)SetForegroundWindow(hud_.get()); SetFocus(hud_.get());
        if (!SetTimer(hud_.get(),1,250,nullptr)) winrt::throw_last_error();
        std::cout << (monitor_mode_?"Monitor selector: ready; ":"Region selector: ready; ") << "single-monitor physical pixels; overlay count=" << overlays_.size() << '\n';
        std::cout.flush();
    }
    std::optional<RegionSelectionResult> run()
    {
        MSG message{};
        while (!done_)
        {
            const BOOL result=GetMessageW(&message,nullptr,0,0);
            if (result==-1) winrt::throw_last_error();
            if (!result) { PostQuitMessage(static_cast<int>(message.wParam)); cancel(); break; }
            event_point_=message.pt; // Input-time screen physical position, not a later GetCursorPos sample.
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        if (error_) std::rethrow_exception(error_);
        return result_;
    }
    static LRESULT CALLBACK procedure(HWND w,UINT m,WPARAM word,LPARAM parameter) noexcept
    {
        if (m==WM_NCCREATE)
        {
            const auto* creation=reinterpret_cast<CREATESTRUCTW*>(parameter);
            SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
        }
        auto* self=reinterpret_cast<RegionUi*>(GetWindowLongPtrW(w,GWLP_USERDATA));
        if (!self) return DefWindowProcW(w,m,word,parameter);
        try { return self->handle(w,m,word,parameter); }
        catch (...) { self->error_=std::current_exception(); self->cancel(); return 0; }
    }
private:
    std::optional<std::size_t> monitor_at(POINT p) const
    {
        for (std::size_t i=0;i<monitors_.size();++i) {
            const auto& b=monitors_[i].monitor_rectangle;
            if (p.x>=b.left && p.x<b.right && p.y>=b.top && p.y<b.bottom) return i;
        }
        return {};
    }
    RegionSelectionResult snapshot() const
    {
        RegionSelectionResult value{MonitorSource{monitors_.at(*owner_).device_name,owner_bounds(),{}},initial_,cursor_.include_cursor(),destination_.destination()};
        value.preferences.file_format=format_.format(); value.preferences.jpeg_selection=jpeg_.selection();
        value.source.expected_topology=topology_;
        return value;
    }
    void redraw_monitors()
    {
        for (const auto& overlay:overlays_) { InvalidateRect(overlay.get(),nullptr,TRUE); UpdateWindow(overlay.get()); }
        layout();
    }
    static bool button_down() noexcept { return (GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0; }
    static POINT point() { POINT p{}; if (!GetCursorPos(&p)) winrt::throw_last_error(); return p; }
    static bool mouse_event_source() noexcept
    {
        INPUT_MESSAGE_SOURCE source{};
        // Posted/replayed WM_LBUTTON messages have no mouse device source.
        if (!GetCurrentInputMessageSource(&source) || source.deviceType!=IMDT_MOUSE) return false;
        if (source.originId==IMO_HARDWARE) return true;
        wchar_t audit[2]{};
        return source.originId==IMO_INJECTED && clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_REGION_INJECTED_MOUSE",audit,2)==1 && audit[0]==L'1';
    }
    PixelRectangle owner_bounds() const
    { const auto& b=monitors_.at(*owner_).monitor_rectangle; return {b.left,b.top,b.right,b.bottom}; }
    std::wstring text() const
    {
        std::wostringstream s;
        if (monitor_mode_ && owner_) s << ui_string(language_,UiStringId::monitor_prefix) << (*owner_+1) << L"\n"
            << owner_bounds().width() << (language_==UiLanguage::japanese?L" × ":L" x ") << owner_bounds().height() << L" " << ui_string(language_,UiStringId::pixels)
            << L"\n" << ui_string(language_,UiStringId::monitor_navigation) << L"\n" << ui_string(language_,UiStringId::monitor_confirm)
            << L"\n" << ui_string(language_,UiStringId::escape_cancel) << L"\n";
        else s << ui_string(language_,UiStringId::region_drag) << L"\n" << ui_string(language_,UiStringId::escape_cancel) << L"\n";
        s << (cursor_.include_cursor()?ui_string(language_,UiStringId::cursor_on):ui_string(language_,UiStringId::cursor_off));
        switch(destination_.destination()) {
        case capture::CaptureDestination::file:s<<L"\n"<<ui_string(language_,UiStringId::destination_file);break;
        case capture::CaptureDestination::clipboard:s<<L"\n"<<ui_string(language_,UiStringId::destination_clipboard);break;
        case capture::CaptureDestination::both:s<<L"\n"<<ui_string(language_,UiStringId::destination_both);break; }
        const auto format_string=format_.format()==image::ImageFileFormat::png?UiStringId::format_png:
            format_.format()==image::ImageFileFormat::jpeg?UiStringId::format_jpeg:UiStringId::format_webp;
        s << L"\n" << (format_.format()==image::ImageFileFormat::avif
            ? language_==UiLanguage::japanese ? L"F: \u5f62\u5f0f: AVIF" : L"F: File format: AVIF"
            : format_.format()==image::ImageFileFormat::jxl
            ? language_==UiLanguage::japanese ? L"F: \u5f62\u5f0f: JPEG XL" : L"F: File format: JPEG XL"
            : ui_string(language_,format_string));
        if (format_.format()==image::ImageFileFormat::jpeg) s << L"\n" << jpeg_row(language_,jpeg_);
        else if (format_.format()==image::ImageFileFormat::avif || format_.format()==image::ImageFileFormat::jxl)
            s << L"\n" << codec_profile_hud(language_,format_.format(),
                format_.format()==image::ImageFileFormat::avif?initial_.avif_profile:initial_.jxl_profile);
        if (dragging_ && !monitor_mode_) s << L"\n" << (crop_?crop_->width():0) << (language_==UiLanguage::japanese?L" × ":L" x ") << (crop_?crop_->height():0) << L" " << ui_string(language_,UiStringId::pixels);
        return s.str();
    }
    void layout(UINT changed_dpi=0, const RECT* suggested=nullptr)
    {
        if (!hud_.get()) return;
        POINT p=point();
        const auto monitor=monitor_mode_ && owner_ ? reinterpret_cast<HMONITOR>(monitors_.at(*owner_).monitor_value) : MonitorFromPoint(p,MONITOR_DEFAULTTONULL);
        MONITORINFO info{}; info.cbSize=sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor,&info)) winrt::throw_last_error();
        // First move to the actual monitor; WM_DPICHANGED then rebuilds metrics.
        if (!changed_dpi && !positioning_)
        {
            positioning_=true;
            if (!SetWindowPos(hud_.get(),HWND_TOPMOST,info.rcWork.left+16,info.rcWork.top+16,0,0,SWP_NOSIZE|SWP_NOACTIVATE))
            { positioning_=false; winrt::throw_last_error(); }
            positioning_=false;
        }
        const UINT dpi=changed_dpi?changed_dpi:GetDpiForWindow(hud_.get());
        if (!dpi) throw std::runtime_error("Region HUD DPI unavailable.");
        if (font_dpi_!=dpi)
        {
            const auto font=create_system_ui_font(dpi);
            if (!font) winrt::throw_last_error(); font_.reset(font); font_dpi_=dpi;
            std::cout << (monitor_mode_?"Monitor HUD DPI: ":"Region HUD DPI: ") << dpi << '\n';
        }
        const WindowDeviceContext dc{hud_.get()};
        const auto old=SelectObject(dc.get(),font_.get());
        if (!old || old==HGDI_ERROR) throw std::runtime_error("Region HUD font selection failed.");
        RECT metrics{0,0,0,0}; const auto content=text();
        const int measured=DrawTextW(dc.get(),content.c_str(),-1,&metrics,DT_CALCRECT|DT_NOPREFIX);
        SelectObject(dc.get(),old);
        if (!measured) throw std::runtime_error("Region HUD text measurement failed.");
        padding_=MulDiv(12,static_cast<int>(dpi),96);
        RECT size{0,0,metrics.right+padding_*2,metrics.bottom+padding_*2};
        if (!AdjustWindowRectExForDpi(&size,WS_POPUP|WS_BORDER,FALSE,WS_EX_TOPMOST|WS_EX_TOOLWINDOW,dpi)) winrt::throw_last_error();
        const int x=suggested?suggested->left:info.rcWork.left+MulDiv(16,static_cast<int>(dpi),96);
        const int y=suggested?suggested->top:info.rcWork.top+MulDiv(16,static_cast<int>(dpi),96);
        if (!SetWindowPos(hud_.get(),HWND_TOPMOST,x,y,size.right-size.left,size.bottom-size.top,SWP_NOACTIVATE)) winrt::throw_last_error();
        InvalidateRect(hud_.get(),nullptr,TRUE); UpdateWindow(hud_.get());
    }
    void update(POINT p)
    {
        crop_=region_crop(owner_bounds(),anchor_.x,anchor_.y,p.x,p.y);
        InvalidateRect(overlays_.at(*owner_).get(),nullptr,TRUE); UpdateWindow(overlays_.at(*owner_).get());
        layout();
    }
    void cancel() noexcept { result_.reset(); done_=true; }
    void request_cancel() noexcept
    { if (dragging_ && button_down()) cancel_pending_=true; else cancel(); }
    void close() noexcept
    {
        ready_=false;
        if (hud_.get()) (void)KillTimer(hud_.get(),1);
        if (GetCapture() && std::any_of(overlays_.begin(),overlays_.end(),[](const auto& w){return w.get()==GetCapture();})) ReleaseCapture();
        overlays_.clear(); hud_.reset(); font_.reset();
    }
    void paint(HWND w)
    {
        const PaintSession paint{w}; RECT client{}; if (!GetClientRect(w,&client)) winrt::throw_last_error();
        const HDC dc=paint.context();
        if (w==hud_.get())
        {
            FillRect(dc,&client,GetSysColorBrush(COLOR_WINDOW)); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
            const auto old=SelectObject(dc,font_.get());
            RECT r{padding_,padding_,client.right-padding_,client.bottom-padding_}; const auto content=text();
            DrawTextW(dc,content.c_str(),-1,&r,DT_NOPREFIX); SelectObject(dc,old); return;
        }
        FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (monitor_mode_ && owner_ && w==overlays_.at(*owner_).get())
        {
            // Keep an opaque color-key mask: transparent pixels would pass clicks to the desktop.
            const OwnedBrush active{RGB(32,32,32)}, outline{RGB(255,80,0)};
            FillRect(dc,&client,active.get());
            for (int i=0;i<4;++i) { RECT r{i,i,client.right-i,client.bottom-i}; FrameRect(dc,&r,outline.get()); }
            return;
        }
        if (owner_ && w==overlays_.at(*owner_).get() && crop_)
        {
            const OwnedBrush clear{clear_key}, outline{RGB(255,80,0)};
            RECT r{crop_->left,crop_->top,crop_->right,crop_->bottom}; FillRect(dc,&r,clear.get());
            FrameRect(dc,&r,outline.get());
        }
    }
    LRESULT handle(HWND w,UINT m,WPARAM word,LPARAM parameter)
    {
        switch (m)
        {
        case WM_NCDESTROY: SetWindowLongPtrW(w,GWLP_USERDATA,0); return DefWindowProcW(w,m,word,parameter);
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(w); return 0;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr,IDC_CROSS)); return TRUE;
        case WM_NCHITTEST: if (w==hud_.get()) return HTTRANSPARENT; return DefWindowProcW(w,m,word,parameter);
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_DPICHANGED: if (w==hud_.get() && ready_) layout(HIWORD(word),reinterpret_cast<RECT*>(parameter)); return 0;
        case WM_DISPLAYCHANGE: if (ready_) { std::cout << "Region display change: canceled\n"; request_cancel(); } return 0;
        case WM_CLOSE: request_cancel(); return 0;
        case WM_CAPTURECHANGED: if (dragging_ && !done_) request_cancel(); return 0;
        case WM_TIMER:
            if (dragging_ && !button_down()) { std::cout << "Region lost release: canceled, never confirmed\n"; cancel(); }
            return 0;
        case WM_KEYDOWN:
        {
            if (!ready_) return 0;
            if (word==VK_ESCAPE) { request_cancel(); return 0; }
            const bool repeated=(static_cast<std::uintptr_t>(parameter)&(std::uintptr_t{1}<<30))!=0;
            const bool modified=audit_modifier_override_ ? (audit_modifiers_ & 6)!=0 :
                (GetKeyState(VK_CONTROL)&0x8000)!=0 || (GetKeyState(VK_MENU)&0x8000)!=0;
            if (dragging_ || repeated || modified) return 0;
            if (monitor_mode_ && word==VK_TAB)
            {
                const auto count=monitors_.size();
                owner_=next_monitor_index(*owner_,count,audit_modifier_override_ ? (audit_modifiers_ & 1)!=0 : (GetKeyState(VK_SHIFT)&0x8000)!=0);
                redraw_monitors(); return 0;
            }
            if (monitor_mode_ && word==VK_RETURN)
            {
                auto frozen=snapshot(); // Never substitute a replacement candidate for this input.
                (void)resolve_monitor_source(frozen.source);
                result_=std::move(frozen); done_=true; return 0;
            }
            if (word=='F') (void)format_.toggle(false,false,false);
            if (word=='O') (void)destination_.cycle(true);
            if (word=='C') (void)cursor_.toggle(false);
            if (word=='P')
            {
                if (format_.format()==image::ImageFileFormat::jpeg) (void)jpeg_.cycle_profile(true,false,false,false);
                else (void)cycle_codec_profile(format_.format(),initial_.avif_profile,initial_.jxl_profile,false,false,false);
            }
            if (word=='E') (void)jpeg_.cycle_encoder(format_.format()==image::ImageFileFormat::jpeg,false,false,false);
            layout(); return 0;
        }
        case WM_APP + 0x126:
        {
            wchar_t enabled[2]{};
            if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_SELECTOR_KEY_STATE", enabled, 2)==1 && enabled[0]==L'1' && word<=7)
            { audit_modifiers_=static_cast<unsigned int>(word); audit_modifier_override_=word!=0; }
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            if (!ready_ || dragging_ || w==hud_.get() || !mouse_event_source() || !button_down()) return 0;
            anchor_=event_point_;
            if (monitor_mode_)
            {
                const auto hit=monitor_at(anchor_); if (!hit) return 0;
                owner_=hit; frozen_=snapshot(); (void)resolve_monitor_source(frozen_->source);
                dragging_=true; SetCapture(w);
                if (GetCapture()!=w) throw std::runtime_error("Monitor mouse capture failed.");
                redraw_monitors(); return 0;
            }
            for (std::size_t i=0;i<monitors_.size();++i)
            {
                const auto& b=monitors_[i].monitor_rectangle;
                if (anchor_.x>=b.left && anchor_.x<b.right && anchor_.y>=b.top && anchor_.y<b.bottom) {owner_=i;break;}
            }
            if (!owner_) return 0;
            frozen_=RegionSelectionResult{MonitorSource{monitors_[*owner_].device_name,owner_bounds(),{}},initial_,cursor_.include_cursor(),destination_.destination()};
            frozen_->preferences.file_format=format_.format(); frozen_->preferences.jpeg_selection=jpeg_.selection();
            frozen_->source.expected_topology=topology_;
            (void)resolve_monitor_source(frozen_->source);
            dragging_=true; SetCapture(w);
            if (GetCapture()!=w) throw std::runtime_error("Region mouse capture failed.");
            std::cout << "Region mouse-down: settings frozen; monitor=" << winrt::to_string(monitors_[*owner_].device_name) << '\n';
            update(anchor_); return 0;
        }
        case WM_MOUSEMOVE:
            if (!mouse_event_source()) return 0;
            if (monitor_mode_)
            {
                if (ready_ && !dragging_ && !done_) {
                    const auto hit=monitor_at(event_point_);
                    if (hit && hit!=owner_) { owner_=hit; redraw_monitors(); }
                }
                return 0;
            }
            if (dragging_ && !done_) update(event_point_); else if (ready_ && w!=hud_.get()) layout(); return 0;
        case WM_LBUTTONUP:
            if (!dragging_ || done_ || !mouse_event_source() || button_down()) return 0;
            {
                wchar_t drop[2]{};
                if (clipss2::audit::environment_variable(L"_CLIPSS2_AUDIT_REGION_DROP_UP",drop,2)==1 && drop[0]==L'1') return 0;
            }
            if (monitor_mode_)
            {
                if (cancel_pending_) { cancel(); return 0; }
                (void)resolve_monitor_source(frozen_->source);
                result_=frozen_; done_=true; return 0;
            }
            update(event_point_);
            if (cancel_pending_ || !crop_) { std::cout << "Region mouse-up: canceled or empty\n"; cancel(); return 0; }
            frozen_->source.crop=crop_; (void)resolve_monitor_source(frozen_->source);
            result_=frozen_; done_=true;
            std::cout << "Region mouse-up: confirmed; crop=" << crop_->left << ',' << crop_->top << ',' << crop_->right << ',' << crop_->bottom << '\n';
            return 0;
        default: return DefWindowProcW(w,m,word,parameter);
        }
    }
    ProductPreferences initial_;
    FileFormatPolicy format_;
    JpegSessionPolicy jpeg_;
    CursorCapturePolicy cursor_;
    capture::CaptureDestinationPolicy destination_;
    std::vector<DisplayMonitorInfo> monitors_;
    std::string topology_;
    std::vector<OwnedWindow> overlays_;
    OwnedWindow hud_;
    OwnedFont font_;
    UINT font_dpi_{};
    int padding_{};
    bool ready_{},done_{},dragging_{},cancel_pending_{},positioning_{};
    bool audit_modifier_override_{};
    unsigned int audit_modifiers_{};
    POINT anchor_{};
    POINT event_point_{};
    std::optional<std::size_t> owner_;
    std::optional<PixelRectangle> crop_;
    std::optional<RegionSelectionResult> frozen_,result_;
    std::exception_ptr error_;
    bool monitor_mode_{};
    UiLanguage language_{UiLanguage::english};
};
}
std::optional<RegionSelectionResult> select_region(const ProductPreferences& initial)
{
    std::optional<RegionSelectionResult> result;
    {
        const HINSTANCE instance=GetModuleHandleW(nullptr); if (!instance) winrt::throw_last_error();
        const auto suffix=L"."+std::to_wstring(GetCurrentProcessId());
        RegisteredWindowClass overlay{instance,L"clipss2.RegionSelector.Shield"+suffix,RegionUi::procedure,nullptr};
        RegisteredWindowClass hud{instance,L"clipss2.RegionSelector.Status"+suffix,RegionUi::procedure,nullptr};
        RegionUi ui{initial}; ui.create(instance,overlay.name(),hud.name()); result=ui.run();
    }
    // Destruction and class release precede this compositor barrier and worker.
    winrt::check_hresult(DwmFlush());
    std::cout << "Region selector teardown: complete; DwmFlush: succeeded\n";
    return result;
}
std::optional<RegionSelectionResult> select_monitor(const ProductPreferences& initial)
{
    std::optional<RegionSelectionResult> result;
    {
        const HINSTANCE instance=GetModuleHandleW(nullptr); if (!instance) winrt::throw_last_error();
        const auto suffix=L"."+std::to_wstring(GetCurrentProcessId());
        RegisteredWindowClass overlay{instance,L"clipss2.MonitorSelector.Shield"+suffix,RegionUi::procedure,nullptr};
        RegisteredWindowClass hud{instance,L"clipss2.MonitorSelector.Status"+suffix,RegionUi::procedure,nullptr};
        RegionUi ui{initial,true}; ui.create(instance,overlay.name(),hud.name()); result=ui.run();
    }
    winrt::check_hresult(DwmFlush());
    std::cout << "Monitor selector teardown: complete; DwmFlush: succeeded\n";
    return result;
}
}
