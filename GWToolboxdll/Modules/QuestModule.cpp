#include <GWCA/Utilities/Hook.h>

#include <GWCA/GameEntities/Quest.h>
#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/QuestMgr.h>
#include <GWCA/Managers/AgentMgr.h>

#include "QuestModule.h"

#include <Windows/Pathfinding/PathfindingWindow.h>
#include <Widgets/Minimap/CustomRenderer.h>
#include <Widgets/Minimap/Minimap.h>
#include <GWCA/Managers/GameThreadMgr.h>

namespace {
    Color draw_quest_path_color = 0xFFFFFFFF;
    bool draw_quest_path_on_terrain = true;
    bool draw_quest_path_on_minimap = true;
    bool redirect_quest_marker = false;
    GW::HookEntry ui_message_entry;

    void OnQuestPathRecalculated(const std::vector<GW::GamePos>& waypoints, void* args);

    void ClearCalculatedPath(GW::Constants::QuestID quest_id);

    void RedirectQuestMarker(const GW::GamePos& new_marker);

    struct CalculatedQuestPath {
        CalculatedQuestPath(GW::Constants::QuestID _quest_id)
            : quest_id(_quest_id) {};

        ~CalculatedQuestPath()
        {
            ClearMinimapLines();
        }

        std::vector<GW::GamePos> waypoints;
        std::vector<CustomRenderer::CustomLine*> minimap_lines;
        GW::GamePos previous_closest_waypoint;
        GW::GamePos original_quest_marker;
        GW::GamePos calculated_from;
        GW::GamePos calculated_to;
        clock_t calculated_at = 0;
        uint32_t current_waypoint = 0;
        GW::Constants::QuestID quest_id;
        bool calculating = false;

        void ClearMinimapLines()
        {
            for (const auto l : minimap_lines) {
                Minimap::Instance().custom_renderer.RemoveCustomLine(l);
            }
            minimap_lines.clear();
        }

        void DrawMinimapLines()
        {
            ClearMinimapLines();
            if (!draw_quest_path_on_terrain && !draw_quest_path_on_minimap)
                return;
            for (size_t i = current_waypoint > 0 ? current_waypoint - 1 : 0; i < waypoints.size() - 1; i++) {
                const auto l = Minimap::Instance().custom_renderer.AddCustomLine(waypoints[i], waypoints[i + 1], std::format("{} - {}", (uint32_t)quest_id, i).c_str(), true);
                l->draw_on_terrain = draw_quest_path_on_terrain;
                l->created_by_toolbox = true;
                l->color = draw_quest_path_color;
                minimap_lines.push_back(l);
            }
        }

        const GW::Quest* GetQuest()
        {
            return GW::QuestMgr::GetQuest(quest_id);
        }

        bool IsActive()
        {
            const auto a = GW::QuestMgr::GetActiveQuest();
            return a && a == GetQuest();
        }

        const GW::GamePos* CurrentWaypoint()
        {
            return &waypoints[current_waypoint];
        }

        const GW::GamePos* NextWaypoint()
        {
            return current_waypoint < waypoints.size() - 1 ? &waypoints[current_waypoint + 1] : nullptr;
        }

        void Recalculate(const GW::GamePos& from)
        {
            if (from == calculated_from && calculated_to == original_quest_marker) {
                calculating = true;
                OnQuestPathRecalculated(waypoints, (void*)quest_id); // No need to recalculate
                return;
            }
            calculated_from = from;
            calculated_to = original_quest_marker;
            if (original_quest_marker.x == INFINITY)
                return;
            calculating = PathfindingWindow::CalculatePath(calculated_from, calculated_to, OnQuestPathRecalculated, (void*)quest_id);
        }

        bool Update(const GW::GamePos& from)
        {
            const auto quest = GetQuest();
            if (!quest) {
                ClearCalculatedPath(quest_id);
                return true;
            }
            if (calculating) {
                return false;
            }
            if (!calculated_at) {
                Recalculate(from);
                return false;
            }
            constexpr float recalculate_when_moved_further_than = 100.f * 100.f;
            if (GetSquareDistance(from, calculated_from) > recalculate_when_moved_further_than) {
                Recalculate(from);
                return false;
            }
            uint32_t original_waypoint = current_waypoint;

            const auto waypoint_len = waypoints.size();
            if (!waypoint_len)
                return false;

            const auto from_end_waypoint = GetSquareDistance(from, waypoints.back());
            // Find next waypoint
            current_waypoint = waypoint_len - 1;
            for (size_t i = 1; i < waypoint_len; i++) {
                if (GetSquareDistance(from, waypoints[i]) < from_end_waypoint) {
                    current_waypoint = i;
                    break;
                }
            }
            if (original_waypoint != current_waypoint) {
                calculated_from = from;
                UpdateUI();
            }
            return false;
        }

