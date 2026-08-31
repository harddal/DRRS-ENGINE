#include "PlayerInventory.h"

#include <spdlog/spdlog.h>

#include "Game/Item/ItemDatabase.h"

int PlayerInventory::add(const std::string& id, int count, const std::string& data)
{
	if (id.empty() || count <= 0)
		return 0;

	const ItemDef& def = ItemDatabase::Get(id);
	if (!def.valid())
	{
		spdlog::warn("PlayerInventory::add(): unknown item '{}'", id);
		return 0;
	}

	int remaining = count;

	// Top up existing stacks first, but ONLY those carrying the same per-instance
	// data. Two half-spent charges are not interchangeable with each other, and
	// merging them would quietly average away whatever the item's script tracks.
	if (def.stackable)
	{
		for (auto& stack : m_stacks)
		{
			if (stack.id != id || stack.data != data)
				continue;

			const int room = def.maxStack - stack.count;
			if (room <= 0)
				continue;

			const int moved = (remaining < room) ? remaining : room;

			stack.count += moved;
			remaining   -= moved;

			if (remaining == 0)
				return count;
		}
	}

	// Whatever did not fit becomes new stacks. Non-stackable items land here
	// every time, one stack each.
	while (remaining > 0)
	{
		const int size = def.stackable
			? ((remaining < def.maxStack) ? remaining : def.maxStack)
			: 1;

		ItemStack stack(id, size);
		stack.data = data;

		m_stacks.emplace_back(std::move(stack));

		remaining -= size;
	}

	return count;
}

int PlayerInventory::remove(const std::string& id, int count)
{
	if (id.empty() || count <= 0)
		return 0;

	int removed = 0;

	// Walked back to front so erasing an emptied stack cannot skip the next one.
	for (size_t i = m_stacks.size(); i-- > 0 && removed < count; )
	{
		if (m_stacks[i].id != id)
			continue;

		const int want  = count - removed;
		const int taken = (want < m_stacks[i].count) ? want : m_stacks[i].count;

		m_stacks[i].count -= taken;
		removed           += taken;

		if (m_stacks[i].count <= 0)
			m_stacks.erase(m_stacks.begin() + i);
	}

	return removed;
}

int PlayerInventory::count(const std::string& id) const
{
	int total = 0;

	for (auto& stack : m_stacks)
	{
		if (stack.id == id)
			total += stack.count;
	}

	return total;
}

std::vector<size_t> PlayerInventory::indicesInCategory(const std::string& category) const
{
	std::vector<size_t> out;

	for (size_t i = 0; i < m_stacks.size(); ++i)
	{
		const ItemDef& def = ItemDatabase::Get(m_stacks[i].id);

		if (def.valid() && def.category == category)
			out.emplace_back(i);
	}

	return out;
}

ItemStack* PlayerInventory::stackAt(size_t index)
{
	if (index >= m_stacks.size())
		return nullptr;

	return &m_stacks[index];
}

void PlayerInventory::load(const std::vector<ItemStack>& stacks)
{
	m_stacks.clear();

	for (auto& stack : stacks)
	{
		if (stack.count <= 0)
			continue;

		// An id the current content set no longer defines is DROPPED, not kept.
		// Keeping it would leave an entry in the pouch with no name, no icon and
		// no use — and the panel would have to grow a special case for it.
		if (!ItemDatabase::Exists(stack.id))
		{
			spdlog::warn("PlayerInventory::load(): save references unknown item '{}' — dropped", stack.id);
			continue;
		}

		m_stacks.emplace_back(stack);
	}
}
