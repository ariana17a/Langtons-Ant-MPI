#include <mpi.h>

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using std::max;
using std::min;

#include <gdiplus.h>

struct Params {
    int n = 100;
    int t = 10000;
    int ants = 1;
    int gather_every = 100;
    std::string out = "langton.ppm";
    bool help = false;
    bool cli = false;
    bool ui = true;
};

struct Ant {
    int id = 0;
    int x = 0;
    int y = 0;
    int dir = 0;
};

struct AntPacket {
    int active = 0;
    int id = 0;
    int x = 0;
    int y = 0;
    int dir = 0;
};

struct PreviewStats {
    int step = 0;
    int total_steps = 0;
    int ants = 0;
    int black_cells = 0;
    int white_cells = 0;
    int rank = 0;
    int size = 1;
    int ant_dir = -1;
    int comm_events = 0;
    int migrations = 0;
    double elapsed_seconds = 0.0;
    double steps_per_sec = 0.0;
    std::string mode;
    std::string status;
};

std::string readText(HWND control);
bool parseIntStrict(const std::string& text, int min_value, int& value);

class AntPreviewWindow {
public:
    explicit AntPreviewWindow(const Params& params)
        : n_(params.n), pending_params_(params), grid_(static_cast<size_t>(params.n) * static_cast<size_t>(params.n), 0) {}

    ~AntPreviewWindow() {
        if (gdiplus_started_) {
            Gdiplus::GdiplusShutdown(gdiplus_token_);
        }
    }