        void UpdateUI()
        {
            if (waypoints.empty())
                return;
            DrawMinimapLines();
            const auto& current_waypoint_pos = waypoints[current_waypoint];
            const auto waypoint_distance = GetSquareDistance(current_waypoint_pos, previous_closest_waypoint);
            constexpr float update_when_waypoint_changed_more_than = 300.f * 300.f;
            if (IsActive() &&
                waypoint_distance > update_when_waypoint_changed_more_than) {
                previous_closest_waypoint = waypoints[current_waypoint];
                if (redirect_quest_marker)
                    RedirectQuestMarker(waypoints[current_waypoint]);
            }
        }
    };

    std::unordered_map<GW::Constants::QuestID, CalculatedQuestPath*> calculated_quest_paths;

    void ClearCalculatedPaths()
    {
        for (auto& it : calculated_quest_paths) {
            delete it.second;
        }
        calculated_quest_paths.clear();
    }

    void ClearCalculatedPath(GW::Constants::QuestID quest_id)
    {
        const auto found = calculated_quest_paths.find(quest_id);
        if (found == calculated_quest_paths.end())
            return;
        auto cqp = found->second;
        calculated_quest_paths.erase(found);
        if (!cqp->calculating) {
            // NB: If its still calculating, it will delete itself later in OnQuestPathRecalculated
            delete cqp;
        }
    }

    CalculatedQuestPath* GetCalculatedQuestPath(GW::Constants::QuestID quest_id, bool create_if_not_found = true)
    {
        const auto found = calculated_quest_paths.find(quest_id);
        if (found != calculated_quest_paths.end()) return found->second;
        if (!create_if_not_found)
            return nullptr;
        auto cqp = new CalculatedQuestPath(quest_id);
        calculated_quest_paths[quest_id] = cqp;
        return cqp;
    }

    constexpr auto ui_messages = {
        GW::UI::UIMessage::kQuestDetailsChanged,
        GW::UI::UIMessage::kQuestAdded,
        GW::UI::UIMessage::kClientActiveQuestChanged
    };

    bool is_spoofing_quest_update = false;

    // Settings
    GW::GamePos* GetPlayerPos()
    {
        const auto p = GW::Agents::GetControlledCharacter();
        return p ? &p->pos : nullptr;
    }

    // Replace marker for current quest, broadcast update to the game
    void RedirectQuestMarker(const GW::GamePos& new_marker)
    {
        const auto quest = GW::QuestMgr::GetActiveQuest();
        if (!quest)
            return;
        Log::Flash("Overriding quest marker from %.2f, %.2f to %.2f, %.2f", quest->marker.x, quest->marker.y, new_marker.x, new_marker.y);
        if (quest->marker == new_marker)
            return; // No change

        quest->marker = new_marker;
        struct QuestUIMsg {
            GW::Constants::QuestID quest_id{};
            GW::GamePos marker{};
            uint32_t h0024{};
            GW::Constants::MapID map_to{};
            uint32_t log_state{};
        } msg;
        msg.quest_id = quest->quest_id;
        msg.marker = quest->marker;
        msg.h0024 = quest->h0024;
        msg.map_to = quest->map_to;
        msg.log_state = quest->log_state;

        is_spoofing_quest_update = true;
        SendUIMessage(GW::UI::UIMessage::kClientActiveQuestChanged, &msg);
        is_spoofing_quest_update = false;
    }

    // Cast helper
    float GetSquareDistance(const GW::GamePos& a, const GW::GamePos& b)
    {
        return GetSquareDistance(static_cast<GW::Vec2f>(a), static_cast<GW::Vec2f>(b));
    }

