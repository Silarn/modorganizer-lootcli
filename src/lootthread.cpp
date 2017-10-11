#include "lootthread.h"
#pragma warning (push, 0)

#include <loot/api.h>

#pragma warning (pop)

#include <thread>
#include <mutex>
#include <ctype.h>
#include <map>
#include <exception>
#include <stdio.h>
#include <stddef.h>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <sstream>
#include <fstream>
#include <memory>

#include <boost/assign.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/locale.hpp>
#include <game_settings.h>
#include <game.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <yaml-cpp/yaml.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlobj.h>

using namespace loot;
namespace fs = boost::filesystem;

using boost::property_tree::ptree;
using boost::property_tree::write_json;
using std::lock_guard;
using std::recursive_mutex;


LOOTWorker::LOOTWorker()
	: m_GameId(GameType::tes5)
	, m_Language(MessageContent::defaultLanguage)
	, m_GameName("Skyrim")
{
}


std::string ToLower(const std::string &text)
{
	std::string result = text;
	std::transform(text.begin(), text.end(), result.begin(), tolower);
	return result;
}


void LOOTWorker::setGame(const std::string &gameName)
{
	static std::map<std::string, GameType> gameMap = boost::assign::map_list_of
	("oblivion", GameType::tes4)
		("fallout3", GameType::fo3)
		("fallout4", GameType::fo4)
		("falloutnv", GameType::fonv)
		("skyrim", GameType::tes5)
		("skyrimse", GameType::tes5se);

	auto iter = gameMap.find(ToLower(gameName));
	if (iter != gameMap.end()) {
		m_GameName = gameName;
		if (ToLower(gameName) == "skyrimse") {
			m_GameName = "Skyrim Special Edition";
		}
		m_GameId = iter->second;
	}
	else {
		throw std::runtime_error((boost::format("invalid game name \"%1%\"") % gameName).str());
	}
}

void LOOTWorker::setGamePath(const std::string &gamePath)
{
	m_GamePath = gamePath;
}

void LOOTWorker::setOutput(const std::string &outputPath)
{
	m_OutputPath = outputPath;
}

void LOOTWorker::setUpdateMasterlist(bool update)
{
	m_UpdateMasterlist = update;
}
void LOOTWorker::setPluginListPath(const std::string &pluginListPath) {
	m_PluginListPath = pluginListPath;
}

void LOOTWorker::setLanguageCode(const std::string &languageCode) {
	m_Language = languageCode;
}

/*void LOOTWorker::handleErr(unsigned int resultCode, const char *description)
{
if (resultCode != LVAR(loot_ok)) {
const char *errMessage;
unsigned int lastError = LFUNC(loot_get_error_message)(&errMessage);
throw std::runtime_error((boost::format("%1% failed: %2% (code %3%)") % description % errMessage % lastError).str());
} else {
progress(description);
}
}*/


boost::filesystem::path GetLOOTAppData() {
	TCHAR path[MAX_PATH];

	HRESULT res = ::SHGetFolderPath(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path);

	if (res == S_OK) {
		return fs::path(path) / "LOOT";
	}
	else {
		return fs::path("");
	}
}

boost::filesystem::path LOOTWorker::settingsPath()
{
	return GetLOOTAppData() / "settings.yaml";
}

boost::filesystem::path LOOTWorker::l10nPath()
{
	return GetLOOTAppData() / "resources" / "l10n";
}

boost::filesystem::path LOOTWorker::dataPath()
{
	return fs::path(m_GamePath) / "Data";
}

