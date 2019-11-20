#pragma once

#include <ToolboxModule.h>
#include <ToolboxUIElement.h>

#include <GWCA/GameEntities/Agent.h>

#include <GWCA/Utilities/Hook.h>

#include <GWCA/Constants/Maps.h>

#include <GWCA/Packets/StoC.h>

#include "mp3.h"

class ZrawDeepModule : public ToolboxModule {
	ZrawDeepModule() {};
	~ZrawDeepModule() {
		if (mp3) delete mp3;
	};
private:
	Mp3* mp3;
	bool transmog_done = false;
	bool checked_enabled = false;
	bool valid_for_map = false;
    uint32_t kanaxai_agent_id = 0;
    
	bool enabled = true;
    bool transmo_team_in_explorable = true;
    bool rewrite_npc_dialogs = true;
    bool kanaxais_true_form = true;

	clock_t pending_transmog = 0;
public:
	static ZrawDeepModule& Instance() {
		static ZrawDeepModule instance;
		return instance;
	}

	const char* Name() const override { return "Zraw 24h Deep!"; }
	void Initialize() override;
	void Terminate() override;
	void Update(float delta) override;
    void DrawSettingInternal() override;
	void DisplayDialogue(GW::Packet::StoC::DisplayDialogue*);
	void PlayKanaxaiDialog(uint8_t idx);

	void SetTransmogs();
	void Reset() {
		transmog_done = false;
		checked_enabled = false;
		valid_for_map = false;
	}
	void Disable() {
        enabled = false;
		Reset();
	}
	void Enable() {
        enabled = true;
		Reset();
	}
	bool IsEnabled() {
        return enabled;
	}
	void Toggle() {
        enabled = !enabled;
		Reset();
	}

	GW::HookEntry ZrawDeepModule_StoCs;
};
