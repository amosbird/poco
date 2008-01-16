//
// SQLiteStatementImpl.cpp
//
// $Id: //poco/1.3/Data/SQLite/src/SQLiteStatementImpl.cpp#3 $
//
// Library: SQLite
// Package: SQLite
// Module:  SQLiteStatementImpl
//
// Copyright (c) 2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Data/SQLite/SQLiteStatementImpl.h"
#include "Poco/Data/SQLite/Utility.h"
#include "Poco/Data/SQLite/SQLiteException.h"
#include "Poco/String.h"
#include <cstdlib>
#include <cstring>
#include "sqlite3.h"


namespace Poco {
namespace Data {
namespace SQLite {


SQLiteStatementImpl::SQLiteStatementImpl(sqlite3* pDB):
	_pDB(pDB),
	_pStmt(0),
	_stepCalled(false),
	_nextResponse(0)
{
}


SQLiteStatementImpl::~SQLiteStatementImpl()
{
	clear();
}


void SQLiteStatementImpl::compileImpl()
{
	if (_pStmt) return;

	std::string statement(toString());
	if (statement.empty())
		throw InvalidSQLStatementException("Empty statements are illegal");

	sqlite3_stmt* pStmt = 0;
	const char* pSql = statement.c_str(); // The SQL to be executed
	int rc = SQLITE_OK;
	const char* pLeftover = 0;
	bool queryFound = false;

	while (rc == SQLITE_OK && !pStmt && !queryFound)
	{
		rc = sqlite3_prepare_v2(_pDB, pSql, -1, &pStmt, &pLeftover);
		if (rc != SQLITE_OK)
		{
			if (pStmt) 
			{
				sqlite3_finalize(pStmt);
			}
			pStmt = 0;
			std::string errMsg = sqlite3_errmsg(_pDB);
			Utility::throwException(rc, errMsg);
		}
		else if (rc == SQLITE_OK && pStmt)
		{
			queryFound = true;
		}
		else if(rc == SQLITE_OK && !pStmt) // comment/whitespace ignore
		{
			pSql = pLeftover;
			if (std::strlen(pSql) == 0)
			{
				// empty statement or an conditional statement! like CREATE IF NOT EXISTS
				// this is valid
				queryFound = true;
			}
		}
	}

	clear();
	_pStmt = pStmt;

	// prepare binding
	_pBinder = new Binder(_pStmt);
	_pExtractor = new Extractor(_pStmt);

	int colCount = sqlite3_column_count(_pStmt);

	for (int i = 0; i < colCount; ++i)
	{
		MetaColumn mc(i, sqlite3_column_name(_pStmt, i), Utility::getColumnType(_pStmt, i));
		_columns.push_back(mc);
	}
}


bool SQLiteStatementImpl::canBind() const
{
	bool ret = false;
	if (!bindings().empty() && _pStmt)
		ret = (*bindings().begin())->canBind();

	return ret;
}


void SQLiteStatementImpl::bindImpl()
{
	_stepCalled      = false;
	_nextResponse = 0;
	if (_pStmt == 0) return;

	sqlite3_reset(_pStmt);

	// bind
	Bindings& binds = bindings();
	int pc = sqlite3_bind_parameter_count(_pStmt);
	if (binds.empty() && 0 == pc) return;
	else if (binds.empty() && pc > 0)
		throw ParameterCountMismatchException();
	else if (!binds.empty() && binds.size() * (*binds.begin())->numOfColumnsHandled() != pc)
		throw ParameterCountMismatchException();

	std::size_t pos = 1; // sqlite starts with 1 not 0!

	Bindings::iterator it    = binds.begin();
	Bindings::iterator itEnd = binds.end();
	for (; it != itEnd && (*it)->canBind(); ++it)
	{
		(*it)->bind(pos);
		pos += (*it)->numOfColumnsHandled();
	}
}


void SQLiteStatementImpl::clear()
{
	_columns.clear();

	if (_pStmt)
	{
		sqlite3_finalize(_pStmt);
		_pStmt=0;
	}
}


bool SQLiteStatementImpl::hasNext()
{
	if (_stepCalled)
		return (_nextResponse == SQLITE_ROW);

	// _pStmt is allowed to be null for conditional SQL statements
	if (_pStmt == 0)
	{
		_stepCalled      = true;
		_nextResponse = SQLITE_DONE;
		return false;
	}

	_stepCalled      = true;
	_nextResponse = sqlite3_step(_pStmt);

	if (_nextResponse != SQLITE_ROW && _nextResponse != SQLITE_OK && _nextResponse != SQLITE_DONE)
	{
		Utility::throwException(_nextResponse);
	}

	return (_nextResponse == SQLITE_ROW);
}


void SQLiteStatementImpl::next()
{
	if (SQLITE_ROW == _nextResponse)
	{
		poco_assert (columnsReturned() == sqlite3_column_count(_pStmt));

		Extractions& extracts = extractions();
		Extractions::iterator it    = extracts.begin();
		Extractions::iterator itEnd = extracts.end();
		std::size_t pos = 0; // sqlite starts with pos 0 for results!
		for (; it != itEnd; ++it)
		{
			(*it)->extract(pos);
			pos += (*it)->numOfColumnsHandled();
		}
		_stepCalled = false;
	}
	else if (SQLITE_DONE == _nextResponse)
	{
		throw Poco::Data::DataException("No data received");
	}
	else
	{
		int rc = _nextResponse;
		Utility::throwException(rc, std::string("Iterator Error: trying to access the next value"));
	}
}


Poco::UInt32 SQLiteStatementImpl::columnsReturned() const
{
	return (Poco::UInt32) _columns.size();
}


const MetaColumn& SQLiteStatementImpl::metaColumn(Poco::UInt32 pos) const
{
	poco_assert (pos >= 0 && pos <= _columns.size());
	return _columns[pos];
}


} } } // namespace Poco::Data::SQLite
