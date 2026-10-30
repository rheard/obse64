#include "GameTypes.h"
#include <cstring>

GameStdString ToGameStdString(const char * text)
{
	GameStdString str = {};
	str.size = strlen(text);
	if(str.size < sizeof(str.buf))
	{
		memcpy(str.buf, text, str.size + 1);
		str.capacity = sizeof(str.buf) - 1;
	}
	else
	{
		str.ptr = text;
		str.capacity = str.size;
	}

	return str;
}
