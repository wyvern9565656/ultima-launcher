#include "raylib.h"
#include <json/json.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kSidebarWidth = 190;
constexpr int kCardWidth = 270;
constexpr int kCardHeight = 360;
constexpr int kCardGap = 18;

const Color kBackground{16, 20, 23, 255};
const Color kSidebar{22, 28, 31, 255};
const Color kPanel{29, 37, 39, 255};
const Color kPanelHover{39, 49, 50, 255};
const Color kText{232, 237, 232, 255};
const Color kMuted{143, 157, 151, 255};
const Color kAccent{113, 204, 157, 255};
const Color kCoral{237, 137, 111, 255};

struct Game {
    Json::Value data;
    Texture2D cover{};
    Texture2D icon{};
    bool hasCover = false;
    bool hasIcon = false;
};

struct ChildProcess {
    pid_t pid = -1;
    size_t gameIndex = 0;
    std::time_t startedAt = 0;
};

struct App {
    fs::path appDir;
    fs::path gamesFile;
    fs::path sessionsFile;
    Json::Value root;
    Json::Value sessions;
    std::vector<Game> games;
    std::vector<ChildProcess> children;
    std::string search;
    std::string notice;
    std::string noticeError;
    bool editing = false;
    int editingIndex = -1;
    std::array<std::string, 7> fields{};
    int focusedField = 0;
    int selected = -1;
    int scrollRow = 0;
    int page = 0;
    double noticeUntil = 0;
    Font font{};
    Texture2D appIcon{};
    Texture2D background{};
    bool hasAppIcon = false;
    bool hasBackground = false;
    bool customFont = false;
    bool searchFocused = false;
    Color accent{246, 112, 235, 255};
    Color cardColor{0, 0, 0, 77};
    Color borderColor{255, 255, 255, 255};
    Color buttonColor{255, 255, 255, 77};
    Color buttonTextColor{255, 255, 255, 255};
    Color buttonBorderColor{58, 58, 74, 255};
    Color buttonHoverColor{246, 112, 235, 77};
    int backgroundOpacity = 100;
    int cardOpacity = 30;
    int borderWidth = 10;
    int borderRadius = 20;
    int buttonBorderWidth = 1;
    int buttonBorderRadius = 8;
};

std::string value(const Json::Value& object, const char* key,
                  const std::string& fallback = "") {
    return object.isMember(key) && object[key].isString()
        ? object[key].asString() : fallback;
}

long long number(const Json::Value& object, const char* key) {
    return object.isMember(key) && object[key].isNumeric()
        ? object[key].asInt64() : 0;
}

int settingInt(const Json::Value& settings, const char* key, int fallback) {
    return settings.isMember(key) && settings[key].isNumeric()
        ? settings[key].asInt() : fallback;
}

Color parseColor(const Json::Value& settings, const char* key, Color fallback,
                 int opacity = 100) {
    if (!settings.isMember(key) || !settings[key].isString()) return fallback;
    const std::string hex = settings[key].asString();
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    char* end = nullptr;
    const unsigned long rgb = std::strtoul(hex.c_str() + 1, &end, 16);
    if (!end || *end != '\0') return fallback;
    return Color{static_cast<unsigned char>((rgb >> 16) & 0xff),
                 static_cast<unsigned char>((rgb >> 8) & 0xff),
                 static_cast<unsigned char>(rgb & 0xff),
                 static_cast<unsigned char>(std::clamp(opacity, 0, 100) * 255 / 100)};
}

fs::path expandHome(std::string path) {
    if (!path.empty() && path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) path.replace(0, 1, home);
    }
    return fs::path(path);
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return text;
}

void setNotice(App& app, std::string text, bool error = false) {
    app.notice = std::move(text);
    app.noticeError = error ? "1" : "";
    app.noticeUntil = GetTime() + 4.0;
}

bool writeJson(const fs::path& path, const Json::Value& data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temp = path.string() + ".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "    ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(data, &file);
    file << '\n';
    file.close();
    if (!file) return false;
    fs::rename(temp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temp, path, ec);
    }
    return !ec;
}

bool loadJson(const fs::path& path, Json::Value& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    Json::CharReaderBuilder builder;
    std::string errors;
    return Json::parseFromStream(builder, file, &out, &errors);
}

void saveGames(App& app) {
    Json::Value array(Json::arrayValue);
    for (const auto& game : app.games) array.append(game.data);
    app.root["games"] = std::move(array);
    if (!writeJson(app.gamesFile, app.root)) {
        setNotice(app, "Falha ao salvar games.json", true);
    }
}