std::string LOOTWorker::formatDirty(const PluginCleaningData &cleaningData) {

	const std::string itmRecords = std::to_string(cleaningData.GetITMCount()) + " ITM record(s)";
	const std::string deletedReferences = std::to_string(cleaningData.GetDeletedReferenceCount()) + " deleted reference(s)";
	const std::string deletedNavmeshes = std::to_string(cleaningData.GetDeletedNavmeshCount()) + " deleted navmesh(es)";
	std::string message;
	if (cleaningData.GetITMCount() > 0 && cleaningData.GetDeletedReferenceCount() > 0 && cleaningData.GetDeletedNavmeshCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + itmRecords + ", " + deletedReferences + " and " + deletedNavmeshes + ".";
	else if (cleaningData.GetITMCount() == 0 && cleaningData.GetDeletedReferenceCount() == 0 && cleaningData.GetDeletedNavmeshCount() == 0)
		message = cleaningData.GetCleaningUtility() + " found dirty edits.";
	else if (cleaningData.GetITMCount() == 0 && cleaningData.GetDeletedReferenceCount() > 0 && cleaningData.GetDeletedNavmeshCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + deletedReferences + " and " + deletedNavmeshes + ".";
	else if (cleaningData.GetITMCount() > 0 && cleaningData.GetDeletedReferenceCount() == 0 && cleaningData.GetDeletedNavmeshCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + itmRecords + " and " + deletedNavmeshes + ".";
	else if (cleaningData.GetITMCount() > 0 && cleaningData.GetDeletedReferenceCount() > 0 && cleaningData.GetDeletedNavmeshCount() == 0)
		message = cleaningData.GetCleaningUtility() + " found " + itmRecords + " and " + deletedReferences + ".";
	else if (cleaningData.GetITMCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + itmRecords + ".";
	else if (cleaningData.GetDeletedReferenceCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + deletedReferences + ".";
	else if (cleaningData.GetDeletedNavmeshCount() > 0)
		message = cleaningData.GetCleaningUtility() + " found " + deletedNavmeshes + ".";

	if (cleaningData.GetInfo().empty()) {
		return Message(MessageType::warn, message).ToSimpleMessage(m_Language).text;
	}

	auto info = cleaningData.GetInfo();
	for (auto& content : info) {
		content = MessageContent(message + " " + content.GetText(), content.GetLanguage());
	}

	return Message(MessageType::warn, info).ToSimpleMessage(m_Language).text;
}

void LOOTWorker::getSettings(YAML::Node& settings) {
	m_GameSettings = GameSettings(m_GameId);
	if (settings["games"]) {
		std::vector<GameSettings> parsedSettings = settings["games"].as<std::vector<GameSettings>>();

		auto pos = find_if(begin(parsedSettings), end(parsedSettings), [&](const GameSettings& gameSettings) {
			if (gameSettings.Type() == m_GameSettings.Type())
				return true;
			return false;
		});
		m_GameSettings.SetGamePath(pos->GamePath());
	}

	if (settings["language"])
		m_Language = settings["language"].as<std::string>();

	if (m_Language != MessageContent::defaultLanguage) {
		BOOST_LOG_TRIVIAL(debug) << "Initialising language settings.";
		BOOST_LOG_TRIVIAL(debug) << "Selected language: " << m_Language;

		//Boost.Locale initialisation: Generate and imbue locales.
		boost::locale::generator gen;
		std::locale::global(gen(m_Language + ".UTF-8"));
		loot::InitialiseLocale(m_Language + ".UTF-8");
		boost::filesystem::path::imbue(std::locale());
	}
}

int LOOTWorker::run()
{
	// Do some preliminary locale / UTF-8 support setup here, in case the settings file reading requires it.
	//Boost.Locale initialisation: Specify location of language dictionaries.
	boost::locale::generator gen;
	gen.add_messages_path(l10nPath().string());
	gen.add_messages_domain("loot");

	//Boost.Locale initialisation: Generate and imbue locales.
	std::locale::global(gen("en.UTF-8"));
	InitialiseLocale("en.UTF-8");
	boost::filesystem::path::imbue(std::locale());

	SetLoggingVerbosity(LogVerbosity::warning);

	try {
		// ensure the loot directory exists
		fs::path lootAppData = GetLOOTAppData();
		if (lootAppData.empty()) {
			errorOccured("failed to create loot app data path");
			return 1;
		}

		if (!fs::exists(lootAppData)) {
			fs::create_directory(lootAppData);
		}

		auto gameHandle = CreateGameHandle(m_GameId, m_GamePath);
		auto db = gameHandle->GetDatabase();

		fs::path settings = settingsPath();

		boost::filesystem::ifstream in(settings);
		YAML::Node settingsData = YAML::Load(in);

		getSettings(settingsData);

		Game loadedGame = Game(m_GameSettings, GetLOOTAppData());

		bool mlUpdated = false;
		if (m_UpdateMasterlist) {
			progress("checking masterlist existence");
			if (!fs::exists(loadedGame.MasterlistPath())) {
				fs::create_directories(loadedGame.MasterlistPath().parent_path());
			}
			progress("updating masterlist");

			mlUpdated = db->UpdateMasterlist(loadedGame.MasterlistPath().string(), m_GameSettings.RepoURL(), "v0.10");
		}

		fs::path userlist = loadedGame.UserlistPath();

		progress("loading lists");
		db->LoadLists(loadedGame.MasterlistPath().string(), fs::exists(userlist) ? loadedGame.UserlistPath().string() : "");

		progress("Evaluating lists");
		db->EvalLists();

		progress("Reading loadorder.txt");
		std::vector<std::string> pluginsList;
		for (fs::directory_iterator it(dataPath()); it != fs::directory_iterator(); ++it) {
			if (fs::is_regular_file(it->status()) && gameHandle->IsValidPlugin(it->path().filename().string())) {
				std::string name = it->path().filename().string();
				BOOST_LOG_TRIVIAL(info) << "Found plugin: " << name;

				pluginsList.push_back(name);
			}
		}

		progress("Sorting Plugins");
		std::vector<std::string> sortedPlugins = gameHandle->SortPlugins(pluginsList);

		progress("Writing loadorder.txt");
		std::ofstream outf(m_PluginListPath);
		if (!outf) {
			errorOccured("failed to open loadorder.txt to rewrite it");
			return 1;
		}
		outf << "# This file was automatically generated by Mod Organizer." << std::endl;
		for (const std::string &plugin : sortedPlugins) {
			outf << plugin << std::endl;
		}
		outf.close();

		ptree report;

		progress("retrieving loot messages");
		for (size_t i = 0; i < sortedPlugins.size(); ++i) {
			report.add("name", sortedPlugins[i]);

			std::vector<Message> pluginMessages;
			pluginMessages = db->GetGeneralMessages(true);

			if (!pluginMessages.empty()) {
				for (Message message : pluginMessages) {
					const char *type;
					if (message.GetType() == MessageType::say) {
						type = "info";
					}
					else if (message.GetType() == MessageType::warn) {
						type = "warn";
					}
					else if (message.GetType() == MessageType::error) {
						type = "error";
					}
					else {
						type = "unknown";
						errorOccured((boost::format("invalid message type %1%") % type).str());
					}
					report.add("messages.type", type);
					report.add("messages.message", message.ToSimpleMessage(m_Language).text);
				}
			}

			std::set<PluginCleaningData> dirtyInfo = db->GetPluginMetadata(sortedPlugins[i]).GetDirtyInfo();
			for (const auto &element : dirtyInfo) {
				report.add("dirty", formatDirty(element));
			}
		}

		std::ofstream buf;
		buf.open(m_OutputPath.c_str());
		write_json(buf, report, false);
	}
	catch (const std::exception &e) {
		errorOccured((boost::format("LOOT failed: %1%") % e.what()).str());
		return 1;
	}
	progress("done");

	return 0;
}

void LOOTWorker::progress(const std::string &step)
{
	BOOST_LOG_TRIVIAL(info) << "[progress] " << step;
	fflush(stdout);
}

void LOOTWorker::errorOccured(const std::string &message)
{
	BOOST_LOG_TRIVIAL(error) << message;
	fflush(stdout);
}
