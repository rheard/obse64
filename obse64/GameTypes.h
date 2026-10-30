#pragma once

#include "obse64_common/Types.h"

// base types/containers

// 10
template <typename T>
class BSSimpleList
{
public:
	struct Node
	{
		T		m_data;
		Node	* m_next;
	};

	Node node;
};

static_assert(sizeof(BSSimpleList<void *>) == 0x10);

// 10
class BSStringT
{
public:
	char	* m_data;	// 0
	u16		m_dataLen;	// 8
	u16		m_bufLen;	// A
};

static_assert(sizeof(BSStringT) == 0x10);

// 20
// the game's std::string, as the game reads one (MSVC's layout)
struct GameStdString
{
	union
	{
		char		buf[16];	// 00
		const char	* ptr;		// 00 once capacity passes 15
	};
	u64	size;		// 10
	u64	capacity;	// 18
};

static_assert(sizeof(GameStdString) == 0x20);

// text, as a std::string the game only reads; it points into text when longer than 15 characters
GameStdString ToGameStdString(const char * text);