void loadCovers(App& app) {
    for (auto& game : app.games) {
        const std::string cover = value(game.data, "cover");
        if (!game.hasCover && !cover.empty()) {
            const fs::path path = expandHome(cover);
            const std::string ext = lower(path.extension().string());
            if (fs::exists(path) && (ext == ".png" || ext == ".jpg" || ext == ".jpeg")) {
                game.cover = LoadTexture(path.c_str());
                game.hasCover = game.cover.id != 0;
                if (game.hasCover) SetTextureFilter(game.cover, TEXTURE_FILTER_BILINEAR);
            }
        }
        const std::string icon = value(game.data, "icon");
        if (!game.hasIcon && !icon.empty()) {
            const fs::path iconPath = expandHome(icon);
            std::string converted;
            fs::path loadPath = iconPath;
            if (lower(iconPath.extension().string()) == ".ico") {
                const fs::path temp = fs::temp_directory_path() /
                    ("ultimalauncher-icon-" + std::to_string(std::hash<std::string>{}(icon)) + ".png");
                const pid_t converter = fork();
                if (converter == 0) {
                    const std::string source = iconPath.string() + "[0]";
                    execlp("convert", "convert", source.c_str(), temp.c_str(), static_cast<char*>(nullptr));
                    _exit(127);
                }
                if (converter > 0) {
                    int status = 0;
                    waitpid(converter, &status, 0);
                    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                        converted = temp.string();
                        loadPath = temp;
                    }
                }
            }
            if (fs::exists(loadPath)) {
                game.icon = LoadTexture(loadPath.c_str());
                game.hasIcon = game.icon.id != 0;
                if (game.hasIcon) SetTextureFilter(game.icon, TEXTURE_FILTER_BILINEAR);
            }
            if (!converted.empty()) fs::remove(converted);
        }
    }
}

bool loadData(App& app) {
    Json::Value loaded;
    if (loadJson(app.gamesFile, loaded)) {
        if (loaded.isArray()) {
            app.root = Json::Value(Json::objectValue);
            app.root["games"] = loaded;
        } else if (loaded.isObject()) {
            app.root = loaded;
        }
    } else {
        app.root = Json::Value(Json::objectValue);
        app.root["games"] = Json::Value(Json::arrayValue);
    }

    Json::Value array = app.root["games"];
    if (array.isArray()) {
        for (const auto& item : array) {
            if (!item.isObject()) continue;
            Game game;
            game.data = item;
            if (!game.data.isMember("times_played")) game.data["times_played"] = 0;
            if (!game.data.isMember("total_playtime_seconds")) game.data["total_playtime_seconds"] = 0;
            if (!game.data.isMember("longest_session_seconds")) game.data["longest_session_seconds"] = 0;
            if (!game.data.isMember("last_played")) game.data["last_played"] = Json::Value();
            app.games.push_back(std::move(game));
        }
    }
    Json::Value sessions;
    app.sessions = loadJson(app.sessionsFile, sessions) && sessions.isArray()
        ? sessions : Json::Value(Json::arrayValue);
    return true;
}

std::string shellQuote(const std::string& text) {
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') quoted += "'\\''";
        else quoted += ch;
    }
    quoted += '\'';
    return quoted;
}

