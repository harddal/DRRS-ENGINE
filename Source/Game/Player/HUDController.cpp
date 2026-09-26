#include "HUDController.h"

#include <cmath>

#include "Engine/Engine.h"
#include "Game/Item/ItemDatabase.h"

#include "Game/Components.h"

#include "PlayerController.h"


void HUDController::init()
{
	m_crosshair = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair001.png");
	if (!m_crosshair) {
		spdlog::error("Failed to load texture asset: m_crosshair");
	}

	m_crosshair_interact = RenderManager::Get()->driver()->getTexture("content/texture/ui/crosshair/crosshair087.png");
	if (!m_crosshair_interact) {
		spdlog::error("Failed to load texture asset: m_crosshair_interact");
	}

	m_healthbar_background = RenderManager::Get()->driver()->getTexture("content/texture/ui/healthbar_background.png");
	if (!m_healthbar_background) {
		spdlog::error("Failed to load texture asset: m_healthbar_background");
	}

	m_health_icon_empty = RenderManager::Get()->driver()->getTexture("content/texture/ui/health_icon_empty.png");
	if (!m_health_icon_empty) {
		spdlog::error("Failed to load texture asset: m_health_icon_empty");
	}

	m_health_icon_full = RenderManager::Get()->driver()->getTexture("content/texture/ui/health_icon_full.png");
	if (!m_health_icon_full) {
		spdlog::error("Failed to load texture asset: m_health_icon_full");
	}

	m_healthbar_empty = RenderManager::Get()->driver()->getTexture("content/texture/ui/healthbar_empty.png");
	if (!m_healthbar_empty) {
		spdlog::error("Failed to load texture asset: m_healthbar_empty");
	}

	m_healthbar_full = RenderManager::Get()->driver()->getTexture("content/texture/ui/healthbar_full.png");
	if (!m_healthbar_full) {
		spdlog::error("Failed to load texture asset: m_healthbar_full");
	}

	m_ammobackground = RenderManager::Get()->driver()->getTexture("content/texture/ui/ammo_background.png");
	if (!m_ammobackground) {
		spdlog::error("Failed to load texture asset: m_ammobackground");
	}
}

