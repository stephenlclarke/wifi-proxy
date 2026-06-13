#include <SDL.h>
#include <lvgl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kScreenWidth = 240;
constexpr int kScreenHeight = 536;
constexpr int kWindowScale = 1;
constexpr uint32_t kDisplayRefreshMs = 1000;
constexpr uint32_t kUiBackgroundColor = 0x080908;
constexpr uint32_t kUiPanelColor = 0x181a18;
constexpr uint32_t kUiTextColor = 0xf5f5f2;
constexpr uint32_t kUiGreenColor = 0x56d98d;
constexpr uint32_t kUiCyanColor = 0x52c7e8;
constexpr uint32_t kUiOrangeColor = 0xffcf5c;
constexpr uint32_t kUiRedColor = 0xff6b6b;
constexpr size_t kUiLineCount = 5;

struct UiState {
    bool setupMode = false;
    bool upstreamConnected = true;
    bool guestAccessActive = false;
    bool naptEnabled = false;
    uint8_t screenPage = 0;
    int clients = 2;
    int sessionMinutes = 120;
    uint32_t remainingSeconds = 91 * 60;
    std::string guestApSsid = "Home Guest";
    std::string setupApSsid = "wifi-proxy-setup";
};

std::vector<uint16_t> framebuffer(kScreenWidth * kScreenHeight);
lv_obj_t* headerBar = nullptr;
lv_obj_t* titleLabel = nullptr;
std::array<lv_obj_t*, kUiLineCount> lineLabels = {};
lv_obj_t* footerBar = nullptr;
lv_obj_t* pageButtonWidget = nullptr;
lv_obj_t* actionButtonWidget = nullptr;
lv_obj_t* actionButtonLabel = nullptr;
UiState state;
bool mousePressed = false;
int mouseX = 0;
int mouseY = 0;

std::string formatDuration(uint32_t totalSeconds) {
    const uint32_t hours = totalSeconds / 3600;
    const uint32_t minutes = (totalSeconds % 3600) / 60;
    const uint32_t seconds = totalSeconds % 60;

    char buffer[16];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%luh %02lum",
                      static_cast<unsigned long>(hours),
                      static_cast<unsigned long>(minutes));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
                      static_cast<unsigned long>(minutes),
                      static_cast<unsigned long>(seconds));
    }
    return buffer;
}

std::string gatewayModeText() {
    if (state.setupMode) {
        return "setup mode";
    }
    if (!state.guestAccessActive) {
        return "closed";
    }
    if (!state.upstreamConnected) {
        return "waiting for upstream";
    }
    return state.naptEnabled ? "guest access open" : "opening gateway";
}

std::string batteryText() {
    return "Battery: 3980 mV USB in";
}

void displayFlush(lv_disp_drv_t* displayDriver, const lv_area_t* area, lv_color_t* colorP) {
    const int32_t x1 = std::max<int32_t>(0, area->x1);
    const int32_t y1 = std::max<int32_t>(0, area->y1);
    const int32_t x2 = std::min<int32_t>(kScreenWidth - 1, area->x2);
    const int32_t y2 = std::min<int32_t>(kScreenHeight - 1, area->y2);

    for (int32_t y = y1; y <= y2; ++y) {
        for (int32_t x = x1; x <= x2; ++x) {
            const int32_t sourceIndex = (y - area->y1) * lv_area_get_width(area) + (x - area->x1);
            framebuffer[y * kScreenWidth + x] = colorP[sourceIndex].full;
        }
    }

    lv_disp_flush_ready(displayDriver);
}

void pointerRead(lv_indev_drv_t*, lv_indev_data_t* data) {
    data->point.x = static_cast<lv_coord_t>(mouseX);
    data->point.y = static_cast<lv_coord_t>(mouseY);
    data->state = mousePressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

void setTextColor(lv_obj_t* obj, uint32_t color) {
    lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN);
}

void stylePanel(lv_obj_t* obj, uint32_t color) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeFooterButton(lv_obj_t* parent, const char* text, lv_align_t align) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, 112, 34);
    lv_obj_align(button, align, align == LV_ALIGN_RIGHT_MID ? -8 : 8, 0);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2f80ed), LV_PART_MAIN);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return button;
}

void drawStatus();

void handlePageAction() {
    state.screenPage = (state.screenPage + 1) % 4;
    drawStatus();
}

void handlePrimaryAction() {
    if (state.setupMode) {
        state.setupMode = false;
        state.screenPage = 0;
    } else {
        state.guestAccessActive = !state.guestAccessActive;
        state.naptEnabled = state.guestAccessActive && state.upstreamConnected;
        if (state.guestAccessActive && state.remainingSeconds == 0) {
            state.remainingSeconds = static_cast<uint32_t>(state.sessionMinutes * 60);
        }
    }
    drawStatus();
}