bool launchGame(App& app, size_t index) {
    if (index >= app.games.size()) return false;
    Json::Value& game = app.games[index].data;
    const std::string exeText = value(game, "exe");
    const fs::path exe = expandHome(exeText);
    const std::string runner = value(game, "runner", "Wine");
    const std::string prefix = value(game, "prefix");
    const std::string custom = value(game, "command");
    if (!fs::is_regular_file(exe)) {
        setNotice(app, "Executavel nao encontrado: " + exe.string(), true);
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        setNotice(app, "Nao foi possivel iniciar o jogo", true);
        return false;
    }
    if (pid == 0) {
        (void)setsid();
        (void)chdir(exe.parent_path().c_str());
        if (runner == "Wine") {
            const std::string resolvedPrefix = prefix.empty()
                ? (std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.wine")
                : expandHome(prefix).string();
            setenv("WINEPREFIX", resolvedPrefix.c_str(), 1);
            execlp("wine", "wine", exe.c_str(), static_cast<char*>(nullptr));
        } else if (runner == "Nativo") {
            execl(exe.c_str(), exe.c_str(), static_cast<char*>(nullptr));
        } else if (runner == "Comando personalizado") {
            std::string command = custom;
            const std::string token = "%EXE%";
            size_t pos = 0;
            while ((pos = command.find(token, pos)) != std::string::npos) {
                const std::string quoted = shellQuote(exe.string());
                command.replace(pos, token.size(), quoted);
                pos += quoted.size();
            }
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        }
        _exit(127);
    }

    const std::time_t now = std::time(nullptr);
    app.children.push_back({pid, index, now});
    Json::Value session(Json::objectValue);
    session["pgid"] = static_cast<Json::Int64>(pid);
    session["name"] = value(game, "name", "Jogo");
    session["runner"] = runner;
    if (runner == "Wine") {
        session["prefix"] = prefix.empty()
            ? (std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/.wine")
            : expandHome(prefix).string();
    } else {
        session["prefix"] = Json::Value();
    }
    session["started_at"] = static_cast<Json::Int64>(now);
    app.sessions.append(session);
    writeJson(app.sessionsFile, app.sessions);

    game["times_played"] = static_cast<Json::Int64>(number(game, "times_played") + 1);
    game["last_played"] = static_cast<Json::Int64>(now);
    saveGames(app);
    setNotice(app, "Iniciando " + value(game, "name", "jogo"));
    return true;
}

void updateProcesses(App& app) {
    for (size_t i = 0; i < app.children.size();) {
        int status = 0;
        const pid_t result = waitpid(app.children[i].pid, &status, WNOHANG);
        if (result == 0) {
            ++i;
            continue;
        }
        const ChildProcess child = app.children[i];
        const long long duration = std::max<long long>(0, std::time(nullptr) - child.startedAt);
        if (child.gameIndex < app.games.size()) {
            auto& data = app.games[child.gameIndex].data;
            data["total_playtime_seconds"] = static_cast<Json::Int64>(number(data, "total_playtime_seconds") + duration);
            data["longest_session_seconds"] = static_cast<Json::Int64>(std::max(number(data, "longest_session_seconds"), duration));
            saveGames(app);
        }
        if (result > 0) {
            const Json::Int64 endedPid = static_cast<Json::Int64>(child.pid);
            Json::Value remaining(Json::arrayValue);
            for (const auto& session : app.sessions) {
                if (!session.isMember("pgid") || session["pgid"].asInt64() != endedPid)
                    remaining.append(session);
            }
            app.sessions = std::move(remaining);
            writeJson(app.sessionsFile, app.sessions);
        }
        app.children.erase(app.children.begin() + static_cast<long>(i));
    }
}

std::vector<size_t> visibleGames(const App& app) {
    std::vector<size_t> result;
    const std::string query = lower(app.search);
    for (size_t i = 0; i < app.games.size(); ++i) {
        const std::string name = value(app.games[i].data, "name", "Jogo");
        if (query.empty() || lower(name).find(query) != std::string::npos)
            result.push_back(i);
    }
    return result;
}

void drawText(const App& app, const std::string& text, int x, int y,
              float size, Color color) {
    DrawTextEx(app.font, text.c_str(), {static_cast<float>(x), static_cast<float>(y)},
               size, 1.0f, color);
}

bool button(const App& app, Rectangle rect, const std::string& label,
            bool active = false, Color accent = kAccent, bool navigation = false) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, rect);
    const float radius = std::min(0.5f, static_cast<float>(app.buttonBorderRadius) / std::max(1.0f, std::min(rect.width, rect.height)) * 2.0f);
    const Color base = active ? accent :
        (hovered ? app.buttonHoverColor : (navigation ? Color{0, 0, 0, 0} : app.buttonColor));
    if (base.a > 0) DrawRectangleRounded(rect, radius, 8, base);
    if (!navigation && app.buttonBorderWidth > 0)
        DrawRectangleRoundedLinesEx(rect, radius, 8, static_cast<float>(app.buttonBorderWidth), app.buttonBorderColor);
    const Color textColor = app.buttonTextColor;
    const float fontSize = 15;
    const float width = MeasureTextEx(app.font, label.c_str(), fontSize, 1).x;
    drawText(app, label, static_cast<int>(rect.x + (rect.width - width) / 2),
             static_cast<int>(rect.y + (rect.height - fontSize) / 2), fontSize, textColor);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

std::string formatPlaytime(long long seconds) {
    if (seconds < 60) return std::to_string(seconds) + "s";
    const long long hours = seconds / 3600;
    const long long minutes = (seconds % 3600) / 60;
    if (hours > 0) return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
    return std::to_string(minutes) + " min";
}

void beginEdit(App& app, int index) {
    app.editing = true;
    app.editingIndex = index;
    app.focusedField = 0;
    app.fields.fill("");
    if (index >= 0 && static_cast<size_t>(index) < app.games.size()) {
        const auto& data = app.games[static_cast<size_t>(index)].data;
        app.fields = {value(data, "name"), value(data, "exe"), value(data, "runner", "Wine"),
                      value(data, "prefix"), value(data, "command"), value(data, "cover"), value(data, "icon")};
    } else {
        app.fields[2] = "Wine";
    }
}

std::string pickFile(const std::string& prompt, const std::string& filter) {
    const std::string command = "zenity --file-selection --title=" + shellQuote(prompt) +
        " --file-filter=" + shellQuote(filter) + " 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) return "";
    std::string result;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe)) result += buffer;
    const int status = pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return status == 0 ? result : "";
}