void HUDController::update(PlayerData &data, bool isInventoryDisplayed) const
{
	auto &player = WorldManager::Get()->managerSystem()->getEntityByName("player");

	const float uiScale = static_cast<float>(RenderManager::Get()->getConfiguration().height) / 1080.0f;
	auto S = [uiScale](int px) -> int { return static_cast<int>(px * uiScale); };
	auto imgDest = [&S](irr::video::ITexture* tex, int x, int y) -> irr::core::rect<irr::s32> {
		return irr::core::rect<irr::s32>(x, y, x + S(tex->getSize().Width), y + S(tex->getSize().Height));
	};

	const int screenW = RenderManager::Get()->getConfiguration().width;
	const int screenH = RenderManager::Get()->getConfiguration().height;
	const irr::core::vector2di crosshairCenter(
		screenW / 2 - S(m_crosshair->getSize().Width) / 2,
		screenH / 2 - S(m_crosshair->getSize().Height) / 2);

	if (!m_hide)
	{

		if (player.isValid()) {

			// Underwater is a renderer post-process pass now ("underwater",
			// RenderManager::updateUnderwaterPass) -- not a HUD overlay.

			// --- DEATH SCREEN EFFECT ---
			if (data.currentHealth <= 0)
			{
				RenderManager::Get()->renderRectangle2D(
					irr::core::rect<irr::s32>(0, 0, RenderManager::Get()->getConfiguration().width, RenderManager::Get()->getConfiguration().height),
					irr::video::SColor(120, 255, 0, 0)
				);
			}

			// --- CROSSHAIR ---
			if (!isInventoryDisplayed) {
				/*RenderManager::Get()->renderImage2D(
					m_crosshair,
					_crosshair_center_position);*/

				auto hit = RenderManager::Get()->raycastWorldPosition(
					player.getComponent<CameraComponent>().camera->getAbsolutePosition(),
					player.getComponent<CameraComponent>().targetNode->getAbsolutePosition(),
					true);

				if (hit.node) {
					auto target = WorldManager::Get()->managerSystem()->getEntityByID(hit.node->getID());
					if (target.isValid()) {
						if ((target.hasComponent<InteractionComponent>() || target.hasComponent<ItemComponent>()) &&
							Math::Stable_3D_Distance(player.getComponent<CameraComponent>().camera->getAbsolutePosition(), hit.point) < _player_interact_distance) {
							/*RenderManager::Get()->renderImage2D(
								m_crosshair_interact,
								_crosshair_center_position,
								irr::video::SColor(255, 51, 51, 255));*/

							if (target.hasComponent<ItemComponent>()) {
								const ItemDef& item = ItemDatabase::Get(target.getComponent<ItemComponent>().item);
								auto value = irr::core::stringw(L"Pickup ");
								value += irr::core::stringw(item.name.c_str());

								RenderManager::Get()->renderText2D(
									value,
									TEXT_DEFAULT_FONT::SMALL,
									irr::core::rect<irr::s32>((crosshairCenter.X - (int)(value.size() * 4 / 2)) - 8,
										crosshairCenter.Y + S(48), 0, 0));
							}
							else {
								auto value = irr::core::stringw(L"Interact");

								RenderManager::Get()->renderText2D(
									value,
									TEXT_DEFAULT_FONT::SMALL,
									irr::core::rect<irr::s32>(
										crosshairCenter.X - (int)(value.size() * 4 / 2),
										crosshairCenter.Y + S(48), 0, 0));
							}
						}
						else if (target.hasComponent<NPCComponent>()) {
							// Ported from the old three-value disposition to FACTION.
							// Still commented out along with the rest of the crosshair
							// art in this function - un-commenting this block alone
							// would draw a reticle nothing else here draws.
							//
							// Note this asks the question the RIGHT way round: hostility
							// is a relation, so it is isHostile(target -> player), not a
							// property read off the target on its own.
							/*if (isHostile(target, player))
							{
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 255, 51, 51));
							}
							else if (factionOf(target) == FACTION::PLAYER)
							{
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 51, 255, 51));
							}
							else
							{
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 175, 175, 175));
							}*/
						}
						else if ((target.hasComponent<DamageReceiverComponent>()))
						{
							/*RenderManager::Get()->renderImage2D(
								m_crosshair_interact,
								_crosshair_center_position,
								irr::video::SColor(255, 175, 175, 175));*/
						}
					}
				}
			}

			// --- HEALTH ---
			const int iconW = S(m_health_icon_full->getSize().Width);
			const int iconH = S(m_health_icon_full->getSize().Height);
			const int pipH  = S(m_healthbar_full->getSize().Height);
			const int bgH   = S(m_healthbar_background->getSize().Height);

			auto health_icon_position      = imgDest(m_health_icon_full,       0,                              screenH - iconH);
			auto health_background_position = imgDest(m_healthbar_background,  iconW - S(9),                   screenH - bgH);
			auto health_pip_full_position  = [&](int n) { return imgDest(m_healthbar_full,  iconW - S(9) + S(5) + n * S(25), screenH - S(4) - pipH); };
			auto health_pip_empty_position = [&](int n) { return imgDest(m_healthbar_empty, iconW - S(9) + S(5) + n * S(25), screenH - S(6) - pipH); };

			irr::video::SColor healthbar_color;

			if (data.currentHealth > 0) {
				healthbar_color = irr::video::SColor(255, 251, 105, 98);
			}
			if (data.currentHealth > 24) {
				healthbar_color = irr::video::SColor(255, 252, 252, 153);
			}
			if (data.currentHealth > 51) {
				healthbar_color = irr::video::SColor(255, 121, 222, 121);
			}

			if (data.currentHealth < 25) {
				double current_time = Engine::Get()->getCurrentTime();
				static double last_time = 0.0;
				static bool display_empty = true;

				if (current_time - last_time > 500.0) {
					display_empty = !display_empty;

					last_time = current_time;
				}

				if (display_empty) {
					RenderManager::Get()->renderImage2DScaled(
						m_health_icon_empty,
						health_icon_position, healthbar_color);
				}
				else {
					RenderManager::Get()->renderImage2DScaled(
						m_health_icon_full,
						health_icon_position, healthbar_color);
				}
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_health_icon_full,
					health_icon_position, healthbar_color);
			}

			RenderManager::Get()->renderImage2DScaled(
				m_healthbar_background,
				health_background_position);

			if (data.currentHealth > 0) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(0), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(0), healthbar_color);
			}
			if (data.currentHealth > 10) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(1), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(1), healthbar_color);
			}
			if (data.currentHealth > 20) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(2), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(2), healthbar_color);
			}
			if (data.currentHealth > 30) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(3), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(3), healthbar_color);
			}
			if (data.currentHealth > 40) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(4), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(4), healthbar_color);
			}
			if (data.currentHealth > 50) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(5), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(5), healthbar_color);
			}
			if (data.currentHealth > 60) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(6), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(6), healthbar_color);
			}
			if (data.currentHealth > 70) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(7), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(7), healthbar_color);
			}
			if (data.currentHealth > 80) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(8), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(8), healthbar_color);
			}
			if (data.currentHealth > 90) {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_full,
					health_pip_full_position(9), healthbar_color);
			}
			else {
				RenderManager::Get()->renderImage2DScaled(
					m_healthbar_empty,
					health_pip_empty_position(9), healthbar_color);
			}

			// --- BREATH ---
			// A second pip row directly above the health pips, reusing their art
			// tinted blue. Faded in while holding breath and while refilling, so
			// it is invisible for anyone who never goes under. Pulses when low
			// and turns red once the air is gone and damage is being dealt.
			if (data.breathHudAlpha > 0.01f && data.currentHealth > 0)
			{
				const double now = Engine::Get()->getCurrentTime();
				const float  pulse = 0.5f + 0.5f * sinf(static_cast<float>(now) * 0.012f);   // ~2 Hz

				float alpha = data.breathHudAlpha;
				if (data.breathFraction < 0.25f && !data.isDrowning)
					alpha *= 0.45f + 0.55f * pulse;

				const irr::u32 a = static_cast<irr::u32>(alpha * 255.0f);
				const irr::video::SColor breathColor = data.isDrowning
					? irr::video::SColor(a, 251, 105, 98)
					: irr::video::SColor(a, 120, 200, 255);

				const int rowX  = iconW - S(9) + S(5);
				const int rowUp = bgH + S(6);   // clears the health background

				// Same rule as the health row: pip n is lit while more than n
				// tenths remain, so the last pip goes out as the air hits zero.
				for (int n = 0; n < 10; ++n)
				{
					if (data.breathFraction * 10.0f > static_cast<float>(n))
						RenderManager::Get()->renderImage2DScaled(
							m_healthbar_full,
							imgDest(m_healthbar_full, rowX + n * S(25), screenH - rowUp - S(4) - pipH),
							breathColor);
					else
						RenderManager::Get()->renderImage2DScaled(
							m_healthbar_empty,
							imgDest(m_healthbar_empty, rowX + n * S(25), screenH - rowUp - S(6) - pipH),
							breathColor);
				}

				// Drowning: a faint red tint over the whole view, in time with the pulse,
				// on top of the hurt sound each damage tick already plays.
				if (data.isDrowning)
				{
					RenderManager::Get()->renderRectangle2D(
						irr::core::rect<irr::s32>(0, 0, screenW, screenH),
						irr::video::SColor(static_cast<irr::u32>(20.0f + 40.0f * pulse), 120, 0, 0));
				}
			}

			// --- AMMO ---
			// Asked of the weapon every frame rather than read from a cached
			// PlayerData field. The old ammoDisplayValue was the same stale-copy
			// pattern that already bites health — currentHealth is refreshed from
			// the component once a frame, and anything written to the copy is
			// overwritten before it is read. There is no reason to repeat that.
			if (auto* weapons = g_PlayerController ? g_PlayerController->weaponController() : nullptr)
			{
				const int magazine = weapons->currentDisplayAmmo();

				// -1 means this weapon has no ammunition readout at all — melee
				// and the pitchfork. Nothing is drawn rather than a zero.
				if (magazine >= 0)
				{
					const int reserve = weapons->currentReserveAmmo();

					RenderManager::Get()->renderImage2D(
						m_ammobackground,
						irr::core::vector2di(
							RenderManager::Get()->getConfiguration().width  - m_ammobackground->getSize().Width,
							RenderManager::Get()->getConfiguration().height - m_ammobackground->getSize().Height));

					auto value = irr::core::stringw(magazine);

					// The staff and the crossbow have no pool behind them, so the
					// separator is only drawn when there is a second number.
					if (reserve >= 0)
					{
						value += L" / ";
						value += reserve;
					}

					RenderManager::Get()->renderText2D(
						value,
						TEXT_DEFAULT_FONT::SMALL,
						irr::core::rect<irr::s32>(
							RenderManager::Get()->getConfiguration().width  - (m_ammobackground->getSize().Width / 2) + 20 - (int)value.size() * 8,
							RenderManager::Get()->getConfiguration().height + 30 - m_ammobackground->getSize().Height - 10, 0, 0),
						irr::video::SColor(255, 255, 255, 255));
				}
			}
		}
	}
}

void HUDController::destroy()
{

}