    // Called by PathfindingWindow when a path has been calculated. Should be on the main loop.
    void OnQuestPathRecalculated(const std::vector<GW::GamePos>& waypoints, void* args)
    {
        const auto cqp = GetCalculatedQuestPath(*(GW::Constants::QuestID*)&args, false);
        if (!cqp || !cqp->calculating) return;
        // TODO: @3vcloud idc to look at it atm but this crashes me when changing zones if a path had already been calculated
        ASSERT(cqp->calculating);

        if (GetCalculatedQuestPath(cqp->quest_id) != cqp) {
            // Calculated path is stale, delete and drop out
            delete cqp;
            return;
        }

        cqp->current_waypoint = 0;
        cqp->waypoints = std::move(waypoints);

        if (!cqp->waypoints.empty() && GetSquareDistance(cqp->waypoints.back(), cqp->calculated_from) < GetSquareDistance(cqp->waypoints.front(), cqp->calculated_from)) {
            // Waypoint array is in descending distance, flip it
            std::ranges::reverse(cqp->waypoints);
        }

        const auto waypoint_len = waypoints.size();
        if (!waypoint_len) {
            cqp->calculating = false;
            cqp->calculated_at = TIMER_INIT();
            return;
        }

        const auto from_end_waypoint = GetSquareDistance(cqp->calculated_from, waypoints.back());
        // Find next waypoint
        cqp->current_waypoint = waypoint_len - 1;
        for (size_t i = 1; i < waypoint_len; i++) {
            if (GetSquareDistance(cqp->calculated_from, waypoints[i]) < from_end_waypoint) {
                cqp->current_waypoint = i;
                break;
            }
        }

        cqp->calculated_at = TIMER_INIT();
        cqp->calculating = false;
        cqp->UpdateUI();
    }

    // Callback invoked by quest related ui messages. All messages sent should have the quest id as first wparam variable
    void OnGWQuestMarkerUpdated(GW::HookStatus*, GW::UI::UIMessage, void* packet, void*)
    {
        if (is_spoofing_quest_update)
            return; // Recursive
        GW::Constants::QuestID affected_quest_id = *(GW::Constants::QuestID*)packet;

        const auto quest = GW::QuestMgr::GetQuest(affected_quest_id);
        auto cqp = GetCalculatedQuestPath(affected_quest_id);

        cqp->original_quest_marker = quest->marker;
        const auto pos = GetPlayerPos();
        if (!pos)
            return;
        cqp->Recalculate(*pos);
    }
} // namespace

void QuestModule::DrawSettingsInternal()
{
    ImGui::Text("Draw path to quest marker on:");
    ImGui::Checkbox("Terrain##drawquestpath", &draw_quest_path_on_terrain);
    ImGui::Checkbox("Minimap##drawquestpath", &draw_quest_path_on_minimap);
    Colors::DrawSettingHueWheel("Color##drawquestpath", &draw_quest_path_color);
    ImGui::Checkbox("Redirect quest marker to nearest stop##redirectquestmarker", &redirect_quest_marker);
}

void QuestModule::LoadSettings(ToolboxIni* ini)
{
    ToolboxModule::LoadSettings(ini);
    LOAD_BOOL(draw_quest_path_on_minimap);
    LOAD_BOOL(draw_quest_path_on_terrain);
    LOAD_BOOL(redirect_quest_marker);
    LOAD_COLOR(draw_quest_path_color);
}

void QuestModule::SaveSettings(ToolboxIni* ini)
{
    ToolboxModule::SaveSettings(ini);
    SAVE_BOOL(draw_quest_path_on_minimap);
    SAVE_BOOL(draw_quest_path_on_terrain);
    SAVE_BOOL(redirect_quest_marker);
    SAVE_COLOR(draw_quest_path_color);
}

void QuestModule::Initialize()
{
    ToolboxModule::Initialize();

    for (auto ui_message : ui_messages) {
        // Post callbacks, non blocking
        (ui_message);
        GW::UI::RegisterUIMessageCallback(&ui_message_entry, ui_message, OnGWQuestMarkerUpdated, 0x4000);
    }
    GW::GameThread::Enqueue([] {
        PathfindingWindow::ReadyForPathing();
    });
}

void QuestModule::SignalTerminate()
{
    ToolboxModule::SignalTerminate();
    GW::UI::RemoveUIMessageCallback(&ui_message_entry);
    ClearCalculatedPaths();
}

void QuestModule::Update(float)
{
    const auto pos = GetPlayerPos();
    if (!pos)
        return;
    const uint8_t alpha = (draw_quest_path_color >> IM_COL32_A_SHIFT) & 0xFF;
    if (!draw_quest_path_on_minimap && !draw_quest_path_on_terrain || alpha == 0) {
        //return;
    }

    for (const auto& it : calculated_quest_paths) {
        if (it.second->Update(*pos))
            break; // Deleted, skip frame
    }
}

void QuestModule::Terminate()
{
    ToolboxModule::Terminate();
}
