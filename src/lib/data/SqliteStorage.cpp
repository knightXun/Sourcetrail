#include "SqliteStorage.h"

#include "utility/logging/logging.h"

SqliteStorage::SqliteStorage(const FilePath& dbFilePath)
	: m_dbFilePath(dbFilePath.canonical())
{
	m_database.open(m_dbFilePath.str().c_str());

	executeStatement("PRAGMA foreign_keys=ON;");

	m_mode = STORAGE_MODE_UNKNOWN;
}

SqliteStorage::~SqliteStorage()
{
	try
	{
		m_database.close();
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(e.errorMessage());
	}
}

void SqliteStorage::setup()
{
	m_indices = getIndices();

	executeStatement("PRAGMA foreign_keys=ON;");
	setupMetaTable();
	setupTables();
	m_mode = STORAGE_MODE_UNKNOWN;
}

void SqliteStorage::clear()
{
	executeStatement("PRAGMA foreign_keys=OFF;");
	clearMetaTable();
	clearTables();

	setup();
}

void SqliteStorage::setMode(const StorageModeType mode)
{
	if (mode == m_mode)
	{
		return;
	}

	for (size_t i = 0; i < m_indices.size(); i++)
	{
		if (m_indices[i].first & mode)
		{
			m_indices[i].second.createOnDatabase(m_database);
		}
		else
		{
			m_indices[i].second.removeFromDatabase(m_database);
		}
	}

	m_mode = mode;
}

void SqliteStorage::beginTransaction()
{
	executeStatement("BEGIN TRANSACTION;");
}

void SqliteStorage::commitTransaction()
{
	executeStatement("COMMIT TRANSACTION;");
}

void SqliteStorage::rollbackTransaction()
{
	executeStatement("ROLLBACK TRANSACTION;");
}

void SqliteStorage::optimizeMemory() const
{
	executeStatement("VACUUM;");
}

FilePath SqliteStorage::getDbFilePath() const
{
	return m_dbFilePath;
}

bool SqliteStorage::isEmpty() const
{
	size_t storageVersion = getStorageVersion();
	if (storageVersion > 0)
	{
		return false;
	}

	Version applicationVersion = getApplicationVersion();
	return applicationVersion.isEmpty();
}

bool SqliteStorage::isIncompatible() const
{
	size_t storageVersion = getStorageVersion();
	if (storageVersion == 0 || storageVersion != getStaticStorageVersion())
	{
		return true;
	}

	return false;
}

void SqliteStorage::setVersion()
{
	setStorageVersion();
	setApplicationVersion();
}

void SqliteStorage::setupMetaTable()
{
	try
	{
		m_database.execDML(
			"CREATE TABLE IF NOT EXISTS meta("
				"id INTEGER, "
				"key TEXT, "
				"value TEXT, "
				"PRIMARY KEY(id)"
			");"
		);
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());

		throw(std::exception());
	}
}

void SqliteStorage::clearMetaTable()
{
	try
	{
		m_database.execDML("DROP TABLE IF EXISTS main.meta;");
	}
	catch (CppSQLite3Exception& e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

void SqliteStorage::executeStatement(const std::string& statement) const
{
	try
	{
		m_database.execDML(statement.c_str());
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

void SqliteStorage::executeStatement(CppSQLite3Statement& statement) const
{
	try
	{
		statement.execDML();
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
}

int SqliteStorage::executeStatementScalar(const std::string& statement) const
{
	int ret = 0;
	try
	{
		ret = m_database.execScalar(statement.c_str());
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return ret;
}

CppSQLite3Query SqliteStorage::executeQuery(const std::string& statement) const
{
	try
	{
		return m_database.execQuery(statement.c_str());
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return CppSQLite3Query();
}

CppSQLite3Query SqliteStorage::executeQuery(CppSQLite3Statement& statement) const
{
	try
	{
		return statement.execQuery();
	}
	catch (CppSQLite3Exception e)
	{
		LOG_ERROR(std::to_string(e.errorCode()) + ": " + e.errorMessage());
	}
	return CppSQLite3Query();
}

bool SqliteStorage::hasTable(const std::string& tableName) const
{
	CppSQLite3Query q = executeQuery(
		"SELECT name FROM sqlite_master WHERE type='table' AND name='" + tableName + "';"
	);

	if (!q.eof())
	{
		return q.getStringField(0, "") == tableName;
	}

	return false;
}

std::string SqliteStorage::getMetaValue(const std::string& key) const
{
	if (hasTable("meta"))
	{
		CppSQLite3Query q = executeQuery("SELECT value FROM meta WHERE key = '" + key + "';");

		if (!q.eof())
		{
			return q.getStringField(0, "");
		}
	}

	return "";
}

void SqliteStorage::insertOrUpdateMetaValue(const std::string& key, const std::string& value)
{
	CppSQLite3Statement stmt = m_database.compileStatement(std::string(
		"INSERT OR REPLACE INTO meta(id, key, value) VALUES("
			"(SELECT id FROM meta WHERE key = ?), ?, ?"
		");"
	).c_str());

	stmt.bind(1, key.c_str());
	stmt.bind(2, key.c_str());
	stmt.bind(3, value.c_str());
	executeStatement(stmt);
}

size_t SqliteStorage::getStorageVersion() const
{
	std::string storageVersionStr = getMetaValue("storage_version");

	if (storageVersionStr.size())
	{
		return std::stoi(storageVersionStr);
	}

	return 0;
}

void SqliteStorage::setStorageVersion()
{
	insertOrUpdateMetaValue("storage_version", std::to_string(getStaticStorageVersion()));
}

Version SqliteStorage::getApplicationVersion() const
{
	std::string versionStr = getMetaValue("application_version");

	if (versionStr.size())
	{
		return Version::fromString(versionStr);
	}

	return Version();
}

void SqliteStorage::setApplicationVersion()
{
	insertOrUpdateMetaValue("application_version", Version::getApplicationVersion().toString());
}