void onPageButtonClicked(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        handlePageAction();
    }
}

void onActionButtonClicked(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        handlePrimaryAction();
    }
}

void createDisplayUi() {
    lv_obj_t* screen = lv_scr_act();
    stylePanel(screen, kUiBackgroundColor);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    headerBar = lv_obj_create(screen);
    stylePanel(headerBar, kUiCyanColor);
    lv_obj_set_size(headerBar, kScreenWidth, 34);
    lv_obj_align(headerBar, LV_ALIGN_TOP_MID, 0, 0);

    titleLabel = lv_label_create(headerBar);
    lv_obj_set_width(titleLabel, kScreenWidth - 24);
    lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, LV_PART_MAIN);
    setTextColor(titleLabel, 0x101010);
    lv_obj_align(titleLabel, LV_ALIGN_LEFT_MID, 12, 0);

    for (size_t i = 0; i < kUiLineCount; ++i) {
        lineLabels[i] = lv_label_create(screen);
        lv_obj_set_width(lineLabels[i], kScreenWidth - 24);
        lv_label_set_long_mode(lineLabels[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(lineLabels[i], &lv_font_montserrat_16, LV_PART_MAIN);
        setTextColor(lineLabels[i], kUiTextColor);
        lv_obj_align(lineLabels[i], LV_ALIGN_TOP_LEFT, 12, 46 + static_cast<int>(i) * 27);
    }

    footerBar = lv_obj_create(screen);
    stylePanel(footerBar, kUiPanelColor);
    lv_obj_set_size(footerBar, kScreenWidth, 44);
    lv_obj_align(footerBar, LV_ALIGN_BOTTOM_MID, 0, 0);

    pageButtonWidget = makeFooterButton(footerBar, "PAGE", LV_ALIGN_LEFT_MID);
    lv_obj_add_event_cb(pageButtonWidget, onPageButtonClicked, LV_EVENT_CLICKED, nullptr);

    actionButtonWidget = makeFooterButton(footerBar, "START", LV_ALIGN_RIGHT_MID);
    lv_obj_add_event_cb(actionButtonWidget, onActionButtonClicked, LV_EVENT_CLICKED, nullptr);
    actionButtonLabel = lv_obj_get_child(actionButtonWidget, 0);
}

void setUiContent(const std::string& title,
                  uint32_t headerColor,
                  const std::array<std::string, kUiLineCount>& lines,
                  const char* actionText,
                  uint32_t actionColor) {
    lv_obj_set_style_bg_color(headerBar, lv_color_hex(headerColor), LV_PART_MAIN);
    lv_label_set_text(titleLabel, title.c_str());

    for (size_t i = 0; i < kUiLineCount; ++i) {
        lv_label_set_text(lineLabels[i], lines[i].c_str());
        setTextColor(lineLabels[i], i == 3 ? kUiOrangeColor : kUiTextColor);
    }

    lv_label_set_text(actionButtonLabel, actionText);
    lv_obj_set_style_bg_color(actionButtonWidget, lv_color_hex(actionColor), LV_PART_MAIN);
}

void drawDashboard() {
    const bool open = state.guestAccessActive && state.naptEnabled;
    const std::array<std::string, kUiLineCount> lines = {
        std::string("Mode: ") + (state.setupMode ? "SETUP" : "GUEST"),
        std::string("AP: ") + (state.setupMode ? state.setupApSsid : state.guestApSsid),
        "Clients: " + std::to_string(state.clients),
        std::string("Access: ") +
            (state.guestAccessActive ? formatDuration(state.remainingSeconds) : "closed"),
        batteryText(),
    };
    setUiContent("Guest gateway",
                 state.setupMode ? kUiOrangeColor : (open ? kUiGreenColor : kUiCyanColor),
                 lines,
                 state.setupMode ? "REBOOT" : (state.guestAccessActive ? "STOP" : "START"),
                 state.setupMode ? kUiOrangeColor :
                     (state.guestAccessActive ? kUiRedColor : kUiGreenColor));
}

void drawUpstream() {
    const std::array<std::string, kUiLineCount> lines = {
        "SSID: HomeNet",
        state.upstreamConnected ? "IP: 192.168.1.42" : "IP: not connected",
        state.upstreamConnected ? "RSSI: -58 dBm" : "RSSI: -",
        state.upstreamConnected ? "Home Wi-Fi: connected" : "Home Wi-Fi: offline",
        "BOOT long press: setup mode",
    };
    setUiContent("Home Wi-Fi",
                 state.upstreamConnected ? kUiGreenColor : kUiOrangeColor,
                 lines,
                 state.setupMode ? "REBOOT" : (state.guestAccessActive ? "STOP" : "START"),
                 state.setupMode ? kUiOrangeColor :
                     (state.guestAccessActive ? kUiRedColor : kUiGreenColor));
}

void drawGuest() {
    const std::array<std::string, kUiLineCount> lines = {
        "Portal: 192.168.42.1",
        "Code: 123456",
        "Duration: " + std::to_string(state.sessionMinutes) + " min",
        "Gateway: " + gatewayModeText(),
        std::string("Remaining: ") +
            (state.guestAccessActive ? formatDuration(state.remainingSeconds) : "-"),
    };
    setUiContent("Guest access",
                 state.guestAccessActive ? kUiGreenColor : kUiRedColor,
                 lines,
                 state.setupMode ? "REBOOT" : (state.guestAccessActive ? "STOP" : "START"),
                 state.setupMode ? kUiOrangeColor :
                     (state.guestAccessActive ? kUiRedColor : kUiGreenColor));
}

void drawSetupHelp() {
    const std::array<std::string, kUiLineCount> lines = {
        "1. Join Wi-Fi:",
        state.setupApSsid,
        "2. Open:",
        "192.168.42.1",
        "Save settings, then reboot",
    };
    setUiContent("Owner setup",
                 kUiOrangeColor,
                 lines,
                 state.setupMode ? "REBOOT" : (state.guestAccessActive ? "STOP" : "START"),
                 state.setupMode ? kUiOrangeColor :
                     (state.guestAccessActive ? kUiRedColor : kUiGreenColor));
}

void drawStatus() {
    switch (state.screenPage) {
        case 0:
            drawDashboard();
            break;
        case 1:
            drawUpstream();
            break;
        case 2:
            drawGuest();
            break;
        default:
            drawSetupHelp();
            break;
    }
}

void installLvglDrivers() {
    static lv_color_t drawBuffer1[kScreenWidth * 40];
    static lv_color_t drawBuffer2[kScreenWidth * 40];
    static lv_disp_draw_buf_t drawBuffer;
    lv_disp_draw_buf_init(&drawBuffer, drawBuffer1, drawBuffer2, kScreenWidth * 40);

    static lv_disp_drv_t displayDriver;
    lv_disp_drv_init(&displayDriver);
    displayDriver.hor_res = kScreenWidth;
    displayDriver.ver_res = kScreenHeight;
    displayDriver.flush_cb = displayFlush;
    displayDriver.draw_buf = &drawBuffer;
    lv_disp_drv_register(&displayDriver);

    static lv_indev_drv_t pointerDriver;
    lv_indev_drv_init(&pointerDriver);
    pointerDriver.type = LV_INDEV_TYPE_POINTER;
    pointerDriver.read_cb = pointerRead;
    lv_indev_drv_register(&pointerDriver);
}

uint32_t exitAfterMs() {
    const char* value = std::getenv("SIM_EXIT_AFTER_MS");
    if (value == nullptr || *value == '\0') {
        return 0;
    }
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("wifi-proxy LilyGo UI simulator",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          kScreenWidth * kWindowScale,
                                          kScreenHeight * kWindowScale,
                                          SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer == nullptr) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                            SDL_PIXELFORMAT_RGB565,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            kScreenWidth,
                                            kScreenHeight);
    if (texture == nullptr) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    lv_init();
    installLvglDrivers();
    createDisplayUi();
    drawStatus();

    bool running = true;
    uint32_t lastTick = SDL_GetTicks();
    uint32_t lastRefresh = lastTick;
    const uint32_t startedAt = lastTick;
    const uint32_t autoExitMs = exitAfterMs();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event) != 0) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                mousePressed = event.type == SDL_MOUSEBUTTONDOWN;
                mouseX = event.button.x / kWindowScale;
                mouseY = event.button.y / kWindowScale;
            } else if (event.type == SDL_MOUSEMOTION) {
                mouseX = event.motion.x / kWindowScale;
                mouseY = event.motion.y / kWindowScale;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    handlePageAction();
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    handlePrimaryAction();
                } else if (event.key.keysym.sym == SDLK_s) {
                    state.setupMode = !state.setupMode;
                    drawStatus();
                } else if (event.key.keysym.sym == SDLK_u) {
                    state.upstreamConnected = !state.upstreamConnected;
                    state.naptEnabled = state.guestAccessActive && state.upstreamConnected;
                    drawStatus();
                }
            }
        }

        const uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - lastTick);
        lastTick = now;

        if (now - lastRefresh >= kDisplayRefreshMs) {
            lastRefresh = now;
            if (state.guestAccessActive && state.remainingSeconds > 0) {
                --state.remainingSeconds;
            }
            if (state.guestAccessActive && state.remainingSeconds == 0) {
                state.guestAccessActive = false;
                state.naptEnabled = false;
            }
            drawStatus();
        }

        lv_timer_handler();
        SDL_UpdateTexture(texture, nullptr, framebuffer.data(), kScreenWidth * sizeof(uint16_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        if (autoExitMs > 0 && now - startedAt >= autoExitMs) {
            running = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
