#include "Bookmark.h"

Bookmark::Bookmark(const Id id, const std::string& name, const std::string& comment, const TimePoint& timeStamp, const BookmarkCategory& category)
	: m_id(id)
	, m_name(name)
	, m_comment(comment)
	, m_timeStamp(timeStamp)
	, m_category(category)
	, m_isValid(false)
{
}

Bookmark::~Bookmark()
{
}

Id Bookmark::getId() const
{
	return m_id;
}

void Bookmark::setId(const Id id)
{
	m_id = id;
}

std::string Bookmark::getName() const
{
	return m_name;
}

void Bookmark::setName(const std::string& name)
{
	m_name = name;
}

std::string Bookmark::getComment() const
{
	return m_comment;
}

void Bookmark::setComment(const std::string& comment)
{
	m_comment = comment;
}

TimePoint Bookmark::getTimeStamp() const
{
	return m_timeStamp;
}

void Bookmark::setTimeStamp(const TimePoint& timeStamp)
{
	m_timeStamp = timeStamp;
}

BookmarkCategory Bookmark::getCategory() const
{
	return m_category;
}

void Bookmark::setCategory(const BookmarkCategory& category)
{
	m_category = category;
}

bool Bookmark::isValid() const
{
	return m_isValid;
}

void Bookmark::setIsValid(const bool isValid)
{
	m_isValid = isValid;
}

