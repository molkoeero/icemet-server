#include "database.hpp"

#include <stdexcept>

#include "util/log.hpp"
#include "util/strfmt.hpp"

#define FLOAT_REPR "%.24f"
#define MAX_ROWS "1000"

static const char* createDBQuery = "CREATE DATABASE %s;";
static const char* createParticleTableQuery = "CREATE TABLE %s ("
"id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
"datetime DATETIME NOT NULL,"
"ms SMALLINT UNSIGNED NOT NULL,"
"sensor TINYINT UNSIGNED NOT NULL,"
"frame INT UNSIGNED NOT NULL,"
"particle INT UNSIGNED NOT NULL,"
"x FLOAT NOT NULL,"
"y FLOAT NOT NULL,"
"z FLOAT NOT NULL,"
"diam FLOAT NOT NULL,"
"circularity FLOAT NOT NULL,"
"dnr TINYINT UNSIGNED NOT NULL,"
"effpsz FLOAT NOT NULL,"
"subx INT UNSIGNED NOT NULL,"
"suby INT UNSIGNED NOT NULL,"
"subw INT UNSIGNED NOT NULL,"
"subh INT UNSIGNED NOT NULL,"
"PRIMARY KEY (id)"
");";
static const char* createStatsTableQuery = "CREATE TABLE %s ("
"id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
"datetime DATETIME NOT NULL,"
"lwc FLOAT NOT NULL,"
"mvd FLOAT NOT NULL,"
"conc FLOAT NOT NULL,"
"frames INT UNSIGNED NOT NULL,"
"particles INT UNSIGNED NOT NULL,"
"PRIMARY KEY (id)"
");";
static const char* insertParticleQuery = "INSERT INTO %s ("
"id, datetime, ms, sensor, frame, particle, x, y, z, diam, circularity, dnr, effpsz, subx, suby, subw, subh"
")VALUES("
"NULL, '%s', %u, %u, %u, %u, " FLOAT_REPR ", " FLOAT_REPR ", " FLOAT_REPR ", " FLOAT_REPR ", " FLOAT_REPR ", %u, " FLOAT_REPR ", %u, %u, %u, %u"
");";
static const char* insertStatsQuery = "INSERT INTO %s ("
"id, datetime, lwc, mvd, conc, frames, particles"
")VALUES("
"NULL, '%s', " FLOAT_REPR ", " FLOAT_REPR ", " FLOAT_REPR ", %u, %u"
");";
static const char* selectParticlesQuery = "SELECT "
"id, datetime, ms, sensor, frame, particle, x, y, z, diam, circularity, dnr, effpsz, subx, suby, subw, subh "
"FROM %s WHERE id>=%u ORDER BY id ASC LIMIT " MAX_ROWS ";";

static Database* dbDefault = NULL;

Database::Database() :
	m_mysql(NULL) {}

Database::Database(const ConnectionInfo& connInfo, const DatabaseInfo& dbInfo)
{
	connect(connInfo);
	open(dbInfo);
}

Database::~Database()
{
	close();
}

void Database::exec(const char* fmt, va_list args)
{
	// Make sure we're still connected
	if (mysql_ping(m_mysql)) {
		close();
		connect(m_connInfo);
		open(m_dbInfo);
	}
	
	// Query
	mysql_query(m_mysql, vstrfmt(fmt, args).c_str());
	
	// Check for errors
	std::string err = mysql_error(m_mysql);
	if (!err.empty())
		throw std::runtime_error(strfmt("SQL error: %s", err.c_str()));
}

void Database::query(const char* fmt, ...)
{
	lock();
	
	va_list args;
	va_start(args, fmt);
	exec(fmt, args);
	va_end(args);
	
	unlock();
}

MYSQL_RES* Database::queryRes(const char* fmt, ...)
{
	lock();
	
	va_list args;
	va_start(args, fmt);
	exec(fmt, args);
	va_end(args);
	
	MYSQL_RES* res = mysql_store_result(m_mysql);
	
	unlock();
	return res;
}

bool Database::tableExists(const char* table)
{
	MYSQL_RES* res = NULL;
	if (!(res = queryRes("SHOW TABLES LIKE \"%s\"", table)))
		return false;
	bool ret = mysql_num_rows(res);
	mysql_free_result(res);
	return ret;
}

void Database::connect(const ConnectionInfo& connInfo)
{
	if (!(m_mysql = mysql_init(NULL)))
		throw std::runtime_error("Couldnt initialize SQL handle");
	m_mysql = mysql_real_connect(
		m_mysql,
		connInfo.host.c_str(),
		connInfo.user.c_str(), connInfo.passwd.c_str(),
		NULL, 
		connInfo.port,
		NULL, 0
	);
	if (!m_mysql)
		throw std::runtime_error("Couldnt connect to SQL server");
	
	m_connInfo = connInfo;
}

void Database::open(const DatabaseInfo& dbInfo)
{
	// Make sure that database exists
	if (mysql_select_db(m_mysql, dbInfo.name.c_str())) {
		query(createDBQuery, dbInfo.name.c_str());
		mysql_select_db(m_mysql, dbInfo.name.c_str());
	}
	
	// Make sure that tables exist
	if (!tableExists(dbInfo.particleTable.c_str()))
		query(createParticleTableQuery, dbInfo.particleTable.c_str());
	if (!tableExists(dbInfo.statsTable.c_str()))
		query(createStatsTableQuery, dbInfo.statsTable.c_str());
	
	m_dbInfo = dbInfo;
}

void Database::close()
{
	mysql_close(m_mysql);
	m_mysql = NULL;
}

void Database::writeParticle(const ParticleRow& row)
{
	query(
		insertParticleQuery, m_dbInfo.particleTable.c_str(),
		row.dt.str().c_str(), row.dt.MS,
		row.sensor, row.frame, row.particle,
		row.x, row.y, row.z,
		row.diam, row.circularity, row.dnr, row.effpsz,
		row.sub.x, row.sub.y, row.sub.width, row.sub.height
	);
}

void Database::writeStats(const StatsRow& row)
{
	query(
		insertStatsQuery, m_dbInfo.statsTable.c_str(),
		row.dt.str().c_str(),
		row.lwc, row.mvd, row.conc,
		row.frames, row.particles
	);
}

/* TODO: mysql_free_result() in case of failure */
void Database::readParticles(std::vector<ParticleRow>& rows, unsigned int minId)
{
	MYSQL_RES* res = queryRes(selectParticlesQuery, m_dbInfo.particleTable.c_str(), minId);
	int nrows = mysql_num_rows(res);
	for (int i = 0; i < nrows; i++) {
		MYSQL_ROW row = mysql_fetch_row(res);
		DateTime dt(row[1]);
		dt.MS = std::stoi(row[2]);
		rows.push_back({
			(unsigned int)std::stoi(row[0]), dt,
			(unsigned int)std::stoi(row[3]), (unsigned int)std::stoi(row[4]), (unsigned int)std::stoi(row[5]),
			std::stof(row[6]), std::stof(row[7]), std::stof(row[8]),
			std::stof(row[9]), std::stof(row[10]), (unsigned char)std::stoi(row[11]), std::stof(row[12]),
			cv::Rect(std::stoi(row[13]), std::stoi(row[14]), std::stoi(row[15]), std::stoi(row[16]))
		});
	}
	mysql_free_result(res);
}

Database* Database::getDefaultPtr()
{
	return dbDefault;
}

void Database::setDefaultPtr(Database* cfg)
{
	dbDefault = cfg;
}
