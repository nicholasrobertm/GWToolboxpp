#include "stdafx.h"

#include <GWCA/Constants/Constants.h>
#include <GWCA/GameContainers/GamePos.h>

#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Quest.h>

#include <GWCA/Managers/AgentMgr.h>

#include <ImGuiAddons.h>

#include <Color.h>

#include <GWCA/Managers/QuestMgr.h>
#include <Widgets/Minimap/Minimap.h>
#include <Widgets/Minimap/SymbolsRenderer.h>
#include <Modules/QuestModule.h>

// Note: autogenerated by CMake!
#include "Shaders/constant_colour_ps.h"

namespace {
    IDirect3DPixelShader9* pshader = nullptr;
    bool need_configure_pipeline = true;
}

void SymbolsRenderer::LoadSettings(const ToolboxIni* ini, const char* section)
{
    color_quest = Colors::Load(ini, section, "color_quest", 0xFF22EF22);
    color_other_quests = Colors::Load(ini, section, "color_other_quests", 0x00006400);
    color_north = Colors::Load(ini, section, "color_north", 0xFFFF8000);
    color_modifier = Colors::Load(ini, section, "color_symbols_modifier", 0x001E1E1E);

    Invalidate();
}

void SymbolsRenderer::SaveSettings(ToolboxIni* ini, const char* section) const
{
    Colors::Save(ini, section, "color_quest", color_quest);
    Colors::Save(ini, section, "color_other_quests", color_other_quests);
    Colors::Save(ini, section, "color_north", color_north);
    Colors::Save(ini, section, "color_symbols_modifier", color_modifier);
}

void SymbolsRenderer::DrawSettings()
{
    ImGui::SmallConfirmButton("Restore Defaults", "Are you sure?", [&](bool result, void*) {
        if (result) {
            color_quest = 0xFF22EF22;
            color_other_quests = 0x00006400;
            color_north = 0xFFFF8000;
            color_modifier = 0x001E1E1E;
            Invalidate();
        }
        });
    if (Colors::DrawSettingHueWheel("Active quest marker", &color_quest)) {
        Invalidate();
    }
    if (Colors::DrawSettingHueWheel("Other quest markers", &color_other_quests)) {
        Invalidate();
    }
    ImGui::ShowHelp("Quest markers that are not the active quest will be shown in this colour, if the option 'Draw all quests' is enabled.\n"
                    "If this colour has an alpha of 0, the inactive quest markers will all have random colours.");
    if (Colors::DrawSettingHueWheel("North marker", &color_north)) {
        Invalidate();
    }
    if (Colors::DrawSettingHueWheel("Symbol modifier", &color_modifier)) {
        Invalidate();
    }
    ImGui::ShowHelp("Each symbol has this value removed on the border and added at the center\n"
                    "Zero makes them have solid color, while a high number makes them appear more shaded.");
}

void SymbolsRenderer::Initialize(IDirect3DDevice9* device)
{
    if (initialized) {
        return;
    }
    initialized = true;
    type = D3DPT_TRIANGLELIST;

    D3DVertex* vertices = nullptr;
    const DWORD vertex_count = (star_ntriangles + arrow_ntriangles + north_ntriangles) * 3;
    DWORD offset = 0;

    device->CreateVertexBuffer(sizeof(D3DVertex) * vertex_count, D3DUSAGE_WRITEONLY,
                               D3DFVF_CUSTOMVERTEX, D3DPOOL_MANAGED, &buffer, nullptr);
    buffer->Lock(0, sizeof(D3DVertex) * vertex_count,
                 reinterpret_cast<void**>(&vertices), D3DLOCK_DISCARD);

    const auto add_vertex = [&vertices, &offset](const float x, const float y, const Color color) -> void {
        vertices[0].x = x;
        vertices[0].y = y;
        vertices[0].z = 0.0f;
        vertices[0].color = color;
        ++vertices;
        ++offset;
    };

    // === Star ===
    star_offset = offset;
    for (auto i = 0u; i < star_ntriangles; i++) {
        constexpr float star_size_big = 300.0f;
        constexpr float star_size_small = 150.0f;
        const float angle1 = 2 * (i + 0) * DirectX::XM_PI / star_ntriangles;
        const float angle2 = 2 * (i + 1) * DirectX::XM_PI / star_ntriangles;
        const float size1 = (i + 0) % 2 == 0 ? star_size_small : star_size_big;
        const float size2 = (i + 1) % 2 == 0 ? star_size_small : star_size_big;
        const Color c1 = (i + 0) % 2 == 0 ? color_quest : Colors::Sub(color_quest, color_modifier);
        const Color c2 = (i + 1) % 2 == 0 ? color_quest : Colors::Sub(color_quest, color_modifier);
        add_vertex(std::cos(angle1) * size1, std::sin(angle1) * size1, c1);
        add_vertex(std::cos(angle2) * size2, std::sin(angle2) * size2, c2);
        add_vertex(0.0f, 0.0f, Colors::Add(color_quest, color_modifier));
    }

    // === Arrow (quest) ===
    arrow_offset = offset;
    add_vertex(0.0f, -125.0f, Colors::Add(color_quest, color_modifier));
    add_vertex(250.0f, -250.0f, color_quest);
    add_vertex(0.0f, 250.0f, color_quest);
    add_vertex(0.0f, 250.0f, color_quest);
    add_vertex(-250.0f, -250.0f, color_quest);
    add_vertex(0.0f, -125.0f, Colors::Add(color_quest, color_modifier));

    // === Arrow (north) ===
    north_offset = offset;
    add_vertex(0.0f, -375.0f, Colors::Add(color_north, color_modifier));
    add_vertex(250.0f, -500.0f, color_north);
    add_vertex(0.0f, 0.0f, color_north);
    add_vertex(0.0f, 0.0f, color_north);
    add_vertex(-250.0f, -500.0f, color_north);
    add_vertex(0.0f, -375.0f, Colors::Add(color_north, color_modifier));

    buffer->Unlock();
}

