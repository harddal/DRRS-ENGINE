#include "HUDController.h"

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

	m_water_overlay = RenderManager::Get()->driver()->getTexture("content/texture/ui/water_overlay.png");
	if (!m_water_overlay) {
		spdlog::error("Failed to load texture asset: m_water_overlay");
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

			// --- WATER ---
			if (g_PlayerController->isHeadUnderWater())
			{
				RenderManager::Get()->renderImage2DScaled(
					m_water_overlay,
					irr::core::rect<irr::s32>(0, 0, screenW, screenH));
			}

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
								auto item = ItemDatabase::GetItemByName(target.getComponent<ItemComponent>().item);
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
							/*switch (target.getComponent<NPCComponent>().disposition)
							{
							case NPC_AI_DISPOSITION::NEUTRAL:
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 175, 175, 175));
								break;
							case NPC_AI_DISPOSITION::ENEMY:
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 255, 51, 51));
								break;
							case NPC_AI_DISPOSITION::FRIENDLY:
								RenderManager::Get()->renderImage2D(
									m_crosshair_interact,
									_crosshair_center_position,
									irr::video::SColor(255, 51, 255, 51));
								break;
							default:
								break;
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

			// --- AMMO ---
			// if (data.isWeaponEquipped && data.ammoDisplayValue >= 0) {
			// 	RenderManager::Get()->renderImage2D(
			// 		m_ammobackground,
			// 		irr::core::vector2di(RenderManager::Get()->getConfiguration().width - m_ammobackground->getSize().Width, RenderManager::Get()->getConfiguration().height - m_ammobackground->getSize().Height));

			// 	auto value = irr::core::stringw(L"Ammo: ");
			// 	value += data.ammoDisplayValue;
			// 	RenderManager::Get()->renderText2D(
			// 		value,
			// 		TEXT_DEFAULT_FONT::SMALL,
			// 		irr::core::rect<irr::s32>(RenderManager::Get()->getConfiguration().width - (m_ammobackground->getSize().Width / 2) + 20 - value.size() * 8, RenderManager::Get()->getConfiguration().height + 30 - m_ammobackground->getSize().Height - 10, 0, 0),
			// 		irr::video::SColor(255, 255, 255, 255));
			// }
		}
	}
}

void HUDController::destroy()
{

}