    bool open() {
        if (opened_) {
            return true;
        }

        HINSTANCE instance = GetModuleHandle(nullptr);
        WNDCLASSA window_class{};
        window_class.lpfnWndProc = AntPreviewWindow::WindowProc;
        window_class.hInstance = instance;
        window_class.lpszClassName = "LangtonPreviewWindow";
        window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        if (RegisterClassA(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        hwnd_ = CreateWindowExA(
            0,
            window_class.lpszClassName,
            "Langton's Ant - MPI Simulation",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            840,
            nullptr,
            nullptr,
            instance,
            this);

        if (hwnd_ == nullptr) {
            return false;
        }

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&gdiplus_token_, &input, nullptr) == Gdiplus::Ok) {
            gdiplus_started_ = true;
        }

        btn_start_ = CreateWindowA(
            "BUTTON",
            "Start",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            174,
            184,
            30,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_START)),
            instance,
            nullptr);

        const std::string n_default = std::to_string(pending_params_.n);
        const std::string t_default = std::to_string(pending_params_.t);
        const std::string ants_default = std::to_string(pending_params_.ants);

        edit_n_ = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            n_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            124,
            88,
            88,
            24,
            hwnd_,
            nullptr,
            instance,
            nullptr);

        edit_t_ = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            t_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            124,
            118,
            88,
            24,
            hwnd_,
            nullptr,
            instance,
            nullptr);

        edit_ants_ = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            ants_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            124,
            148,
            88,
            24,
            hwnd_,
            nullptr,
            instance,
            nullptr);

        btn_pause_ = CreateWindowA(
            "BUTTON",
            "Pause",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            124,
            210,
            88,
            30,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_PAUSE)),
            instance,
            nullptr);

        btn_stop_ = CreateWindowA(
            "BUTTON",
            "Stop",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            210,
            88,
            30,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_STOP)),
            instance,
            nullptr);

        btn_fit_ = CreateWindowA(
            "BUTTON",
            "Fit",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            246,
            88,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_FIT)),
            instance,
            nullptr);

        btn_grid_ = CreateWindowA(
            "BUTTON",
            "Grid: ON",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            124,
            246,
            88,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_GRID)),
            instance,
            nullptr);

        btn_center_ = CreateWindowA(
            "BUTTON",
            "Center: ON",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            278,
            184,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_CENTER)),
            instance,
            nullptr);

        btn_speed_ = CreateWindowA(
            "BUTTON",
            "Speed: 1x",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            310,
            184,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_SPEED)),
            instance,
            nullptr);

        btn_step_mode_ = CreateWindowA(
            "BUTTON",
            "Step Mode: OFF",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            342,
            184,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_STEP_MODE)),
            instance,
            nullptr);

        btn_step_ = CreateWindowA(
            "BUTTON",
            "Step +1",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            374,
            88,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_STEP)),
            instance,
            nullptr);

        btn_reset_ = CreateWindowA(
            "BUTTON",
            "Reset",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            124,
            374,
            88,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_RESET)),
            instance,
            nullptr);

        btn_export_ = CreateWindowA(
            "BUTTON",
            "Export PNG",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            28,
            406,
            184,
            28,
            hwnd_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_EXPORT)),
            instance,
            nullptr);

        SendMessageA(btn_start_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(edit_n_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(edit_t_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(edit_ants_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_pause_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_stop_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_fit_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_grid_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_center_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_speed_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_step_mode_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_step_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_reset_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        SendMessageA(btn_export_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);

        opened_ = true;
        return true;
    }

    bool isOpen() const {
        return opened_ && !closed_ && hwnd_ != nullptr;
    }

    void update(const std::vector<int>& grid, bool has_ant, int ant_x, int ant_y, const PreviewStats& stats) {
        if (!isOpen()) {
            return;
        }
        if (grid.size() == grid_.size()) {
            grid_ = grid;
        }
        has_ant_ = has_ant;
        ant_x_ = ant_x;
        ant_y_ = ant_y;
        stats_ = stats;

        if (consumeExportRequested()) {
            const std::string name = "frame_manual_step" + std::to_string(stats_.step) + ".png";
            saveCurrentFramePng(name);
        }
        if (stats_.step > 0 && auto_capture_every_ > 0 && stats_.step % auto_capture_every_ == 0 && stats_.step != last_auto_capture_step_) {
            const std::string name = "frame_" + std::to_string(stats_.step) + ".png";
            if (saveCurrentFramePng(name)) {
                last_auto_capture_step_ = stats_.step;
            }
        }

        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void pumpMessages() {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void waitUntilClosed() {
        if (!isOpen()) {
            return;
        }

        MSG msg;
        while (isOpen() && GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    bool isPaused() const { return paused_; }

    bool stopRequested() const { return stop_requested_; }

    bool hasStarted() const { return started_; }

    bool resetRequested() const { return reset_requested_; }

    bool consumeResetRequested() {
        const bool value = reset_requested_;
        reset_requested_ = false;
        return value;
    }

    bool stepModeEnabled() const { return step_mode_; }

    bool consumeStepOnceRequested() {
        const bool value = step_once_requested_;
        step_once_requested_ = false;
        return value;
    }

    int speedMultiplier() const { return speed_levels_[speed_index_]; }

    bool consumeExportRequested() {
        const bool value = export_requested_;
        export_requested_ = false;
        return value;
    }

    const Params& selectedParams() const { return pending_params_; }

private:
    static constexpr int ID_BTN_START = 4000;
    static constexpr int ID_BTN_PAUSE = 4001;
    static constexpr int ID_BTN_STOP = 4002;
    static constexpr int ID_BTN_FIT = 4003;
    static constexpr int ID_BTN_GRID = 4004;
    static constexpr int ID_BTN_CENTER = 4005;
    static constexpr int ID_BTN_SPEED = 4006;
    static constexpr int ID_BTN_STEP_MODE = 4007;
    static constexpr int ID_BTN_STEP = 4008;
    static constexpr int ID_BTN_RESET = 4009;
    static constexpr int ID_BTN_EXPORT = 4010;

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        AntPreviewWindow* self = reinterpret_cast<AntPreviewWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
            self = reinterpret_cast<AntPreviewWindow*>(cs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            if (self != nullptr) {
                self->hwnd_ = hwnd;
            }
            return TRUE;
        }

        if (self == nullptr) {
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }

        switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == ID_BTN_START) {
                int n_value = 0;
                int t_value = 0;
                int ants_value = 0;
                if (!parseIntStrict(readText(self->edit_n_), 1, n_value) ||
                    !parseIntStrict(readText(self->edit_t_), 0, t_value) ||
                    !parseIntStrict(readText(self->edit_ants_), 1, ants_value)) {
                    MessageBoxA(hwnd, "N, T si Ants trebuie sa fie numere intregi valide.", "Valori invalide", MB_OK | MB_ICONERROR);
                    return 0;
                }

                self->pending_params_.n = n_value;
                self->pending_params_.t = t_value;
                self->pending_params_.ants = ants_value;
                self->n_ = n_value;
                self->grid_.assign(static_cast<size_t>(n_value) * static_cast<size_t>(n_value), 0);
                self->started_ = true;
                self->paused_ = false;
                EnableWindow(self->btn_start_, FALSE);
                EnableWindow(self->edit_n_, FALSE);
                EnableWindow(self->edit_t_, FALSE);
                EnableWindow(self->edit_ants_, FALSE);
                SetWindowTextA(self->btn_pause_, "Pause");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_PAUSE) {
                if (!self->started_) {
                    return 0;
                }
                self->paused_ = !self->paused_;
                SetWindowTextA(self->btn_pause_, self->paused_ ? "Resume" : "Pause");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_STOP) {
                self->stop_requested_ = true;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_FIT) {
                self->fit_to_window_ = true;
                self->zoom_ = 1.0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_GRID) {
                self->show_grid_ = !self->show_grid_;
                SetWindowTextA(self->btn_grid_, self->show_grid_ ? "Grid: ON" : "Grid: OFF");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_CENTER) {
                self->auto_center_on_ant_ = !self->auto_center_on_ant_;
                SetWindowTextA(self->btn_center_, self->auto_center_on_ant_ ? "Center: ON" : "Center: OFF");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_SPEED) {
                self->speed_index_ = (self->speed_index_ + 1) % 4;
                const std::string caption = "Speed: " + std::to_string(self->speed_levels_[self->speed_index_]) + "x";
                SetWindowTextA(self->btn_speed_, caption.c_str());
                return 0;
            }
            if (id == ID_BTN_STEP_MODE) {
                self->step_mode_ = !self->step_mode_;
                SetWindowTextA(self->btn_step_mode_, self->step_mode_ ? "Step Mode: ON" : "Step Mode: OFF");
                return 0;
            }
            if (id == ID_BTN_STEP) {
                self->step_once_requested_ = true;
                return 0;
            }
            if (id == ID_BTN_RESET) {
                self->reset_requested_ = true;
                self->stop_requested_ = false;
                self->started_ = false;
                self->paused_ = false;
                self->step_once_requested_ = false;
                EnableWindow(self->btn_start_, TRUE);
                EnableWindow(self->edit_n_, TRUE);
                EnableWindow(self->edit_t_, TRUE);
                EnableWindow(self->edit_ants_, TRUE);
                SetWindowTextA(self->btn_pause_, "Pause");
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == ID_BTN_EXPORT) {
                self->export_requested_ = true;
                return 0;
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            const double factor = delta > 0 ? 1.2 : (1.0 / 1.2);
            self->zoom_ = std::max(1.0, std::min(32.0, self->zoom_ * factor));
            self->fit_to_window_ = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_CLOSE:
            self->closed_ = true;
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            self->hwnd_ = nullptr;
            if (self->gdiplus_started_) {
                Gdiplus::GdiplusShutdown(self->gdiplus_token_);
                self->gdiplus_started_ = false;
            }
            return 0;

        case WM_PAINT:
            self->paint();
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
        }
    }

    void paint() {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);

        RECT client;
        GetClientRect(hwnd_, &client);
        HBRUSH bg = CreateSolidBrush(RGB(12, 16, 22));
        FillRect(dc, &client, bg);
        DeleteObject(bg);

        RECT top_bar{0, 0, client.right, 44};
        HBRUSH top_br = CreateSolidBrush(RGB(17, 24, 34));
        FillRect(dc, &top_bar, top_br);
        DeleteObject(top_br);

        RECT left_panel{0, 44, 248, client.bottom};
        HBRUSH left_br = CreateSolidBrush(RGB(19, 26, 36));
        FillRect(dc, &left_panel, left_br);
        DeleteObject(left_br);

        RECT status_bar{248, client.bottom - 32, client.right, client.bottom};
        HBRUSH status_br = CreateSolidBrush(RGB(17, 24, 34));
        FillRect(dc, &status_bar, status_br);
        DeleteObject(status_br);

        RECT viewport{260, 54, client.right - 16, client.bottom - 42};
        HBRUSH view_br = CreateSolidBrush(RGB(6, 8, 12));
        FillRect(dc, &viewport, view_br);
        DeleteObject(view_br);

        const int available_w = std::max(1L, viewport.right - viewport.left - 16);
        const int available_h = std::max(1L, viewport.bottom - viewport.top - 16);
        const int side = std::max(1, std::min(available_w, available_h));

        RECT draw_rect{};
        draw_rect.left = viewport.left + (available_w - side) / 2 + 8;
        draw_rect.top = viewport.top + (available_h - side) / 2 + 8;
        draw_rect.right = draw_rect.left + side;
        draw_rect.bottom = draw_rect.top + side;

        if (fit_to_window_) {
            zoom_ = 1.0;
        }

        if (auto_center_on_ant_ && has_ant_) {
            view_center_x_ = ant_x_;
            view_center_y_ = ant_y_;
        } else if (view_center_x_ == 0 && view_center_y_ == 0) {
            view_center_x_ = n_ / 2;
            view_center_y_ = n_ / 2;
        }

        const double clamped_zoom = std::max(1.0, zoom_);
        const int source_size = std::max(1, static_cast<int>(std::round(static_cast<double>(n_) / clamped_zoom)));
        int src_x = view_center_y_ - source_size / 2;
        int src_y = view_center_x_ - source_size / 2;
        src_x = std::max(0, std::min(n_ - source_size, src_x));
        src_y = std::max(0, std::min(n_ - source_size, src_y));

        std::vector<unsigned char> rgb(static_cast<size_t>(n_) * static_cast<size_t>(n_) * 3u, 255);
        for (int r = 0; r < n_; ++r) {
            for (int c = 0; c < n_; ++c) {
                const size_t cell_index = static_cast<size_t>(r) * static_cast<size_t>(n_) + static_cast<size_t>(c);
                const size_t rgb_index = cell_index * 3u;
                const bool black = grid_[cell_index] != 0;
                rgb[rgb_index + 0] = black ? 20 : 250;
                rgb[rgb_index + 1] = black ? 20 : 250;
                rgb[rgb_index + 2] = black ? 20 : 250;
            }
        }

        if (has_ant_ && ant_x_ >= 0 && ant_x_ < n_ && ant_y_ >= 0 && ant_y_ < n_) {
            const size_t ant_index = (static_cast<size_t>(ant_x_) * static_cast<size_t>(n_) + static_cast<size_t>(ant_y_)) * 3u;
            rgb[ant_index + 0] = 30;
            rgb[ant_index + 1] = 40;
            rgb[ant_index + 2] = 230;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = n_;
        bmi.bmiHeader.biHeight = -n_;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 24;
        bmi.bmiHeader.biCompression = BI_RGB;

        StretchDIBits(
            dc,
            draw_rect.left,
            draw_rect.top,
            side,
            side,
            src_x,
            src_y,
            source_size,
            source_size,
            rgb.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);

        const int cell_px = std::max(1, side / source_size);
        if (show_grid_ && cell_px >= 4) {
            HPEN grid_pen = CreatePen(PS_SOLID, 1, RGB(33, 44, 58));
            HPEN old_grid_pen = static_cast<HPEN>(SelectObject(dc, grid_pen));
            for (int i = 0; i <= source_size; ++i) {
                const int x = draw_rect.left + (i * side) / source_size;
                const int y = draw_rect.top + (i * side) / source_size;
                MoveToEx(dc, x, draw_rect.top, nullptr);
                LineTo(dc, x, draw_rect.bottom);
                MoveToEx(dc, draw_rect.left, y, nullptr);
                LineTo(dc, draw_rect.right, y);
            }
            SelectObject(dc, old_grid_pen);
            DeleteObject(grid_pen);
        }

        if (has_ant_ && ant_x_ >= src_y && ant_x_ < src_y + source_size && ant_y_ >= src_x && ant_y_ < src_x + source_size) {
            const int ant_screen_x = draw_rect.left + ((ant_y_ - src_x) * side) / source_size;
            const int ant_screen_y = draw_rect.top + ((ant_x_ - src_y) * side) / source_size;
            const int ant_size = std::max(5, cell_px + 2);

            HBRUSH ant_brush = CreateSolidBrush(RGB(255, 94, 58));
            HBRUSH old_ant_brush = static_cast<HBRUSH>(SelectObject(dc, ant_brush));
            HPEN ant_pen = CreatePen(PS_SOLID, 2, RGB(255, 220, 120));
            HPEN old_ant_pen = static_cast<HPEN>(SelectObject(dc, ant_pen));
            Ellipse(dc, ant_screen_x - ant_size, ant_screen_y - ant_size, ant_screen_x + ant_size, ant_screen_y + ant_size);

            POINT arrow[3]{};
            if (stats_.ant_dir == 0) {
                arrow[0] = {ant_screen_x, ant_screen_y - ant_size - 6};
                arrow[1] = {ant_screen_x - 4, ant_screen_y - ant_size + 2};
                arrow[2] = {ant_screen_x + 4, ant_screen_y - ant_size + 2};
            } else if (stats_.ant_dir == 1) {
                arrow[0] = {ant_screen_x + ant_size + 6, ant_screen_y};
                arrow[1] = {ant_screen_x + ant_size - 2, ant_screen_y - 4};
                arrow[2] = {ant_screen_x + ant_size - 2, ant_screen_y + 4};
            } else if (stats_.ant_dir == 2) {
                arrow[0] = {ant_screen_x, ant_screen_y + ant_size + 6};
                arrow[1] = {ant_screen_x - 4, ant_screen_y + ant_size - 2};
                arrow[2] = {ant_screen_x + 4, ant_screen_y + ant_size - 2};
            } else if (stats_.ant_dir == 3) {
                arrow[0] = {ant_screen_x - ant_size - 6, ant_screen_y};
                arrow[1] = {ant_screen_x - ant_size + 2, ant_screen_y - 4};
                arrow[2] = {ant_screen_x - ant_size + 2, ant_screen_y + 4};
            }
            Polygon(dc, arrow, 3);

            SelectObject(dc, old_ant_pen);
            SelectObject(dc, old_ant_brush);
            DeleteObject(ant_pen);
            DeleteObject(ant_brush);
        }

        HPEN border = CreatePen(PS_SOLID, 1, RGB(55, 80, 112));
        HPEN old_pen = static_cast<HPEN>(SelectObject(dc, border));
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, draw_rect.left, draw_rect.top, draw_rect.right, draw_rect.bottom);

        if (stats_.size > 1) {
            static const COLORREF line_colors[] = {
                RGB(0, 191, 255), RGB(50, 205, 50), RGB(255, 215, 0), RGB(186, 85, 211), RGB(255, 99, 71)
            };
            SetTextColor(dc, RGB(120, 189, 255));
            for (int r = 1; r < stats_.size; ++r) {
                const int y = draw_rect.top + (r * (draw_rect.bottom - draw_rect.top)) / stats_.size;
                HPEN sep_pen = CreatePen(PS_DOT, 1, line_colors[(r - 1) % (sizeof(line_colors) / sizeof(line_colors[0]))]);
                HPEN old_sep_pen = static_cast<HPEN>(SelectObject(dc, sep_pen));
                MoveToEx(dc, draw_rect.left + 1, y, nullptr);
                LineTo(dc, draw_rect.right - 1, y);
                SelectObject(dc, old_sep_pen);
                DeleteObject(sep_pen);

                const std::string label = "Rank " + std::to_string(r);
                TextOutA(dc, draw_rect.right - 64, y + 2, label.c_str(), static_cast<int>(label.size()));
            }
            TextOutA(dc, draw_rect.right - 64, draw_rect.top + 4, "Rank 0", 6);
        }

        SelectObject(dc, old_brush);
        SelectObject(dc, old_pen);
        DeleteObject(border);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(95, 163, 255));
        const std::string title = "Mode: " + stats_.mode;
        TextOutA(dc, 12, 13, title.c_str(), static_cast<int>(title.size()));

        SetTextColor(dc, RGB(219, 229, 243));
        const std::string step_line = "Step: " + std::to_string(stats_.step) + " / " + std::to_string(stats_.total_steps);
        TextOutA(dc, 420, 13, step_line.c_str(), static_cast<int>(step_line.size()));

        SetTextColor(dc, !started_ ? RGB(95, 163, 255) : (paused_ ? RGB(245, 191, 66) : (stop_requested_ ? RGB(233, 82, 82) : RGB(55, 201, 98))));
        const std::string running = !started_ ? "Ready (press Start)" : (paused_ ? "Paused" : (stop_requested_ ? "Stop Requested" : "Simulation Running"));
        TextOutA(dc, client.right - 220, 13, running.c_str(), static_cast<int>(running.size()));

        SetTextColor(dc, RGB(165, 180, 203));
        TextOutA(dc, 20, 58, "Simulation Controls", 19);
        TextOutA(dc, 20, 90, "Grid Size (N):", 13);
        TextOutA(dc, 20, 120, "Steps (T):", 10);
        TextOutA(dc, 20, 150, "Ants:", 5);

        TextOutA(dc, 20, 468, "MPI Info", 8);
        const std::string proc_line = "Processes: " + std::to_string(stats_.size);
        TextOutA(dc, 20, 492, proc_line.c_str(), static_cast<int>(proc_line.size()));
        const std::string rank_line = "My Rank: " + std::to_string(stats_.rank);
        TextOutA(dc, 20, 510, rank_line.c_str(), static_cast<int>(rank_line.size()));

        TextOutA(dc, 20, 544, "Statistics", 10);
        const std::string black_line = "Black cells: " + std::to_string(stats_.black_cells);
        const std::string white_line = "White cells: " + std::to_string(stats_.white_cells);
        const std::string elapsed_line = "Elapsed: " + std::to_string(static_cast<int>(stats_.elapsed_seconds * 100.0) / 100.0);
        const std::string speed_line = "Steps/s: " + std::to_string(static_cast<int>(stats_.steps_per_sec));
        const std::string ants_line = "Ants active: " + std::to_string(stats_.ants);
        const std::string zoom_line = "Zoom: " + std::to_string(static_cast<int>(zoom_ * 100.0)) + "%";
        const std::string comm_line = "Comm events: " + std::to_string(stats_.comm_events);
        const std::string mig_line = "Migrations: " + std::to_string(stats_.migrations);
        TextOutA(dc, 20, 568, black_line.c_str(), static_cast<int>(black_line.size()));
        TextOutA(dc, 20, 586, white_line.c_str(), static_cast<int>(white_line.size()));
        TextOutA(dc, 20, 604, elapsed_line.c_str(), static_cast<int>(elapsed_line.size()));
        TextOutA(dc, 20, 622, speed_line.c_str(), static_cast<int>(speed_line.size()));
        TextOutA(dc, 20, 640, ants_line.c_str(), static_cast<int>(ants_line.size()));
        TextOutA(dc, 20, 658, zoom_line.c_str(), static_cast<int>(zoom_line.size()));
        TextOutA(dc, 20, 676, comm_line.c_str(), static_cast<int>(comm_line.size()));
        TextOutA(dc, 20, 694, mig_line.c_str(), static_cast<int>(mig_line.size()));

        if (has_ant_) {
            const std::string ant_line = "Ant position: (" + std::to_string(ant_x_) + ", " + std::to_string(ant_y_) + ")";
            TextOutA(dc, 20, 716, ant_line.c_str(), static_cast<int>(ant_line.size()));
            const char* dir_text = "?";
            if (stats_.ant_dir == 0) dir_text = "UP";
            if (stats_.ant_dir == 1) dir_text = "RIGHT";
            if (stats_.ant_dir == 2) dir_text = "DOWN";
            if (stats_.ant_dir == 3) dir_text = "LEFT";
            const std::string dir_line = std::string("Ant direction: ") + dir_text;
            TextOutA(dc, 20, 734, dir_line.c_str(), static_cast<int>(dir_line.size()));
        }

        SetTextColor(dc, RGB(145, 232, 163));
        const std::string bottom = "Status: " + stats_.status;
        TextOutA(dc, 262, client.bottom - 24, bottom.c_str(), static_cast<int>(bottom.size()));

        EndPaint(hwnd_, &ps);
    }

    static int getEncoderClsid(const WCHAR* format, CLSID* clsid) {
        UINT num = 0;
        UINT size = 0;
        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0) {
            return -1;
        }

        std::vector<BYTE> image_codec_info(size);
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(image_codec_info.data());
        Gdiplus::GetImageEncoders(num, size, codecs);
        for (UINT i = 0; i < num; ++i) {
            if (wcscmp(codecs[i].MimeType, format) == 0) {
                *clsid = codecs[i].Clsid;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    bool saveCurrentFramePng(const std::string& file_name) {
        if (!gdiplus_started_ || grid_.empty() || n_ <= 0) {
            return false;
        }

        Gdiplus::Bitmap bmp(n_, n_, PixelFormat24bppRGB);
        for (int y = 0; y < n_; ++y) {
            for (int x = 0; x < n_; ++x) {
                const bool black = grid_[static_cast<size_t>(y) * static_cast<size_t>(n_) + static_cast<size_t>(x)] != 0;
                Gdiplus::Color color = black ? Gdiplus::Color(20, 20, 20) : Gdiplus::Color(250, 250, 250);
                bmp.SetPixel(x, y, color);
            }
        }

        if (has_ant_ && ant_x_ >= 0 && ant_x_ < n_ && ant_y_ >= 0 && ant_y_ < n_) {
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int xx = ant_y_ + dx;
                    const int yy = ant_x_ + dy;
                    if (xx >= 0 && xx < n_ && yy >= 0 && yy < n_) {
                        bmp.SetPixel(xx, yy, Gdiplus::Color(255, 110, 60));
                    }
                }
            }
        }

        CLSID encoder{};
        if (getEncoderClsid(L"image/png", &encoder) < 0) {
            return false;
        }

        std::wstring wide_name(file_name.begin(), file_name.end());
        return bmp.Save(wide_name.c_str(), &encoder, nullptr) == Gdiplus::Ok;
    }

    int n_;
    HWND hwnd_ = nullptr;
    HWND edit_n_ = nullptr;
    HWND edit_t_ = nullptr;
    HWND edit_ants_ = nullptr;
    HWND btn_start_ = nullptr;
    HWND btn_pause_ = nullptr;
    HWND btn_stop_ = nullptr;
    HWND btn_fit_ = nullptr;
    HWND btn_grid_ = nullptr;
    HWND btn_center_ = nullptr;
    HWND btn_speed_ = nullptr;
    HWND btn_step_mode_ = nullptr;
    HWND btn_step_ = nullptr;
    HWND btn_reset_ = nullptr;
    HWND btn_export_ = nullptr;
    bool opened_ = false;
    bool closed_ = false;
    bool started_ = false;
    bool paused_ = false;
    bool stop_requested_ = false;
    bool reset_requested_ = false;
    bool export_requested_ = false;
    bool step_mode_ = false;
    bool step_once_requested_ = false;
    bool show_grid_ = true;
    bool auto_center_on_ant_ = true;
    bool fit_to_window_ = true;
    double zoom_ = 1.0;
    int view_center_x_ = 0;
    int view_center_y_ = 0;
    int speed_index_ = 0;
    int auto_capture_every_ = 100;
    int last_auto_capture_step_ = -1;
    const int speed_levels_[4] = {1, 10, 100, 1000};
    ULONG_PTR gdiplus_token_ = 0;
    bool gdiplus_started_ = false;
    Params pending_params_;
    std::vector<int> grid_;
    bool has_ant_ = false;
    int ant_x_ = -1;
    int ant_y_ = -1;
    PreviewStats stats_{};
};

class Grid {
public:
    Grid(int n, int storage_rows, int first_real_row)
        : n_(n), storage_rows_(storage_rows), first_real_row_(first_real_row), cells_(storage_rows * n, 0) {}

    int n() const { return n_; }

    int storageRowForGlobal(int global_row, int start_row) const {
        return global_row - start_row + first_real_row_;
    }

    int& at(int row, int col) { return cells_[row * n_ + col]; }

    const int& at(int row, int col) const { return cells_[row * n_ + col]; }

    int* rowPtr(int row) { return cells_.data() + row * n_; }

    const int* rowPtr(int row) const { return cells_.data() + row * n_; }

    void clear() { std::fill(cells_.begin(), cells_.end(), 0); }

private:
    int n_;
    int storage_rows_;
    int first_real_row_;
    std::vector<int> cells_;
};

Params parseArgs(int argc, char** argv) {
    Params params;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if ((arg == "-n" || arg == "--size") && i + 1 < argc) {
            params.n = std::stoi(argv[++i]);
        } else if ((arg == "-t" || arg == "--steps") && i + 1 < argc) {
            params.t = std::stoi(argv[++i]);
        } else if ((arg == "-ants" || arg == "--ants") && i + 1 < argc) {
            params.ants = std::stoi(argv[++i]);
        } else if ((arg == "-k" || arg == "--gather") && i + 1 < argc) {
            params.gather_every = std::stoi(argv[++i]);
        } else if ((arg == "-out" || arg == "--out") && i + 1 < argc) {
            params.out = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            params.help = true;
        } else if (arg == "--cli") {
            params.cli = true;
        } else if (arg == "--no-ui") {
            params.ui = false;
        } else if (arg == "--ui") {
            params.ui = true;
        } else {
            throw std::runtime_error("Invalid argument: " + arg);
        }
    }

    if (params.n <= 0 || params.t < 0 || params.ants <= 0 || params.gather_every <= 0) {
        throw std::runtime_error("Invalid parameters: n > 0, t >= 0, ants > 0, gather > 0");
    }

    return params;
}

struct GuiState {
    HWND edit_n = nullptr;
    HWND edit_t = nullptr;
    HWND edit_out = nullptr;
    HWND btn_run = nullptr;
    HWND btn_cancel = nullptr;
    Params* params = nullptr;
    bool accepted = false;
    HFONT font_title = nullptr;
    HFONT font_body = nullptr;
    HBRUSH edit_brush = nullptr;
};

constexpr int ID_BTN_RUN = 1001;
constexpr int ID_BTN_CANCEL = 1002;

constexpr int CARD_LEFT = 26;
constexpr int CARD_TOP = 24;
constexpr int CARD_RIGHT = 514;
constexpr int CARD_BOTTOM = 306;

COLORREF lerpColor(COLORREF a, COLORREF b, int i, int n) {
    if (n <= 0) {
        return a;
    }

    const int r = (GetRValue(a) * (n - i) + GetRValue(b) * i) / n;
    const int g = (GetGValue(a) * (n - i) + GetGValue(b) * i) / n;
    const int bl = (GetBValue(a) * (n - i) + GetBValue(b) * i) / n;
    return RGB(r, g, bl);
}

void fillVerticalGradient(HDC dc, const RECT& rect, COLORREF top, COLORREF bottom) {
    const int height = std::max(1L, rect.bottom - rect.top);
    for (int y = 0; y < height; ++y) {
        const COLORREF color = lerpColor(top, bottom, y, height - 1);
        HPEN pen = CreatePen(PS_SOLID, 1, color);
        HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, rect.left, rect.top + y, nullptr);
        LineTo(dc, rect.right, rect.top + y);
        SelectObject(dc, old_pen);
        DeleteObject(pen);
    }
}

