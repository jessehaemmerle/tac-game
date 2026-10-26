// Headless interface tests: every screen is built for a few frames without a renderer (Dear ImGui with
// a null backend), and the string tables are checked against the keys used in the source code.
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Localization.h"
#include "imgui.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"

using namespace it;

namespace {

struct HeadlessImGui {
    HeadlessImGui() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = {1600, 900};
        io.DeltaTime = 1.0f / 30.0f;
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ui::loadFonts(fs::dataPath("fonts").string());
        ui::applyTheme(1.0f);
    }
    ~HeadlessImGui() { ImGui::DestroyContext(); }
    template <typename F> void frame(F&& f) {
        ImGui::NewFrame();
        f();
        ImGui::Render();
        // Pretend to be a renderer backend: accept every texture request.
        for (ImTextureData* t : ImGui::GetPlatformIO().Textures) {
            if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
                t->SetTexID(ImTextureID(1));
                t->SetStatus(ImTextureStatus_OK);
            } else if (t->Status == ImTextureStatus_WantDestroy) {
                t->SetTexID(ImTextureID_Invalid);
                t->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
};

/// Runs one automated screen until its capture point; returns the number of frames used.
int runScreen(const ui::Automation& a, ui::GameUi::Screen* reached = nullptr) {
    HeadlessImGui im;
    ui::NullServices svc;
    ui::Settings settings;
    settings.language = "en";
    ui::GameUi gui(svc, settings);
    std::string err;
    REQUIRE_MESSAGE(gui.init(&err), err);
    gui.startAutomation(a);
    int frames = 0;
    while (!gui.automationReady() && frames < 200) {
        im.frame([&] { gui.frame(1.0 / 30.0); });
        ++frames;
        const auto& cam = gui.camera();
        CHECK(std::isfinite(cam.pos.x));
        CHECK(std::isfinite(cam.pos.y));
    }
    if (reached) *reached = gui.screen();
    return frames;
}

}  // namespace

TEST_CASE("UI: every screen builds headless") {
    struct Case {
        const char* screen;
        ui::GameUi::Screen expect;
    } cases[] = {{"menu", ui::GameUi::Screen::MainMenu},      {"missions", ui::GameUi::Screen::Missions},
                 {"briefing", ui::GameUi::Screen::Briefing},  {"unitref", ui::GameUi::Screen::UnitReference},
                 {"editor", ui::GameUi::Screen::Editor},      {"settings", ui::GameUi::Screen::Settings},
                 {"credits", ui::GameUi::Screen::Credits},    {"battle", ui::GameUi::Screen::Battle},
                 {"map", ui::GameUi::Screen::Battle},         {"split", ui::GameUi::Screen::Battle},
                 {"debrief", ui::GameUi::Screen::Debrief}};
    for (const auto& c : cases) {
        CAPTURE(c.screen);
        ui::Automation a;
        a.screen = c.screen;
        a.frames = 4;
        a.advance = std::string(c.screen) == "debrief" ? 900.0 : 300.0;
        a.side = "axis";
        a.insight = true;
        a.unitClass = "usn_ca_new_orleans";
        ui::GameUi::Screen reached{};
        int frames = runScreen(a, &reached);
        CHECK(frames < 200);
        CHECK(reached == c.expect);
    }
}

TEST_CASE("UI: umpire (AI vs AI) battle runs with all panels") {
    ui::Automation a;
    a.screen = "split";
    a.side = "none";
    a.advance = 600.0;
    a.frames = 6;
    a.insight = true;
    CHECK(runScreen(a) < 200);
}

TEST_CASE("Localisation: German and English tables are complete and consistent") {
    auto en = loadJsonFile(fs::dataPath("loc/en.json"));
    auto de = loadJsonFile(fs::dataPath("loc/de.json"));
    REQUIRE(en);
    REQUIRE(de);
    std::set<std::string> ke, kd;
    for (auto& [k, v] : en->items()) {
        ke.insert(k);
        CHECK_MESSAGE(!v.get<std::string>().empty(), k);
    }
    for (auto& [k, v] : de->items()) {
        kd.insert(k);
        CHECK_MESSAGE(!v.get<std::string>().empty(), k);
    }
    CHECK(ke == kd);
    // Placeholders must match between languages.
    std::regex ph(R"(\{\d\})");
    for (auto& k : ke) {
        std::string a = (*en)[k].get<std::string>(), b = (*de)[k].get<std::string>();
        std::set<std::string> pa, pb;
        for (auto it = std::sregex_iterator(a.begin(), a.end(), ph); it != std::sregex_iterator(); ++it) pa.insert(it->str());
        for (auto it = std::sregex_iterator(b.begin(), b.end(), ph); it != std::sregex_iterator(); ++it) pb.insert(it->str());
        CHECK_MESSAGE(pa == pb, k);
    }
}

TEST_CASE("Localisation: every key used in the source exists") {
    auto en = loadJsonFile(fs::dataPath("loc/en.json"));
    REQUIRE(en);
    std::filesystem::path src = std::filesystem::path(IRONTIDE_SOURCE_DIR) / "src";
    std::regex use(R"((?:tr|loc::format|textColored\(col::k\w+, "%s", tr)\(\s*"([a-z_]+\.[a-z0-9_.]+)\")");
    int checked = 0;
    for (auto& e : std::filesystem::recursive_directory_iterator(src)) {
        if (e.path().extension() != ".cpp") continue;
        std::ifstream f(e.path());
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        for (auto it = std::sregex_iterator(text.begin(), text.end(), use); it != std::sregex_iterator(); ++it) {
            std::string key = (*it)[1].str();
            CHECK_MESSAGE(en->contains(key), key << " in " << e.path().filename().string());
            ++checked;
        }
    }
    CHECK(checked > 300);
}
