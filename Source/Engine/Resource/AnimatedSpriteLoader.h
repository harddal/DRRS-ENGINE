#pragma once

#include <string>
#include <vector>

#include <irrlicht.h>

struct AnimatedSprite
{
	std::string file, name;

	unsigned int id;
	int frame_x, frame_y;

	std::vector<irr::video::ITexture*> texture_list;

	AnimatedSprite() : id(-1), frame_x(0), frame_y(0) {}
	AnimatedSprite(std::string name, unsigned int frame_x, unsigned int frame_y)
	{
		this->name = name;
		this->frame_x = frame_x;
		this->frame_y = frame_y;
	}
};

class AnimatedSpriteLoader
{
public:
	void loadAnimatedSprites();

	AnimatedSprite getAnimatedSprite(const std::string& name);

private:
	std::vector<AnimatedSprite> m_animatedSpriteList;

};