void SymbolsRenderer::Invalidate()
{
    VBuffer::Invalidate();
    this->initialized = false;
}

void SymbolsRenderer::Render(IDirect3DDevice9* device)
{
    Initialize(device);

    if (need_configure_pipeline) {
        if (!ConfigureProgrammablePipeline(device)) {
            return;
        }
    }

    const GW::Agent* me = GW::Agents::GetObservingAgent();
    if (me == nullptr) {
        return;
    }

    device->SetFVF(D3DFVF_CUSTOMVERTEX);
    device->SetStreamSource(0, buffer, 0, sizeof(D3DVertex));

    constexpr float pi = std::numbers::pi_v<float>;
    static float tau = 0.0f;
    const float fps = ImGui::GetIO().Framerate;
    // tau of += 0.05f is good for 60 fps, adapt that for any
    // note: framerate is a moving average of the last 120 frames, so it won't adapt quickly.
    // when the framerate changes a lot, the quest marker may speed up or down for a bit.
    tau += 0.05f * 60.0f / std::max(fps, 1.0f);
    if (tau > 10 * pi) {
        tau -= 10 * pi;
    }
    DirectX::XMMATRIX translate{};
    DirectX::XMMATRIX world{};

    const GW::Vec2f mypos = me->pos;
    std::vector<GW::Vec2f> markers_drawn;
    const auto draw_quest_marker = [&](const GW::Quest& quest)
    {
        const auto active_quest = GW::QuestMgr::GetActiveQuest();
        const bool is_current_quest = active_quest != nullptr && quest.quest_id == active_quest->quest_id;

        if (!Minimap::ShouldDrawAllQuests() && !is_current_quest) {
            return;
        }

        const GW::Vec2f qpos = { quest.marker.x, quest.marker.y };
        if (std::ranges::contains(markers_drawn, qpos))
            return; // Don't draw more than 1 marker for a position
        markers_drawn.push_back(qpos);

        const auto quest_im_color = QuestModule::GetQuestColor(quest.quest_id);
        const auto quest_color = ImGui::ColorConvertU32ToFloat4(quest_im_color);
        device->SetPixelShaderConstantF(0, &quest_color.x, 1);

        const float compass_scale = Minimap::Instance().Scale();
        const float marker_scale = 1.0f / compass_scale;
        auto rotate = DirectX::XMMatrixRotationZ(-tau / 5);
        DirectX::XMMATRIX scale = DirectX::XMMatrixScaling(marker_scale + std::sin(tau) * 0.3f * marker_scale,
                                                           marker_scale + std::sin(tau) * 0.3f * marker_scale, 1.0f);
        translate = DirectX::XMMatrixTranslation(qpos.x, qpos.y, 0);
        world = rotate * scale * translate;
        device->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world));
        device->DrawPrimitive(type, star_offset, star_ntriangles);

        GW::Vec2f v = qpos - mypos;
        const float max_quest_range = (GW::Constants::Range::Compass - 250.0f) / compass_scale;
        const float max_quest_range_sqr = max_quest_range * max_quest_range;
        if (GetSquaredNorm(v) > max_quest_range_sqr) {
            v = Normalize(v) * max_quest_range;

            const float angle = std::atan2(v.y, v.x);
            rotate = DirectX::XMMatrixRotationZ(angle - DirectX::XM_PIDIV2);
            scale = DirectX::XMMatrixScaling(marker_scale + std::sin(tau) * 0.3f * marker_scale, marker_scale + std::sin(tau) * 0.3f * marker_scale, 1.0f);
            translate = DirectX::XMMatrixTranslation(me->pos.x + v.x, me->pos.y + v.y, 0);
            world = rotate * scale * translate;
            device->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world));
            device->DrawPrimitive(type, arrow_offset, arrow_ntriangles);
        }
    };

    if (const auto quest_log = GW::QuestMgr::GetQuestLog()) {
        if (pshader == nullptr || device->SetPixelShader(pshader) != D3D_OK) {
            Log::Error("SymbolsRenderer: unable to SetPixelShader, aborting render.");
            return;
        }

        // draw active quest first
        const auto active_quest_id = GW::QuestMgr::GetActiveQuestId();
        if (auto* quest = GW::QuestMgr::GetQuest(active_quest_id)) {
            draw_quest_marker(*quest);
        }
        for (const auto& quest : *quest_log | std::views::filter([active_quest_id](const GW::Quest& q) {
            return q.quest_id != active_quest_id;
        })) {
            draw_quest_marker(quest);
        }

        device->SetPixelShader(nullptr);
    }

    translate = DirectX::XMMatrixTranslation(me->pos.x, me->pos.y + 5000.0f, 0);
    world = translate;
    device->SetTransform(D3DTS_WORLD, reinterpret_cast<const D3DMATRIX*>(&world));
    device->DrawPrimitive(type, north_offset, north_ntriangles);
}

bool SymbolsRenderer::ConfigureProgrammablePipeline(IDirect3DDevice9* device)
{
    if (pshader != nullptr) {
        return true;
    }
    if (device->CreatePixelShader(reinterpret_cast<const DWORD*>(&constant_colour_ps), &pshader) != D3D_OK) {
        Log::Error("SymbolsRenderer: unable to CreatePixelShader");
        return false;
    }
    need_configure_pipeline = false;
    return true;
}