void saveEdit(App& app) {
    if (app.fields[0].empty() || app.fields[1].empty()) {
        setNotice(app, "Nome e executavel sao obrigatorios", true);
        return;
    }
    Json::Value data(Json::objectValue);
    if (app.editingIndex >= 0 && static_cast<size_t>(app.editingIndex) < app.games.size())
        data = app.games[static_cast<size_t>(app.editingIndex)].data;
    data["name"] = app.fields[0];
    data["exe"] = app.fields[1];
    data["runner"] = app.fields[2];
    data["prefix"] = app.fields[3];
    data["command"] = app.fields[4];
    data["cover"] = app.fields[5];
    data["icon"] = app.fields[6];
    if (!data.isMember("times_played")) data["times_played"] = 0;
    if (!data.isMember("total_playtime_seconds")) data["total_playtime_seconds"] = 0;
    if (!data.isMember("longest_session_seconds")) data["longest_session_seconds"] = 0;
    if (!data.isMember("show_image")) data["show_image"] = true;
    if (!data.isMember("show_icon_badge")) data["show_icon_badge"] = true;
    if (!data.isMember("cover_mode")) data["cover_mode"] = "crop";
    Game game;
    game.data = std::move(data);
    if (app.editingIndex >= 0 && static_cast<size_t>(app.editingIndex) < app.games.size()) {
        UnloadTexture(app.games[static_cast<size_t>(app.editingIndex)].cover);
        UnloadTexture(app.games[static_cast<size_t>(app.editingIndex)].icon);
        app.games[static_cast<size_t>(app.editingIndex)] = std::move(game);
    } else {
        app.games.push_back(std::move(game));
    }
    saveGames(app);
    loadCovers(app);
    app.editing = false;
    setNotice(app, "Jogo salvo");
}

void handleFieldInput(App& app) {
    const int key = GetCharPressed();
    if (key >= 32 && key <= 125 && app.fields[app.focusedField].size() < 500)
        app.fields[app.focusedField].push_back(static_cast<char>(key));
    if (IsKeyPressed(KEY_BACKSPACE) && !app.fields[app.focusedField].empty())
        app.fields[app.focusedField].pop_back();
    if (IsKeyPressed(KEY_TAB)) app.focusedField = (app.focusedField + 1) % static_cast<int>(app.fields.size());
    if (IsKeyPressed(KEY_ENTER)) {
        if (app.focusedField == 2) {
            static const std::array<std::string, 3> runners{"Wine", "Nativo", "Comando personalizado"};
            auto it = std::find(runners.begin(), runners.end(), app.fields[2]);
            app.fields[2] = runners[(static_cast<size_t>(std::distance(runners.begin(), it)) + 1) % runners.size()];
        } else {
            app.focusedField = (app.focusedField + 1) % static_cast<int>(app.fields.size());
        }
    }
    if (IsKeyPressed(KEY_ESCAPE)) app.editing = false;
}

void drawEditModal(App& app, int width, int height) {
    DrawRectangle(0, 0, width, height, Fade(BLACK, 0.72f));
    const float modalWidth = std::min(780.0f, width - 48.0f);
    const Rectangle modal{(width - modalWidth) / 2, 24, modalWidth, static_cast<float>(height - 48)};
    DrawRectangleRounded(modal, 0.025f, 8, kSidebar);
    drawText(app, app.editingIndex >= 0 ? "Editar jogo" : "Adicionar jogo",
             static_cast<int>(modal.x + 28), static_cast<int>(modal.y + 16), 24, kText);
    static const std::array<std::string, 7> labels{
        "Nome", "Executavel", "Runner (Enter alterna)", "Wine prefix", "Comando customizado", "Capa (.png/.jpg)", "Icone (.png/.jpg)"};
    const float fieldX = modal.x + 28;
    const float fieldW = modal.width - 56;
    float y = modal.y + 62;
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        drawText(app, labels[static_cast<size_t>(i)], static_cast<int>(fieldX), static_cast<int>(y), 12, kMuted);
        y += 15;
        const float inputW = (i == 1 || i == 5 || i == 6) ? fieldW - 104 : fieldW;
        const Rectangle input{fieldX, y, inputW, 28};
        DrawRectangleRounded(input, 0.15f, 5, i == app.focusedField ? kPanelHover : kPanel);
        std::string text = app.fields[static_cast<size_t>(i)];
        if (text.size() > 84) text = "..." + text.substr(text.size() - 81);
        drawText(app, text, static_cast<int>(input.x + 10), static_cast<int>(input.y + 7), 13, kText);
        if (i == app.focusedField && std::fmod(GetTime() * 2.0, 2.0) < 1.0) {
            const float cursorX = input.x + 10 + MeasureTextEx(app.font, text.c_str(), 13, 1).x;
            DrawLine(static_cast<int>(cursorX), static_cast<int>(input.y + 5),
                     static_cast<int>(cursorX), static_cast<int>(input.y + 23), kAccent);
        }
        if (i == 1 || i == 5 || i == 6) {
            const Rectangle browse{fieldX + inputW + 10, y, 94, 28};
            if (button(app, browse, "Procurar")) {
                const std::string filter = i == 1 ? "Executaveis | *.exe *.sh *" : "Imagens | *.png *.jpg *.jpeg";
                const std::string selected = pickFile("Selecionar arquivo", filter);
                if (!selected.empty()) app.fields[static_cast<size_t>(i)] = selected;
            }
        }
        y += 35;
    }
    const float footerY = modal.y + modal.height - 44;
    if (button(app, {modal.x + modal.width - 236, footerY, 98, 34}, "Cancelar")) app.editing = false;
    if (button(app, {modal.x + modal.width - 126, footerY, 98, 34}, "Salvar", true)) saveEdit(app);
    drawText(app, "Tab troca campo | Esc fecha", static_cast<int>(fieldX), static_cast<int>(footerY + 10), 12, kMuted);
}

