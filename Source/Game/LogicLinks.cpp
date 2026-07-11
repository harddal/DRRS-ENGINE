#include "LogicLinks.h"

#include <algorithm>
#include <sstream>

namespace LogicLinks
{

std::vector<std::string> splitNameList(const std::string& csv)
{
	std::vector<std::string> tokens;
	std::stringstream ss(csv);
	std::string tok;
	while (getline(ss, tok, ','))
		tokens.push_back(tok);
	return tokens;
}

std::string joinNameList(const std::vector<std::string>& tokens)
{
	std::string csv;
	for (auto i = 0U; i < tokens.size(); i++)
	{
		if (i > 0) csv += ',';
		csv += tokens[i];
	}
	return csv;
}

bool appendUniqueName(std::string& csv, const std::string& name)
{
	if (name.empty())
		return false;

	for (auto& tok : splitNameList(csv))
		if (tok == name)
			return false;

	if (csv.empty())
		csv = name;
	else
		csv += ',' + name;

	return true;
}

bool removeName(std::string& csv, const std::string& name)
{
	auto tokens = splitNameList(csv);
	auto before = tokens.size();

	tokens.erase(std::remove(tokens.begin(), tokens.end(), name), tokens.end());

	if (tokens.size() == before)
		return false;

	csv = joinNameList(tokens);
	return true;
}

}
