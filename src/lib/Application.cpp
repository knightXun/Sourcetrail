#include "Application.h"

#include "utility/logging/logging.h"
#include "utility/logging/LogManager.h"
#include "utility/messaging/MessageQueue.h"
#include "utility/messaging/type/MessageStatus.h"
#include "utility/messaging/type/MessageShowStartScreen.h"
#include "utility/scheduling/TaskScheduler.h"
#include "utility/tracing.h"
#include "utility/UserPaths.h"
#include "utility/Version.h"

#include "component/view/DialogView.h"
#include "component/view/GraphViewStyle.h"
#include "component/controller/NetworkFactory.h"
#include "component/controller/IDECommunicationController.h"
#include "component/view/MainView.h"
#include "component/view/ViewFactory.h"
#include "data/StorageCache.h"
#include "LicenseChecker.h"
#include "settings/ApplicationSettings.h"
#include "settings/ProjectSettings.h"
#include "settings/ColorScheme.h"

void Application::createInstance(
	const Version& version, ViewFactory* viewFactory, NetworkFactory* networkFactory
){
	Version::setApplicationVersion(version);
	loadSettings();

	TaskScheduler::getInstance();
	MessageQueue::getInstance();

	bool hasGui = (viewFactory != nullptr);
	s_instance = std::shared_ptr<Application>(new Application(hasGui));

	s_instance->m_storageCache = std::make_shared<StorageCache>();

	if (hasGui)
	{
		s_instance->m_componentManager = ComponentManager::create(viewFactory, s_instance->m_storageCache.get());

		s_instance->m_mainView = viewFactory->createMainView();
		s_instance->updateTitle();

		s_instance->m_componentManager->setup(s_instance->m_mainView.get());
		s_instance->m_mainView->loadLayout();

		MessageShowStartScreen().dispatch();
	}

	if (networkFactory != nullptr)
	{
		s_instance->m_ideCommunicationController =
			networkFactory->createIDECommunicationController(s_instance->m_storageCache.get());
		s_instance->m_ideCommunicationController->startListening();
	}

	s_instance->startMessagingAndScheduling();
}

std::shared_ptr<Application> Application::getInstance()
{
	return s_instance;
}

void Application::destroyInstance()
{
	s_instance.reset();
}

void Application::loadSettings()
{
	MessageStatus("Load settings: " + UserPaths::getAppSettingsPath()).dispatch();

	std::shared_ptr<ApplicationSettings> settings = ApplicationSettings::getInstance();
	settings->load(FilePath(UserPaths::getAppSettingsPath()));

	LogManager::getInstance()->setLoggingEnabled(settings->getLoggingEnabled());

	loadStyle(settings->getColorSchemePath());
}

void Application::loadStyle(const FilePath& colorSchemePath)
{
	ColorScheme::getInstance()->load(colorSchemePath);
	GraphViewStyle::loadStyleSettings();
}

std::shared_ptr<Application> Application::s_instance;

Application::Application(bool withGUI)
	: m_hasGUI(withGUI)
	, m_isInTrial(true)
{
	LicenseChecker::createInstance();
}

Application::~Application()
{
	MessageQueue::getInstance()->stopMessageLoop();
	TaskScheduler::getInstance()->stopSchedulerLoop();
	if (m_hasGUI)
	{
		m_mainView->saveLayout();
	}
}

const std::shared_ptr<Project> Application::getCurrentProject()
{
	return m_project;
}

bool Application::hasGUI()
{
	return m_hasGUI;
}

int Application::handleDialog(const std::string& message)
{
	return getDialogView()->confirm(message);
}

int Application::handleDialog(const std::string& message, const std::vector<std::string>& options)
{
	return getDialogView()->confirm(message, options);
}

std::shared_ptr<DialogView> Application::getDialogView()
{
	if (m_componentManager)
	{
		return m_componentManager->getDialogView();
	}

	return std::shared_ptr<DialogView>();
}

bool Application::isInTrial() const
{
	return m_isInTrial;
}

void Application::createAndLoadProject(const FilePath& projectSettingsFilePath)
{
	MessageStatus("Loading Project: " + projectSettingsFilePath.str(), false, true).dispatch();
	try
	{
		updateRecentProjects(projectSettingsFilePath);

		m_storageCache->clear();
		m_storageCache->setSubject(nullptr);

		m_project = std::make_shared<Project>(std::make_shared<ProjectSettings>(projectSettingsFilePath), m_storageCache.get());

		if (m_project)
		{
			m_project->load();

			if (m_hasGUI)
			{
				updateTitle();
				m_mainView->hideStartScreen();
			}
		}
		else
		{
			LOG_ERROR_STREAM(<< "Failed to load project.");
			MessageStatus("Failed to load project: " + projectSettingsFilePath.str(), true).dispatch();
		}
	}
	catch (std::exception& e)
	{
		LOG_ERROR_STREAM(<< "Failed to load project, exception thrown: " << e.what());
		MessageStatus("Failed to load project, exception was thrown: " + projectSettingsFilePath.str(), true).dispatch();
	}
	catch (...)
	{
		LOG_ERROR_STREAM(<< "Failed to load project, unknown exception thrown.");
		MessageStatus("Failed to load project, unknown exception was thrown: " + projectSettingsFilePath.str(), true).dispatch();
	}

	if (m_hasGUI)
	{
		m_componentManager->clearComponents();
	}
}