void drawLibrary(App& app, int width, int height) {
    const int contentX = kSidebarWidth + 20;
    const int rightMargin = 20;

    if (app.page == 1) {
        drawText(app, "Estatisticas", contentX, 28, 26, kText);
        long long totalPlaytime = 0;
        long long totalLaunches = 0;
        int mostPlayed = -1;
        for (size_t i = 0; i < app.games.size(); ++i) {
            const auto& data = app.games[i].data;
            totalPlaytime += number(data, "total_playtime_seconds");
            totalLaunches += number(data, "times_played");
            if (mostPlayed < 0 || number(data, "times_played") > number(app.games[static_cast<size_t>(mostPlayed)].data, "times_played"))
                mostPlayed = static_cast<int>(i);
        }
        const int y = 116;
        const std::array<std::pair<std::string, std::string>, 3> stats{{
            {"Tempo total", formatPlaytime(totalPlaytime)},
            {"Sessoes iniciadas", std::to_string(totalLaunches)},
            {"Mais jogado", mostPlayed >= 0 ? value(app.games[static_cast<size_t>(mostPlayed)].data, "name") : "-"}}};
        int x = contentX;
        for (const auto& stat : stats) {
            DrawRectangleRounded({static_cast<float>(x), static_cast<float>(y), 238, 112}, 0.06f, 6, app.cardColor);
            drawText(app, stat.first, x + 18, y + 18, 14, kMuted);
            drawText(app, stat.second, x + 18, y + 51, 22, kText);
            x += 258;
        }
        int row = 0;
        for (const auto& game : app.games) {
            const int itemY = 264 + row * 50;
            if (itemY > height - 90) break;
            drawText(app, value(game.data, "name", "Jogo"), contentX, itemY, 16, kText);
            drawText(app, formatPlaytime(number(game.data, "total_playtime_seconds")) + "  |  " +
                     std::to_string(number(game.data, "times_played")) + " sessoes",
                     contentX + 330, itemY + 2, 14, kMuted);
            ++row;
        }
        return;
    }

    const Rectangle searchBox{static_cast<float>(contentX), 18,
                              static_cast<float>(width - contentX - rightMargin), 30};
    DrawRectangleRounded(searchBox, 0.08f, 6, app.searchFocused ? WHITE : Color{250, 250, 250, 255});
    DrawRectangleRoundedLinesEx(searchBox, 0.08f, 6, 1, {190, 190, 195, 255});
    drawText(app, app.search.empty() ? "Procurar jogo..." : app.search,
             static_cast<int>(searchBox.x + 12), 26, 14, {76, 76, 76, 255});
    if (CheckCollisionPointRec(GetMousePosition(), searchBox) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        app.searchFocused = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(GetMousePosition(), searchBox))
        app.searchFocused = false;
    if (app.searchFocused) {
        const int character = GetCharPressed();
        if (character >= 32 && character <= 125) app.search.push_back(static_cast<char>(character));
        if (IsKeyPressed(KEY_BACKSPACE) && !app.search.empty()) app.search.pop_back();
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER)) app.searchFocused = false;
    }

    const std::vector<size_t> visible = visibleGames(app);
    const int startY = 76;
    const int availableWidth = width - contentX - rightMargin;
    const int columns = std::max(1, width / 280);
    const int cardWidth = std::clamp((availableWidth - kCardGap * (columns - 1)) / columns, 185, kCardWidth);
    const int rowsVisible = std::max(1, (height - startY - 24) / (kCardHeight + kCardGap));
    const int totalRows = static_cast<int>((visible.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));
    app.scrollRow = std::clamp(app.scrollRow, 0, std::max(0, totalRows - rowsVisible));
    const int gridWidth = columns * cardWidth + (columns - 1) * kCardGap;
    const int gridX = contentX + std::max(0, (availableWidth - gridWidth) / 2);
    const Rectangle clip{static_cast<float>(contentX), static_cast<float>(startY),
                         static_cast<float>(availableWidth), static_cast<float>(height - startY - 24)};
    BeginScissorMode(static_cast<int>(clip.x), static_cast<int>(clip.y), static_cast<int>(clip.width), static_cast<int>(clip.height));
    for (size_t position = 0; position < visible.size(); ++position) {
        const int row = static_cast<int>(position) / columns;
        const int column = static_cast<int>(position) % columns;
        const int x = gridX + column * (cardWidth + kCardGap);
        const int y = startY + (row - app.scrollRow) * (kCardHeight + kCardGap);
        const size_t gameIndex = visible[position];
        const Rectangle card{static_cast<float>(x), static_cast<float>(y), static_cast<float>(cardWidth), kCardHeight};
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), card);
        const auto& game = app.games[gameIndex];
        const int border = std::clamp(app.borderWidth, 0, 20);
        const float radius = std::clamp(static_cast<float>(app.borderRadius) / card.height * 2.0f, 0.02f, 0.35f);
        DrawRectangleRounded(card, radius, 12, app.borderColor);
        const Rectangle inside{card.x + border, card.y + border, card.width - border * 2, card.height - border * 2};
        DrawRectangleRounded(inside, radius, 12, app.cardColor);
        const bool showImage = !game.data.isMember("show_image") || game.data["show_image"].asBool();
        const bool fullCover = showImage && game.hasCover;
        float nameY = inside.y + 148;
        if (fullCover) {
            const std::string mode = value(game.data, "cover_mode", "crop");
            Rectangle source{0, 0, static_cast<float>(game.cover.width), static_cast<float>(game.cover.height)};
            if (mode != "stretch") {
                const float scale = std::max(inside.width / game.cover.width, inside.height / game.cover.height);
                source.width = inside.width / scale;
                source.height = inside.height / scale;
                source.x = (game.cover.width - source.width) / 2;
                source.y = (game.cover.height - source.height) / 2;
            }
            DrawTexturePro(game.cover, source, inside, {0, 0}, 0, WHITE);
            const int gradientTop = static_cast<int>(inside.y);
            const int gradientMid = static_cast<int>(inside.y + inside.height * 0.45f);
            const int gradientBottom = static_cast<int>(inside.y + inside.height);
            DrawRectangleGradientV(static_cast<int>(inside.x), gradientTop,
                                   static_cast<int>(inside.width), gradientMid - gradientTop,
                                   Fade(BLACK, 0), Fade(BLACK, 60.0f / 255.0f));
            DrawRectangleGradientV(static_cast<int>(inside.x), gradientMid,
                                   static_cast<int>(inside.width), gradientBottom - gradientMid,
                                   Fade(BLACK, 60.0f / 255.0f), Fade(BLACK, 205.0f / 255.0f));
            nameY = inside.y + inside.height - 151;
        } else if (showImage && game.hasIcon) {
            const float scale = std::min((inside.width - 24) / game.icon.width, 130.0f / game.icon.height);
            const float iconWidth = game.icon.width * scale;
            const float iconHeight = game.icon.height * scale;
            DrawTexturePro(game.icon, {0, 0, static_cast<float>(game.icon.width), static_cast<float>(game.icon.height)},
                           {inside.x + (inside.width - iconWidth) / 2, inside.y + 10 + (130 - iconHeight) / 2,
                            iconWidth, iconHeight}, {0, 0}, 0, WHITE);
        } else if (showImage) {
            drawText(app, "UL", static_cast<int>(inside.x + inside.width / 2 - 15), static_cast<int>(inside.y + 52), 30, app.accent);
        } else {
            nameY = inside.y + 22;
        }
        const bool hasCoverBadge = fullCover && game.hasIcon &&
            (!game.data.isMember("show_icon_badge") || game.data["show_icon_badge"].asBool());
        if (hasCoverBadge) {
            DrawCircle(static_cast<int>(inside.x + inside.width - 37), static_cast<int>(inside.y + 37), 25, Fade(BLACK, 0.55f));
            DrawTexturePro(game.icon, {0, 0, static_cast<float>(game.icon.width), static_cast<float>(game.icon.height)},
                           {inside.x + inside.width - 57, inside.y + 17, 40, 40}, {0, 0}, 0, WHITE);
        }
        const std::string gameName = value(game.data, "name", "Jogo");
        std::string truncated = gameName;
        while (MeasureTextEx(app.font, truncated.c_str(), 15, 1).x > inside.width - 24 && truncated.size() > 4) {
            truncated.pop_back();
            if (truncated.size() > 3) truncated.replace(truncated.size() - 3, 3, "...");
        }
        const float centerX = inside.x + inside.width / 2;
        const float nameWidth = MeasureTextEx(app.font, truncated.c_str(), 15, 1).x;
        drawText(app, truncated, static_cast<int>(centerX - nameWidth / 2), static_cast<int>(nameY), 15, BLACK);
        const std::string runner = value(game.data, "runner", "Wine");
        const float runnerWidth = MeasureTextEx(app.font, runner.c_str(), 14, 1).x;
        drawText(app, runner, static_cast<int>(centerX - runnerWidth / 2), static_cast<int>(nameY + 27), 14, {204, 204, 204, 255});
        const std::string playtime = formatPlaytime(number(game.data, "total_playtime_seconds"));
        const float timeWidth = MeasureTextEx(app.font, playtime.c_str(), 12, 1).x;
        drawText(app, playtime, static_cast<int>(centerX - timeWidth / 2), static_cast<int>(nameY + 50), 12, {204, 204, 204, 255});
        if (hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) app.selected = static_cast<int>(gameIndex);
        const float playY = fullCover ? inside.y + inside.height - 78 : inside.y + 230;
        if (button(app, {inside.x + 8, playY, inside.width - 16, 34}, "Jogar")) launchGame(app, gameIndex);
        const float actionY = fullCover ? inside.y + inside.height - 38 : inside.y + 270;
        if (button(app, {inside.x + 8, actionY, inside.width - 54, 28}, "Editar")) {
            app.selected = static_cast<int>(gameIndex);
            beginEdit(app, static_cast<int>(gameIndex));
        }
        if (button(app, {inside.x + inside.width - 38, actionY, 30, 28}, "X", false, app.accent)) {
            UnloadTexture(app.games[gameIndex].cover);
            UnloadTexture(app.games[gameIndex].icon);
            app.games.erase(app.games.begin() + static_cast<long>(gameIndex));
            app.selected = -1;
            saveGames(app);
            break;
        }
    }
    EndScissorMode();
    if (totalRows > rowsVisible) {
        const float trackHeight = static_cast<float>(height - startY - 48);
        const float thumbHeight = std::max(36.0f, trackHeight * rowsVisible / totalRows);
        const float maxOffset = trackHeight - thumbHeight;
        const float thumbY = startY + (totalRows == rowsVisible ? 0 : maxOffset * app.scrollRow / (totalRows - rowsVisible));
        DrawRectangle(width - 18, startY, 4, static_cast<int>(trackHeight), Fade(app.borderColor, 0.25f));
        DrawRectangleRounded({static_cast<float>(width - 20), thumbY, 8, thumbHeight}, 0.5f, 4, app.accent);
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0 && GetMouseX() >= contentX) app.scrollRow = std::max(0, app.scrollRow - (wheel > 0 ? 1 : -1));
}