void drawGlassButton(HDC dc, const RECT& rect, bool pressed, bool primary, const std::string& text, HFONT font) {
    const COLORREF base_top = primary ? RGB(222, 164, 255) : RGB(255, 210, 238);
    const COLORREF base_bottom = primary ? RGB(166, 108, 220) : RGB(227, 160, 214);
    const COLORREF top = pressed ? base_bottom : base_top;
    const COLORREF bottom = pressed ? base_top : base_bottom;

    fillVerticalGradient(dc, rect, top, bottom);

    RECT glare = rect;
    glare.bottom = rect.top + (rect.bottom - rect.top) / 2;
    const COLORREF glare_top = primary ? RGB(248, 230, 255) : RGB(255, 238, 248);
    const COLORREF glare_bottom = primary ? RGB(226, 194, 246) : RGB(248, 208, 234);
    fillVerticalGradient(dc, glare, glare_top, glare_bottom);

    HPEN border = CreatePen(PS_SOLID, 1, primary ? RGB(139, 73, 190) : RGB(170, 84, 145));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, border));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 12, 12);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(border);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(53, 20, 72));
    HFONT old_font = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextA(dc, text.c_str(), -1, const_cast<RECT*>(&rect), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
}

std::string readText(HWND control) {
    const int len = GetWindowTextLengthA(control);
    std::string text(static_cast<size_t>(len), '\0');
    if (len > 0) {
        GetWindowTextA(control, &text[0], len + 1);
    }
    return text;
}

