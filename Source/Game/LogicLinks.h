#pragma once

#include <string>
#include <vector>

// Helpers for the comma-separated entity-name lists stored in
// LogicComponent::receiver and TriggerZoneComponent::triggered_entity.
namespace LogicLinks
{
	// Tokens are intentionally NOT trimmed: runtime name matching is untrimmed,
	// so trimming here would change which entities existing scenes resolve.
	std::vector<std::string> splitNameList(const std::string& csv);

	std::string joinNameList(const std::vector<std::string>& tokens);

	// Appends name if no existing token matches it exactly. Returns true if appended.
	bool appendUniqueName(std::string& csv, const std::string& name);

	// Removes every token exactly matching name. Returns true if anything was removed.
	bool removeName(std::string& csv, const std::string& name);
}