void drawSidebar(App& app, int height) {
    DrawRectangle(0, 0, kSidebarWidth, height, app.cardColor);
    DrawRectangle(kSidebarWidth - app.borderWidth, 0, app.borderWidth, height, app.borderColor);
    if (app.hasAppIcon) {
        DrawTexturePro(app.appIcon, {0, 0, static_cast<float>(app.appIcon.width), static_cast<float>(app.appIcon.height)},
                       {14, 18, 28, 28}, {0, 0}, 0, WHITE);
    }
    drawText(app, "UltimaLauncher", 48, 23, 17, kText);
    if (button(app, {12, 62, kSidebarWidth - 24.0f, 38}, "Biblioteca", app.page == 0, app.accent, true)) app.page = 0;
    if (button(app, {12, 104, kSidebarWidth - 24.0f, 38}, "Estatisticas", app.page == 1, app.accent, true)) app.page = 1;
    DrawLine(14, 158, kSidebarWidth - 14, 158, app.buttonBorderColor);
    if (button(app, {12, 174, kSidebarWidth - 24.0f, 38}, "+ Adicionar", false, app.accent)) beginEdit(app, -1);
    button(app, {12, 218, kSidebarWidth - 24.0f, 38}, "Processos");
    button(app, {12, 262, kSidebarWidth - 24.0f, 38}, "Criar atalho");
    button(app, {12, 306, kSidebarWidth - 24.0f, 38}, "Configuracoes");
    button(app, {12, 350, kSidebarWidth - 24.0f, 38}, "Ajuda");
}