bool parseIntStrict(const std::string& text, int min_value, int& value) {
    if (text.empty()) {
        return false;
    }

    size_t pos = 0;
    try {
        const long long parsed = std::stoll(text, &pos);
        if (pos != text.size()) {
            return false;
        }
        if (parsed < min_value || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

LRESULT CALLBACK ParamWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<GuiState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        state = reinterpret_cast<GuiState*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->font_title = CreateFontA(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        state->font_body = CreateFontA(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        state->edit_brush = CreateSolidBrush(RGB(253, 243, 255));

        const std::string n_default = std::to_string(state->params != nullptr ? state->params->n : 100);
        const std::string t_default = std::to_string(state->params != nullptr ? state->params->t : 10000);
        const std::string out_default = state->params != nullptr ? state->params->out : "langton.ppm";

        state->edit_n = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", n_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            245, 115, 235, 30, hwnd, nullptr, nullptr, nullptr);
        state->edit_t = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", t_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            245, 162, 235, 30, hwnd, nullptr, nullptr, nullptr);
        state->edit_out = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", out_default.c_str(),
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            245, 209, 235, 30, hwnd, nullptr, nullptr, nullptr);

        state->btn_run = CreateWindowA("BUTTON", "Run", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            288, 254, 92, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_RUN)), nullptr, nullptr);
        state->btn_cancel = CreateWindowA("BUTTON", "Cancel", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
            388, 254, 92, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_BTN_CANCEL)), nullptr, nullptr);

        SendMessageA(state->edit_n, WM_SETFONT, reinterpret_cast<WPARAM>(state->font_body), TRUE);
        SendMessageA(state->edit_t, WM_SETFONT, reinterpret_cast<WPARAM>(state->font_body), TRUE);
        SendMessageA(state->edit_out, WM_SETFONT, reinterpret_cast<WPARAM>(state->font_body), TRUE);
        SendMessageA(state->btn_run, WM_SETFONT, reinterpret_cast<WPARAM>(state->font_body), TRUE);
        SendMessageA(state->btn_cancel, WM_SETFONT, reinterpret_cast<WPARAM>(state->font_body), TRUE);

        SetFocus(state->edit_n);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);

        RECT client;
        GetClientRect(hwnd, &client);

        HDC mem_dc = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, client.right, client.bottom);
        HBITMAP old_bmp = static_cast<HBITMAP>(SelectObject(mem_dc, bmp));

        fillVerticalGradient(mem_dc, client, RGB(84, 42, 126), RGB(235, 128, 196));

        RECT shadow1{CARD_LEFT + 10, CARD_TOP + 12, CARD_RIGHT + 10, CARD_BOTTOM + 12};
        HBRUSH sh1 = CreateSolidBrush(RGB(97, 38, 121));
        FillRect(mem_dc, &shadow1, sh1);
        DeleteObject(sh1);

        RECT shadow2{CARD_LEFT + 6, CARD_TOP + 7, CARD_RIGHT + 6, CARD_BOTTOM + 7};
        HBRUSH sh2 = CreateSolidBrush(RGB(121, 52, 149));
        FillRect(mem_dc, &shadow2, sh2);
        DeleteObject(sh2);

        RECT card{CARD_LEFT, CARD_TOP, CARD_RIGHT, CARD_BOTTOM};
        HBRUSH card_brush = CreateSolidBrush(RGB(255, 246, 255));
        HPEN card_border = CreatePen(PS_SOLID, 1, RGB(207, 153, 228));
        HPEN old_pen = static_cast<HPEN>(SelectObject(mem_dc, card_border));
        HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(mem_dc, card_brush));
        RoundRect(mem_dc, card.left, card.top, card.right, card.bottom, 18, 18);
        SelectObject(mem_dc, old_brush);
        SelectObject(mem_dc, old_pen);
        DeleteObject(card_brush);
        DeleteObject(card_border);

        RECT accent{CARD_LEFT + 14, CARD_TOP + 16, CARD_RIGHT - 14, CARD_TOP + 64};
        fillVerticalGradient(mem_dc, accent, RGB(245, 221, 255), RGB(255, 228, 244));

        SetBkMode(mem_dc, TRANSPARENT);
        SetTextColor(mem_dc, RGB(66, 29, 97));
        HFONT old_font = static_cast<HFONT>(SelectObject(mem_dc, state != nullptr ? state->font_title : nullptr));
        RECT title_rect{CARD_LEFT + 22, CARD_TOP + 24, CARD_RIGHT - 22, CARD_TOP + 54};
        DrawTextA(mem_dc, "Langton Ant 2D (MPI)", -1, &title_rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SelectObject(mem_dc, state != nullptr ? state->font_body : nullptr);
        SetTextColor(mem_dc, RGB(106, 66, 130));
        RECT subtitle_rect{CARD_LEFT + 22, CARD_TOP + 52, CARD_RIGHT - 22, CARD_TOP + 78};
        DrawTextA(mem_dc, "Configureaza simularea dintr-o interfata moderna.", -1, &subtitle_rect,
            DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(mem_dc, RGB(95, 47, 123));
        RECT label_n{CARD_LEFT + 24, 117, 238, 139};
        RECT label_t{CARD_LEFT + 24, 164, 238, 186};
        RECT label_out{CARD_LEFT + 24, 211, 238, 233};
        DrawTextA(mem_dc, "Dimensiune grila (n > 0)", -1, &label_n, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextA(mem_dc, "Numar pasi (t >= 0)", -1, &label_t, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextA(mem_dc, "Fisier output (.ppm)", -1, &label_out, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        if (old_font != nullptr) {
            SelectObject(mem_dc, old_font);
        }

        BitBlt(dc, 0, 0, client.right, client.bottom, mem_dc, 0, 0, SRCCOPY);
        SelectObject(mem_dc, old_bmp);
        DeleteObject(bmp);
        DeleteDC(mem_dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));

    case WM_CTLCOLOREDIT:
        if (state != nullptr && state->edit_brush != nullptr) {
            SetBkColor(reinterpret_cast<HDC>(wparam), RGB(253, 243, 255));
            SetTextColor(reinterpret_cast<HDC>(wparam), RGB(60, 28, 82));
            return reinterpret_cast<LRESULT>(state->edit_brush);
        }
        return 0;

    case WM_DRAWITEM: {
        if (state == nullptr) {
            return 0;
        }

        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (dis == nullptr) {
            return 0;
        }

        std::string caption = "Button";
        const int text_len = GetWindowTextLengthA(dis->hwndItem);
        if (text_len > 0) {
            caption.resize(static_cast<size_t>(text_len));
            GetWindowTextA(dis->hwndItem, &caption[0], text_len + 1);
        }

        const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        const bool primary = dis->CtlID == ID_BTN_RUN;
        drawGlassButton(dis->hDC, dis->rcItem, pressed, primary, caption, state->font_body);
        return TRUE;
    }

    case WM_COMMAND: {
        if (state == nullptr) {
            return 0;
        }

        const int cmd = LOWORD(wparam);
        if (cmd == ID_BTN_CANCEL) {
            state->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        }

        if (cmd == ID_BTN_RUN) {
            const std::string n_text = readText(state->edit_n);
            const std::string t_text = readText(state->edit_t);
            const std::string out_text = readText(state->edit_out);

            int n_value = 0;
            int t_value = 0;
            if (!parseIntStrict(n_text, 1, n_value)) {
                MessageBoxA(hwnd, "n trebuie sa fie numar intreg > 0.", "Valoare invalida", MB_OK | MB_ICONERROR);
                return 0;
            }
            if (!parseIntStrict(t_text, 0, t_value)) {
                MessageBoxA(hwnd, "t trebuie sa fie numar intreg >= 0.", "Valoare invalida", MB_OK | MB_ICONERROR);
                return 0;
            }
            if (out_text.empty()) {
                MessageBoxA(hwnd, "Fisierul output nu poate fi gol.", "Valoare invalida", MB_OK | MB_ICONERROR);
                return 0;
            }

            state->params->n = n_value;
            state->params->t = t_value;
            state->params->out = out_text;
            state->params->help = false;
            state->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }

        return 0;
    }

    case WM_CLOSE:
        if (state != nullptr) {
            state->accepted = false;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (state != nullptr) {
            if (state->font_title != nullptr) {
                DeleteObject(state->font_title);
                state->font_title = nullptr;
            }
            if (state->font_body != nullptr) {
                DeleteObject(state->font_body);
                state->font_body = nullptr;
            }
            if (state->edit_brush != nullptr) {
                DeleteObject(state->edit_brush);
                state->edit_brush = nullptr;
            }
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

bool readGuiParams(Params& params) {
    HINSTANCE instance = GetModuleHandle(nullptr);

    WNDCLASSA window_class{};
    window_class.lpfnWndProc = ParamWindowProc;
    window_class.hInstance = instance;
    window_class.lpszClassName = "LangtonParamsWindow";
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;

    if (RegisterClassA(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("Unable to register GUI window class");
    }

    GuiState state;
    state.params = &params;

    HWND hwnd = CreateWindowExA(
        0,
        window_class.lpszClassName,
        "Langton Ant 2D (MPI) - Parametri",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        560,
        370,
        nullptr,
        nullptr,
        instance,
        &state);

    if (hwnd == nullptr) {
        throw std::runtime_error("Unable to create GUI window");
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return state.accepted;
}

int askIntValue(const std::string& label, int default_value, int min_value) {
    while (true) {
        std::cout << label << " [" << default_value << "]: ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("Input stream closed while reading interactive values");
        }

        if (line.empty()) {
            return default_value;
        }

        try {
            const long long value = std::stoll(line);
            if (value < min_value || value > std::numeric_limits<int>::max()) {
                std::cout << "Valoare invalida. Incearca din nou.\n";
                continue;
            }
            return static_cast<int>(value);
        } catch (const std::exception&) {
            std::cout << "Valoare invalida. Incearca din nou.\n";
        }
    }
}

std::string askStringValue(const std::string& label, const std::string& default_value) {
    while (true) {
        std::cout << label << " [" << default_value << "]: ";

        std::string line;
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("Input stream closed while reading interactive values");
        }

        if (line.empty()) {
            return default_value;
        }

        if (!line.empty()) {
            return line;
        }
    }
}

Params readInteractiveParams(const Params& defaults) {
    Params params = defaults;

    std::cout << "=== Interfata Langton 2D (MPI) ===\n";
    std::cout << "Apasa Enter pentru a pastra valorile implicite.\n\n";

    params.n = askIntValue("Dimensiune grila (n, > 0)", defaults.n, 1);
    params.t = askIntValue("Numar pasi (t, >= 0)", defaults.t, 0);
    params.out = askStringValue("Fisier output", defaults.out);
    params.help = false;

    std::cout << "\nParametri selectati: n=" << params.n << ", t=" << params.t
              << ", out=" << params.out << "\n\n";

    return params;
}

void broadcastParams(Params& params, int rank, MPI_Comm comm) {
    int values[6] = {
        params.n,
        params.t,
        params.ants,
        params.gather_every,
        params.help ? 1 : 0,
        params.ui ? 1 : 0
    };
    int out_len = rank == 0 ? static_cast<int>(params.out.size()) : 0;

    MPI_Bcast(values, 6, MPI_INT, 0, comm);
    MPI_Bcast(&out_len, 1, MPI_INT, 0, comm);

    std::vector<char> out_buffer(out_len + 1, '\0');
    if (rank == 0) {
        std::copy(params.out.begin(), params.out.end(), out_buffer.begin());
    }

    MPI_Bcast(out_buffer.data(), out_len + 1, MPI_CHAR, 0, comm);

    if (rank != 0) {
        params.n = values[0];
        params.t = values[1];
        params.ants = values[2];
        params.gather_every = values[3];
        params.help = values[4] != 0;
        params.ui = values[5] != 0;
        params.out.assign(out_buffer.data());
    }
}

int rowsForRank(int rank, int n, int size) {
    const int base = n / size;
    const int rem = n % size;
    return base + (rank < rem ? 1 : 0);
}

int startRowForRank(int rank, int n, int size) {
    const int base = n / size;
    const int rem = n % size;
    return rank * base + std::min(rank, rem);
}

int ownerRankForRow(int row, int n, int size) {
    const int wrapped_row = ((row % n) + n) % n;
    const int base = n / size;
    const int rem = n % size;
    const int split = (base + 1) * rem;

    if (wrapped_row < split) {
        return wrapped_row / (base + 1);
    }

    return rem + (wrapped_row - split) / base;
}

int wrapIndex(int value, int n) {
    return (value % n + n) % n;
}

void moveForward(Ant& ant, int n) {
    switch (ant.dir) {
    case 0:
        ant.x = wrapIndex(ant.x - 1, n);
        break;
    case 1:
        ant.y = wrapIndex(ant.y + 1, n);
        break;
    case 2:
        ant.x = wrapIndex(ant.x + 1, n);
        break;
    case 3:
        ant.y = wrapIndex(ant.y - 1, n);
        break;
    default:
        throw std::runtime_error("Invalid ant direction");
    }
}

void turnByCellColor(Ant& ant, int cell) {
    if (cell == 0) {
        ant.dir = (ant.dir + 1) % 4;
    } else {
        ant.dir = (ant.dir + 3) % 4;
    }
}

std::vector<int> flattenGrid(const Grid& grid, int n) {
    std::vector<int> flat(n * n, 0);
    for (int row = 0; row < n; ++row) {
        std::copy(grid.rowPtr(row), grid.rowPtr(row) + n, flat.begin() + row * n);
    }
    return flat;
}

void writePpm(const std::string& filename, const std::vector<int>& grid, int n) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Unable to open output file: " + filename);
    }

    out << "P6\n" << n << " " << n << "\n255\n";

    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const unsigned char value = grid[r * n + c] == 1 ? 0 : 255;
            out.write(reinterpret_cast<const char*>(&value), 1);
            out.write(reinterpret_cast<const char*>(&value), 1);
            out.write(reinterpret_cast<const char*>(&value), 1);
        }
    }
}

class Simulation {
public:
    explicit Simulation(Params params, bool enable_preview)
        : params_(std::move(params)),
          sequential_grid_(params_.n, params_.n, 0),
          ants_(createInitialAnts(params_.n, params_.ants)),
          enable_preview_(enable_preview) {}

        const std::string& outputPath() const { return params_.out; }

    void runSequential() {
        AntPreviewWindow preview(params_);
        const bool use_preview = enable_preview_ && preview.open();

        for (;;) {
            if (use_preview) {
                PreviewStats stats;
                stats.total_steps = params_.t;
                stats.ants = params_.ants;
                stats.black_cells = 0;
                stats.white_cells = params_.n * params_.n;
                stats.mode = "Sequential";
                stats.status = "Ready";
                preview.update(std::vector<int>(static_cast<size_t>(params_.n) * static_cast<size_t>(params_.n), 0), false, -1, -1, stats);
                preview.pumpMessages();

                while (!preview.hasStarted() && !preview.stopRequested() && !preview.consumeResetRequested()) {
                    preview.pumpMessages();
                    Sleep(20);
                }

                if (preview.stopRequested()) {
                    preview.waitUntilClosed();
                    return;
                }

                configure(preview.selectedParams());
            }

            sequential_grid_.clear();
            std::vector<Ant> ants = ants_;
            int step = 0;
            int executed_steps = 0;
            bool reset = false;
            const int preview_interval = std::max(1, params_.gather_every);
            const double start_time = MPI_Wtime();

            if (use_preview) {
                const bool has_ant = !ants.empty();
                const int ant_x = has_ant ? ants.front().x : -1;
                const int ant_y = has_ant ? ants.front().y : -1;
                PreviewStats stats;
                stats.step = 0;
                stats.total_steps = params_.t;
                stats.ants = static_cast<int>(ants.size());
                stats.black_cells = 0;
                stats.white_cells = params_.n * params_.n;
                stats.elapsed_seconds = 0.0;
                stats.steps_per_sec = 0.0;
                stats.mode = "Sequential";
                stats.status = "Running";
                stats.ant_dir = has_ant ? ants.front().dir : -1;
                preview.update(flattenGrid(sequential_grid_, params_.n), has_ant, ant_x, ant_y, stats);
                preview.pumpMessages();
            }

            std::vector<std::uint8_t> flip_mask(static_cast<size_t>(params_.n) * static_cast<size_t>(params_.n), 0);
            while (step < params_.t) {
                if (use_preview) {
                    preview.pumpMessages();
                    if (preview.consumeResetRequested()) {
                        reset = true;
                        break;
                    }
                    while (preview.isPaused() && !preview.stopRequested()) {
                        preview.pumpMessages();
                        if (preview.consumeResetRequested()) {
                            reset = true;
                            break;
                        }
                        Sleep(20);
                    }
                    if (reset || preview.stopRequested()) {
                        break;
                    }
                    if (preview.stepModeEnabled() && !preview.consumeStepOnceRequested()) {
                        Sleep(10);
                        continue;
                    }
                }

                const int iter_count = use_preview ? preview.speedMultiplier() : 1;
                for (int iter = 0; iter < iter_count && step < params_.t; ++iter) {
                    std::fill(flip_mask.begin(), flip_mask.end(), 0);
                    for (Ant& ant : ants) {
                        const int storage_row = sequential_grid_.storageRowForGlobal(ant.x, 0);
                        const int cell = sequential_grid_.at(storage_row, ant.y);
                        flip_mask[static_cast<size_t>(storage_row) * static_cast<size_t>(params_.n) + static_cast<size_t>(ant.y)] ^= 1u;
                        turnByCellColor(ant, cell);
                        moveForward(ant, params_.n);
                    }

                    for (int row = 0; row < params_.n; ++row) {
                        for (int col = 0; col < params_.n; ++col) {
                            const size_t idx = static_cast<size_t>(row) * static_cast<size_t>(params_.n) + static_cast<size_t>(col);
                            if (flip_mask[idx] != 0) {
                                sequential_grid_.at(row, col) = 1 - sequential_grid_.at(row, col);
                            }
                        }
                    }

                    ants = resolveConflicts(std::move(ants));
                    ++step;
                    executed_steps = step;
                }

                if (use_preview && (step % preview_interval == 0 || step == params_.t)) {
                    const bool has_ant = !ants.empty();
                    const int ant_x = has_ant ? ants.front().x : -1;
                    const int ant_y = has_ant ? ants.front().y : -1;
                    std::vector<int> flat = flattenGrid(sequential_grid_, params_.n);
                    const int black = static_cast<int>(std::count(flat.begin(), flat.end(), 1));
                    PreviewStats stats;
                    stats.step = step;
                    stats.total_steps = params_.t;
                    stats.ants = static_cast<int>(ants.size());
                    stats.black_cells = black;
                    stats.white_cells = params_.n * params_.n - black;
                    stats.elapsed_seconds = MPI_Wtime() - start_time;
                    stats.steps_per_sec = stats.elapsed_seconds > 0.0 ? static_cast<double>(stats.step) / stats.elapsed_seconds : 0.0;
                    stats.mode = "Sequential";
                    stats.status = "Running";
                    stats.ant_dir = has_ant ? ants.front().dir : -1;
                    preview.update(flat, has_ant, ant_x, ant_y, stats);
                    preview.pumpMessages();
                }
            }

            if (reset) {
                continue;
            }

            writePpm(params_.out, flattenGrid(sequential_grid_, params_.n), params_.n);
            if (use_preview) {
                PreviewStats done;
                done.step = executed_steps;
                done.total_steps = params_.t;
                done.ants = static_cast<int>(ants.size());
                done.mode = "Sequential";
                done.status = preview.stopRequested() ? "Stopped" : "Finished";
                std::vector<int> flat = flattenGrid(sequential_grid_, params_.n);
                done.black_cells = static_cast<int>(std::count(flat.begin(), flat.end(), 1));
                done.white_cells = params_.n * params_.n - done.black_cells;
                done.elapsed_seconds = MPI_Wtime() - start_time;
                done.steps_per_sec = done.elapsed_seconds > 0.0 ? static_cast<double>(std::max(1, executed_steps)) / done.elapsed_seconds : 0.0;
                const bool has_ant = !ants.empty();
                done.ant_dir = has_ant ? ants.front().dir : -1;
                preview.update(flat, has_ant, has_ant ? ants.front().x : -1, has_ant ? ants.front().y : -1, done);
                preview.pumpMessages();
                preview.waitUntilClosed();
            }
            return;
        }
    }

    void runMpi(int rank, int size, MPI_Comm comm) {
        AntPreviewWindow preview(params_);
        const bool use_preview = enable_preview_ && rank == 0 && preview.open();

        if (use_preview) {
            PreviewStats stats;
            stats.total_steps = params_.t;
            stats.mode = "MPI (" + std::to_string(size) + " processes)";
            stats.status = "Ready";
            stats.rank = rank;
            stats.size = size;
            stats.white_cells = params_.n * params_.n;
            preview.update(std::vector<int>(static_cast<size_t>(params_.n) * static_cast<size_t>(params_.n), 0), false, -1, -1, stats);
            preview.pumpMessages();
        }

        int started_flag = enable_preview_ ? 0 : 1;
        int pre_stop_flag = 0;
        while (started_flag == 0 && pre_stop_flag == 0) {
            if (rank == 0 && use_preview) {
                preview.pumpMessages();
                started_flag = preview.hasStarted() ? 1 : 0;
                pre_stop_flag = preview.stopRequested() ? 1 : 0;
            }
            MPI_Bcast(&started_flag, 1, MPI_INT, 0, comm);
            MPI_Bcast(&pre_stop_flag, 1, MPI_INT, 0, comm);
            Sleep(20);
        }

        if (pre_stop_flag != 0) {
            if (rank == 0 && use_preview) {
                preview.waitUntilClosed();
            }
            return;
        }

        if (rank == 0 && use_preview) {
            params_ = preview.selectedParams();
        }
        broadcastParams(params_, rank, comm);
        configure(params_);

        if (params_.n < size) {
            throw std::runtime_error("Grid size must be at least the number of MPI processes");
        }

        const int local_rows = rowsForRank(rank, params_.n, size);
        const int start_row = startRowForRank(rank, params_.n, size);
        const int padded_rows = rowsForRank(0, params_.n, size);

        Grid local_grid(params_.n, local_rows + 2, 1);
        local_grid.clear();

        std::vector<Ant> local_ants;
        local_ants.reserve(ants_.size());
        for (const Ant& ant : ants_) {
            if (ownerRankForRow(ant.x, params_.n, size) == rank) {
                local_ants.push_back(ant);
            }
        }

        const int prev = (rank - 1 + size) % size;
        const int next = (rank + 1) % size;

        MPI_Datatype packet_type;
        MPI_Type_contiguous(5, MPI_INT, &packet_type);
        MPI_Type_commit(&packet_type);

        MPI_Barrier(comm);
        const double start_time = MPI_Wtime();
        const int preview_interval = std::max(1, params_.gather_every);

        std::vector<std::uint8_t> flip_mask(static_cast<size_t>(local_rows) * static_cast<size_t>(params_.n), 0);
        int comm_events = 0;
        int migrations = 0;
        for (int step = 0; step < params_.t; ++step) {
            int pause_flag = 0;
            int stop_flag = 0;
            int reset_flag = 0;
            int step_mode_flag = 0;
            int step_once_flag = 0;
            if (rank == 0 && use_preview) {
                preview.pumpMessages();
                pause_flag = preview.isPaused() ? 1 : 0;
                stop_flag = preview.stopRequested() ? 1 : 0;
                reset_flag = preview.consumeResetRequested() ? 1 : 0;
                step_mode_flag = preview.stepModeEnabled() ? 1 : 0;
                step_once_flag = preview.consumeStepOnceRequested() ? 1 : 0;
            }
            MPI_Bcast(&pause_flag, 1, MPI_INT, 0, comm);
            MPI_Bcast(&stop_flag, 1, MPI_INT, 0, comm);
            MPI_Bcast(&reset_flag, 1, MPI_INT, 0, comm);
            MPI_Bcast(&step_mode_flag, 1, MPI_INT, 0, comm);
            MPI_Bcast(&step_once_flag, 1, MPI_INT, 0, comm);

            if (reset_flag != 0) {
                break;
            }

            while (pause_flag != 0 && stop_flag == 0) {
                if (rank == 0 && use_preview) {
                    preview.pumpMessages();
                    pause_flag = preview.isPaused() ? 1 : 0;
                    stop_flag = preview.stopRequested() ? 1 : 0;
                    reset_flag = preview.consumeResetRequested() ? 1 : 0;
                    step_mode_flag = preview.stepModeEnabled() ? 1 : 0;
                    step_once_flag = preview.consumeStepOnceRequested() ? 1 : 0;
                }
                MPI_Bcast(&pause_flag, 1, MPI_INT, 0, comm);
                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, comm);
                MPI_Bcast(&reset_flag, 1, MPI_INT, 0, comm);
                MPI_Bcast(&step_mode_flag, 1, MPI_INT, 0, comm);
                MPI_Bcast(&step_once_flag, 1, MPI_INT, 0, comm);
                Sleep(20);
            }

            if (step_mode_flag != 0 && step_once_flag == 0) {
                Sleep(5);
                --step;
                continue;
            }

            if (stop_flag != 0) {
                break;
            }

            exchangeGhostRows(local_grid, local_rows, rank, size, comm);

            std::fill(flip_mask.begin(), flip_mask.end(), 0);
            std::vector<Ant> next_local_ants;
            next_local_ants.reserve(local_ants.size());
            std::vector<AntPacket> send_to_prev;
            std::vector<AntPacket> send_to_next;

            for (Ant ant : local_ants) {
                const int storage_row = local_grid.storageRowForGlobal(ant.x, start_row);
                const int cell = local_grid.at(storage_row, ant.y);
                const size_t idx = static_cast<size_t>(storage_row - 1) * static_cast<size_t>(params_.n) + static_cast<size_t>(ant.y);
                flip_mask[idx] ^= 1u;
                turnByCellColor(ant, cell);
                moveForward(ant, params_.n);

                const int owner = ownerRankForRow(ant.x, params_.n, size);
                if (owner == rank) {
                    next_local_ants.push_back(ant);
                } else if (owner == prev) {
                    send_to_prev.push_back(packPacket(ant));
                } else if (owner == next) {
                    send_to_next.push_back(packPacket(ant));
                } else {
                    MPI_Type_free(&packet_type);
                    throw std::runtime_error("Ant moved to a non-adjacent rank");
                }
            }

            for (int r = 0; r < local_rows; ++r) {
                for (int c = 0; c < params_.n; ++c) {
                    const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(params_.n) + static_cast<size_t>(c);
                    if (flip_mask[idx] != 0) {
                        local_grid.at(r + 1, c) = 1 - local_grid.at(r + 1, c);
                    }
                }
            }

            MPI_Request requests[2];
            MPI_Isend(send_to_prev.empty() ? nullptr : send_to_prev.data(),
                static_cast<int>(send_to_prev.size()), packet_type, prev, TAG_TO_PREV, comm, &requests[0]);
            MPI_Isend(send_to_next.empty() ? nullptr : send_to_next.data(),
                static_cast<int>(send_to_next.size()), packet_type, next, TAG_TO_NEXT, comm, &requests[1]);

            MPI_Status status_prev;
            MPI_Status status_next;
            MPI_Probe(prev, TAG_TO_NEXT, comm, &status_prev);
            MPI_Probe(next, TAG_TO_PREV, comm, &status_next);
            comm_events += 4;

            int recv_count_prev = 0;
            int recv_count_next = 0;
            MPI_Get_count(&status_prev, packet_type, &recv_count_prev);
            MPI_Get_count(&status_next, packet_type, &recv_count_next);

            std::vector<AntPacket> recv_from_prev(static_cast<size_t>(std::max(0, recv_count_prev)));
            std::vector<AntPacket> recv_from_next(static_cast<size_t>(std::max(0, recv_count_next)));
            MPI_Recv(recv_from_prev.empty() ? nullptr : recv_from_prev.data(), recv_count_prev, packet_type,
                prev, TAG_TO_NEXT, comm, MPI_STATUS_IGNORE);
            MPI_Recv(recv_from_next.empty() ? nullptr : recv_from_next.data(), recv_count_next, packet_type,
                next, TAG_TO_PREV, comm, MPI_STATUS_IGNORE);
            MPI_Waitall(2, requests, MPI_STATUSES_IGNORE);
            comm_events += 4;
            migrations += static_cast<int>(send_to_prev.size() + send_to_next.size() + recv_from_prev.size() + recv_from_next.size());

            for (const AntPacket& packet : recv_from_prev) {
                if (packet.active != 0) {
                    next_local_ants.push_back(unpackPacket(packet));
                }
            }
            for (const AntPacket& packet : recv_from_next) {
                if (packet.active != 0) {
                    next_local_ants.push_back(unpackPacket(packet));
                }
            }

            local_ants = resolveConflicts(std::move(next_local_ants));

            if (enable_preview_ && (step % preview_interval == 0 || step == params_.t - 1)) {
                std::vector<int> preview_grid = gatherGrid(local_grid, local_rows, padded_rows, rank, size, comm);
                AntPacket local_ant_packet = !local_ants.empty() ? packPacket(local_ants.front()) : AntPacket{};
                std::vector<AntPacket> ant_packets(rank == 0 ? size : 0);
                int local_count = static_cast<int>(local_ants.size());
                int global_count = 0;

                MPI_Gather(
                    &local_ant_packet,
                    1,
                    packet_type,
                    rank == 0 ? ant_packets.data() : nullptr,
                    1,
                    packet_type,
                    0,
                    comm);

                MPI_Reduce(&local_count, &global_count, 1, MPI_INT, MPI_SUM, 0, comm);

                if (use_preview) {
                    bool ant_found = false;
                    int ant_x = -1;
                    int ant_y = -1;
                    int ant_dir = -1;
                    for (const AntPacket& packet : ant_packets) {
                        if (packet.active != 0) {
                            ant_found = true;
                            ant_x = packet.x;
                            ant_y = packet.y;
                            ant_dir = packet.dir;
                            break;
                        }
                    }
                    PreviewStats stats;
                    stats.step = step + 1;
                    stats.total_steps = params_.t;
                    stats.rank = rank;
                    stats.size = size;
                    stats.mode = "MPI (" + std::to_string(size) + " processes)";
                    stats.status = "Running";
                    stats.elapsed_seconds = MPI_Wtime() - start_time;
                    stats.steps_per_sec = stats.elapsed_seconds > 0.0 ? static_cast<double>(stats.step) / stats.elapsed_seconds : 0.0;
                    stats.black_cells = static_cast<int>(std::count(preview_grid.begin(), preview_grid.end(), 1));
                    stats.white_cells = params_.n * params_.n - stats.black_cells;
                    stats.ants = global_count;
                    stats.comm_events = comm_events;
                    stats.migrations = migrations;
                    stats.ant_dir = ant_dir;

                    preview.update(preview_grid, ant_found, ant_x, ant_y, stats);
                    preview.pumpMessages();
                }
            }
        }

        MPI_Barrier(comm);
        const double elapsed = MPI_Wtime() - start_time;

        std::vector<int> gathered = gatherGrid(local_grid, local_rows, padded_rows, rank, size, comm);

        if (rank == 0) {
            writePpm(params_.out, gathered, params_.n);
            std::cout << "Final image saved to " << params_.out << "\n";
            std::cout << "MPI simulation time: " << elapsed << " seconds\n";
            if (use_preview) {
                PreviewStats done;
                done.step = params_.t;
                done.total_steps = params_.t;
                done.rank = rank;
                done.size = size;
                done.mode = "MPI (" + std::to_string(size) + " processes)";
                done.status = "Finished";
                done.elapsed_seconds = elapsed;
                done.steps_per_sec = elapsed > 0.0 ? static_cast<double>(params_.t) / elapsed : 0.0;
                done.black_cells = static_cast<int>(std::count(gathered.begin(), gathered.end(), 1));
                done.white_cells = params_.n * params_.n - done.black_cells;

                const bool ant_found = false;
                const int ant_x = -1;
                const int ant_y = -1;
                done.ants = static_cast<int>(ants_.size());
                preview.update(gathered, ant_found, ant_x, ant_y, done);
                preview.pumpMessages();
                preview.waitUntilClosed();
            }
        }

        MPI_Type_free(&packet_type);
    }

private:
    static constexpr int TAG_TO_PREV = 101;
    static constexpr int TAG_TO_NEXT = 102;

    void configure(const Params& params) {
        params_ = params;
        sequential_grid_ = Grid(params_.n, params_.n, 0);
        ants_ = createInitialAnts(params_.n, params_.ants);
    }

    static std::vector<Ant> createInitialAnts(int n, int ant_count) {
        std::vector<Ant> ants;
        ants.reserve(static_cast<size_t>(ant_count));

        const int cx = n / 2;
        const int cy = n / 2;
        for (int i = 0; i < ant_count; ++i) {
            Ant ant;
            ant.id = i;
            ant.x = wrapIndex(cx + (i * 37) % n, n);
            ant.y = wrapIndex(cy + (i * 53) % n, n);
            ant.dir = i % 4;
            ants.push_back(ant);
        }

        return ants;
    }

    static std::vector<Ant> resolveConflicts(std::vector<Ant> ants) {
        std::sort(ants.begin(), ants.end(), [](const Ant& a, const Ant& b) {
            if (a.x != b.x) {
                return a.x < b.x;
            }
            if (a.y != b.y) {
                return a.y < b.y;
            }
            return a.id < b.id;
        });

        std::vector<Ant> unique_ants;
        unique_ants.reserve(ants.size());
        for (const Ant& ant : ants) {
            if (!unique_ants.empty() && unique_ants.back().x == ant.x && unique_ants.back().y == ant.y) {
                continue;
            }
            unique_ants.push_back(ant);
        }

        return unique_ants;
    }

    static void exchangeGhostRows(Grid& grid, int local_rows, int rank, int size, MPI_Comm comm) {
        if (size == 1) {
            return;
        }

        const int prev = (rank - 1 + size) % size;
        const int next = (rank + 1) % size;

        MPI_Sendrecv(
            grid.rowPtr(1),
            grid.n(),
            MPI_INT,
            prev,
            10,
            grid.rowPtr(local_rows + 1),
            grid.n(),
            MPI_INT,
            next,
            10,
            comm,
            MPI_STATUS_IGNORE);

        MPI_Sendrecv(
            grid.rowPtr(local_rows),
            grid.n(),
            MPI_INT,
            next,
            11,
            grid.rowPtr(0),
            grid.n(),
            MPI_INT,
            prev,
            11,
            comm,
            MPI_STATUS_IGNORE);
    }

    static AntPacket packPacket(const Ant& ant) {
        return AntPacket{1, ant.id, ant.x, ant.y, ant.dir};
    }

    static Ant unpackPacket(const AntPacket& packet) {
        return Ant{packet.id, packet.x, packet.y, packet.dir};
    }

    std::vector<int> gatherGrid(
        const Grid& local_grid,
        int local_rows,
        int padded_rows,
        int rank,
        int size,
        MPI_Comm comm) const {
        std::vector<int> local_send(padded_rows * local_grid.n(), 0);

        for (int r = 0; r < local_rows; ++r) {
            std::copy(
                local_grid.rowPtr(r + 1),
                local_grid.rowPtr(r + 1) + local_grid.n(),
                local_send.begin() + r * local_grid.n());
        }

        std::vector<int> gathered;
        if (rank == 0) {
            gathered.resize(size * padded_rows * local_grid.n(), 0);
        }

        MPI_Gather(
            local_send.data(),
            padded_rows * local_grid.n(),
            MPI_INT,
            rank == 0 ? gathered.data() : nullptr,
            padded_rows * local_grid.n(),
            MPI_INT,
            0,
            comm);

        if (rank != 0) {
            return {};
        }

        std::vector<int> global(local_grid.n() * local_grid.n(), 0);
        for (int proc = 0; proc < size; ++proc) {
            const int rows = rowsForRank(proc, local_grid.n(), size);
            const int row_start = startRowForRank(proc, local_grid.n(), size);
            const int* block = gathered.data() + proc * padded_rows * local_grid.n();

            for (int r = 0; r < rows; ++r) {
                std::copy(
                    block + r * local_grid.n(),
                    block + r * local_grid.n() + local_grid.n(),
                    global.begin() + (row_start + r) * local_grid.n());
            }
        }

        return global;
    }

    Params params_;
    Grid sequential_grid_;
    std::vector<Ant> ants_;
    bool enable_preview_ = false;
};

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Params params;
    const bool interactive_mode = argc == 1;

    if (!interactive_mode) {
        try {
            params = parseArgs(argc, argv);
        } catch (const std::exception& ex) {
            if (rank == 0) {
                std::cerr << "Argument error: " << ex.what() << '\n';
            }
            MPI_Finalize();
            return 1;
        }

        if (params.cli && rank == 0) {
            try {
                params = readInteractiveParams(params);
            } catch (const std::exception& ex) {
                std::cerr << "Interactive input error: " << ex.what() << '\n';
                MPI_Abort(MPI_COMM_WORLD, 1);
                return 1;
            }
        }
    } else if (rank == 0) {
        std::cout << "Interactive dashboard mode: folosesc parametrii impliciti"
                  << " (n=" << params.n
                  << ", t=" << params.t
                  << ", ants=" << params.ants
                  << ", gather=" << params.gather_every
                  << ", out=" << params.out << ").\n";
    }

    broadcastParams(params, rank, MPI_COMM_WORLD);

    if (params.help) {
        if (rank == 0) {
            std::cout << "Usage: mpirun -np <proc> ./langton [-n size] [-t steps] [-out file.ppm] [--cli]\n";
            std::cout << "       mpirun -np <proc> ./langton [-ants count] [-k gather_steps]\n";
            std::cout << "       mpirun -np <proc> ./langton [--ui|--no-ui]\n";
            std::cout << "Defaults: -n 100 -t 10000 -ants 1 -k 100 -out langton.ppm\n";
            std::cout << "Interactive mode (GUI): run without arguments.\n";
            std::cout << "Interactive mode (CLI): run with --cli.\n";
        }
        MPI_Finalize();
        return 0;
    }

    try {
        const bool enable_preview = params.ui;
        Simulation simulation(params, enable_preview);

        if (size == 1) {
            simulation.runSequential();
            if (rank == 0) {
                std::cout << "Final image saved to " << simulation.outputPath() << '\n';
            }
        } else {
            simulation.runMpi(rank, size, MPI_COMM_WORLD);
        }
    } catch (const std::exception& ex) {
        if (rank == 0) {
            std::cerr << "Simulation error: " << ex.what() << '\n';
        }
        MPI_Finalize();
        return 1;
    }

    MPI_Finalize();
    return 0;
}