void Application::refreshProject(bool force)
{
	if (m_project)
	{
		bool indexing = m_project->refresh(force);
		if (indexing)
		{
			m_storageCache->clear();
			if (m_hasGUI)
			{
				m_componentManager->refreshViews();
			}
		}
	}
}

void Application::handleMessage(MessageActivateWindow* message)
{
	if (m_hasGUI)
	{
		m_mainView->activateWindow();
	}
}

void Application::handleMessage(MessageEnteredLicense* message)
{
	MessageStatus("Found valid license key, unlocked application.").dispatch();

	m_isInTrial = false;

	updateTitle();
}

void Application::handleMessage(MessageFinishedParsing* message)
{
	logStorageStats();

	if (m_hasGUI)
	{
		MessageRefresh().refreshUiOnly().dispatch();
	}
}

void Application::handleMessage(MessageLoadProject* message)
{
	TRACE("app load project");

	FilePath projectSettingsFilePath(message->projectSettingsFilePath);
	if (projectSettingsFilePath.empty())
	{
		return;
	}

	if (m_project && projectSettingsFilePath == m_project->getProjectSettingsFilePath())
	{
		if (message->forceRefresh)
		{
			m_project->setStateSettingsUpdated();
			refreshProject(false);
		}

		return;
	}

	createAndLoadProject(projectSettingsFilePath);
}

void Application::handleMessage(MessageRefresh* message)
{
	TRACE("app refresh");

	if (message->loadStyle)
	{
		loadStyle(ApplicationSettings::getInstance()->getColorSchemePath());
	}

	if (m_hasGUI)
	{
		m_componentManager->refreshViews();
	}

	if (!message->uiOnly)
	{
		refreshProject(message->all);
	}
}

void Application::handleMessage(MessageSwitchColorScheme* message)
{
	MessageStatus("Switch color scheme: " + message->colorSchemePath.str()).dispatch();

	loadStyle(message->colorSchemePath);
	MessageRefresh().refreshUiOnly().noReloadStyle().dispatch();
}

void Application::startMessagingAndScheduling()
{
	TaskScheduler::getInstance()->startSchedulerLoopThreaded();
	MessageQueue::getInstance()->setSendMessagesAsTasks(true);
	MessageQueue::getInstance()->startMessageLoopThreaded();
}

void Application::updateRecentProjects(const FilePath& projectSettingsFilePath)
{
	if (m_hasGUI)
	{
		ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
		std::vector<FilePath> recentProjects = appSettings->getRecentProjects();
		if (recentProjects.size())
		{
			std::vector<FilePath>::iterator it = std::find(recentProjects.begin(), recentProjects.end(), projectSettingsFilePath);
			if (it != recentProjects.end())
			{
				recentProjects.erase(it);
			}
		}

		recentProjects.insert(recentProjects.begin(), projectSettingsFilePath);
		while (recentProjects.size() > 7)
		{
			recentProjects.pop_back();
		}

		appSettings->setRecentProjects(recentProjects);
		appSettings->save(UserPaths::getAppSettingsPath());

		m_mainView->updateRecentProjectMenu();
	}
}

void Application::logStorageStats() const
{
	if (!ApplicationSettings::getInstance()->getLoggingEnabled())
	{
		return;
	}

	std::stringstream ss;
	StorageStats stats = m_storageCache->getStorageStats();

	ss << "\nGraph:\n";
	ss << "\t" << stats.nodeCount << " Nodes\n";
	ss << "\t" << stats.edgeCount << " Edges\n";

	ss << "\nCode:\n";
	ss << "\t" << stats.fileCount << " Files\n";
	ss << "\t" << stats.fileLOCCount << " Lines of Code\n";


	ErrorCountInfo errorCount = m_storageCache->getErrorCount();

	ss << "\nErrors:\n";
	ss << "\t" << errorCount.total << " Errors\n";
	ss << "\t" << errorCount.fatal << " Fatal Errors\n";

	LOG_INFO(ss.str());
}

void Application::updateTitle()
{
	if (m_hasGUI)
	{
		std::string title = m_isInTrial ? "Sourcetrail Trial" : "Sourcetrail";

		if (m_project)
		{
			FilePath projectPath = m_project->getProjectSettingsFilePath();

			if (!projectPath.empty())
			{
				title += " - " + projectPath.fileName();
			}
		}

		m_mainView->setTitle(title);
	}
}