bool initialize(App& app) {
    const char* home = std::getenv("HOME");
    if (!home) return false;
    app.appDir = fs::path(home) / "UltimaLauncher";
    app.gamesFile = app.appDir / "dados/games.json";
    app.sessionsFile = app.appDir / "dados/sessions.json";
    loadData(app);
    const Json::Value settings = app.root["settings"];
    app.accent = parseColor(settings, "accent", app.accent);
    app.cardOpacity = settingInt(settings, "card_opacity", 30);
    app.cardColor = parseColor(settings, "card_color", app.cardColor, app.cardOpacity);
    app.borderColor = parseColor(settings, "border_color", app.borderColor);
    app.borderWidth = settingInt(settings, "border_width", 10);
    app.borderRadius = settingInt(settings, "border_radius", 20);
    const int buttonOpacity = settingInt(settings, "button_opacity", 30);
    app.buttonColor = parseColor(settings, "button_color", app.buttonColor, buttonOpacity);
    app.buttonTextColor = parseColor(settings, "button_text_color", app.buttonTextColor);
    app.buttonBorderColor = parseColor(settings, "button_border_color", app.buttonBorderColor);
    app.buttonBorderWidth = settingInt(settings, "button_border_width", 1);
    app.buttonBorderRadius = settingInt(settings, "button_border_radius", 8);
    const std::string hover = value(settings, "button_hover_color");
    app.buttonHoverColor = hover.empty()
        ? app.accent
        : parseColor(settings, "button_hover_color", app.accent);
    if (hover.empty()) app.buttonHoverColor.a = static_cast<unsigned char>(buttonOpacity * 255 / 100);
    app.backgroundOpacity = settingInt(settings, "background_opacity", 35);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    App app;
    if (!initialize(app)) {
        std::cerr << "HOME nao definido\n";
        return 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--check-data") {
        std::cout << "games=" << app.games.size() << " sessions=" << app.sessions.size() << '\n';
        return 0;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(1100, 700, "UltimaLauncher");
    SetWindowMinSize(800, 600);
    SetTargetFPS(60);
    const std::array<const char*, 3> fonts{
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"};
    for (const char* path : fonts) {
        if (FileExists(path)) {
            app.font = LoadFontEx(path, 32, nullptr, 0);
            app.customFont = app.font.texture.id != 0;
            if (app.customFont) break;
        }
    }
    if (!app.customFont) app.font = GetFontDefault();
    const Json::Value settings = app.root["settings"];
    fs::path iconPath = expandHome(value(settings, "app_icon"));
    if (iconPath.empty() || !fs::exists(iconPath)) iconPath = app.appDir / "icones/ultimalauncher-icon.png";
    if (FileExists(iconPath.c_str())) {
        app.appIcon = LoadTexture(iconPath.c_str());
        app.hasAppIcon = app.appIcon.id != 0;
    }
    const fs::path backgroundPath = expandHome(value(settings, "background"));
    if (!backgroundPath.empty() && FileExists(backgroundPath.c_str())) {
        app.background = LoadTexture(backgroundPath.c_str());
        app.hasBackground = app.background.id != 0;
    }
    loadCovers(app);

    while (!WindowShouldClose()) {
        updateProcesses(app);
        const int width = GetScreenWidth();
        const int height = GetScreenHeight();
        if (!app.editing && !app.searchFocused) {
            if (IsKeyPressed(KEY_UP)) app.selected = std::max(0, app.selected - 1);
            if (IsKeyPressed(KEY_DOWN)) app.selected = std::min(static_cast<int>(app.games.size()) - 1, app.selected + 1);
            if (IsKeyPressed(KEY_ENTER) && app.selected >= 0) launchGame(app, static_cast<size_t>(app.selected));
            if (IsKeyPressed(KEY_E) && app.selected >= 0) beginEdit(app, app.selected);
            if (IsKeyPressed(KEY_N)) beginEdit(app, -1);
            if (IsKeyPressed(KEY_ESCAPE)) app.selected = -1;
        }
        if (app.editing) handleFieldInput(app);

        BeginDrawing();
        ClearBackground({16, 16, 20, 255});
        if (app.hasBackground) {
            const float scale = std::max(static_cast<float>(width) / app.background.width,
                                         static_cast<float>(height) / app.background.height);
            const float sourceWidth = width / scale;
            const float sourceHeight = height / scale;
            const Rectangle source{(app.background.width - sourceWidth) / 2,
                                   (app.background.height - sourceHeight) / 2,
                                   sourceWidth, sourceHeight};
            DrawTexturePro(app.background, source, {0, 0, static_cast<float>(width), static_cast<float>(height)},
                           {0, 0}, 0, WHITE);
            DrawRectangle(0, 0, width, height, Fade(BLACK, 1.0f - app.backgroundOpacity / 100.0f));
        }
        drawSidebar(app, height);
        drawLibrary(app, width, height);
        if (!app.notice.empty() && GetTime() < app.noticeUntil) {
            const float noticeWidth = std::min(650.0f, MeasureTextEx(app.font, app.notice.c_str(), 14, 1).x + 36);
            const Rectangle toast{(width - noticeWidth) / 2, static_cast<float>(height - 64), noticeWidth, 40};
            DrawRectangleRounded(toast, 0.18f, 6, app.noticeError.empty() ? kPanelHover : kCoral);
            drawText(app, app.notice, static_cast<int>(toast.x + 16), static_cast<int>(toast.y + 12), 14, kText);
        }
        if (app.editing) drawEditModal(app, width, height);
        EndDrawing();
    }

    const std::time_t closedAt = std::time(nullptr);
    for (const auto& child : app.children) {
        if (child.gameIndex >= app.games.size()) continue;
        const long long duration = std::max<long long>(0, closedAt - child.startedAt);
        auto& data = app.games[child.gameIndex].data;
        data["total_playtime_seconds"] = static_cast<Json::Int64>(number(data, "total_playtime_seconds") + duration);
        data["longest_session_seconds"] = static_cast<Json::Int64>(std::max(number(data, "longest_session_seconds"), duration));
    }
    if (!app.children.empty()) saveGames(app);
    for (auto& game : app.games) {
        if (game.hasCover) UnloadTexture(game.cover);
        if (game.hasIcon) UnloadTexture(game.icon);
    }
    if (app.hasAppIcon) UnloadTexture(app.appIcon);
    if (app.hasBackground) UnloadTexture(app.background);
    if (app.customFont) UnloadFont(app.font);
    CloseWindow();
    return 0;
}