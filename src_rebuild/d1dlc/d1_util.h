#ifndef D1_UTIL_H
#define D1_UTIL_H

// Small helpers shared by the converter sources (logging, bounds checked reader, byte appenders).

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MakeFolder(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MakeFolder(path) mkdir(path, 0755)
#endif

inline void Log(std::string& log, const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);

	log += line;
	log += '\n';
}

// bounds checked little endian reader over a lump
class Reader
{
public:
	Reader(const char* data, int size) : m_data(data), m_size(size) {}

	template<typename T>
	T Get()
	{
		T value = T();
		Read(&value, sizeof(T));
		return value;
	}

	void Read(void* dst, int bytes)
	{
		if (bytes < 0 || m_pos + bytes > m_size)
		{
			m_ok = false;
			memset(dst, 0, bytes > 0 ? bytes : 0);
			return;
		}

		memcpy(dst, m_data + m_pos, bytes);
		m_pos += bytes;
	}

	void Skip(int bytes) { m_pos += bytes; if (m_pos > m_size || m_pos < 0) m_ok = false; }

	int				Pos() const { return m_pos; }
	bool			Ok() const { return m_ok; }
	const char*		Here() const { return m_data + m_pos; }

private:
	const char*	m_data;
	int			m_size;
	int			m_pos{ 0 };
	bool		m_ok{ true };
};

// Replaces the section [name] of an ini file by the lines in body (each ending in \n), everything else stays as it is
// (line endings included). The section is added at the end when it is missing.
inline void WriteIniSection(const char* path, const char* name, const std::string& body)
{
	std::string text;
	FILE* fp = fopen(path, "rb");
	if (fp)
	{
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
			text.append(buf, n);

		fclose(fp);
	}

	const char* eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	std::string header = std::string("[") + name + "]";

	size_t start = text.find(header);
	if (start != std::string::npos)
	{
		size_t end = text.find("\n[", start + 1);
		text.erase(start, end == std::string::npos ? std::string::npos : end + 1 - start);
	}

	while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		text.pop_back();

	if (!text.empty())
		text += std::string(eol) + eol;

	text += header + eol;
	for (char c : body)
	{
		if (c == '\n')
			text += eol;
		else
			text += c;
	}

	fp = fopen(path, "wb");
	if (fp)
	{
		fwrite(text.data(), 1, text.size(), fp);
		fclose(fp);
	}
}

inline void Put(std::vector<char>& dst, const void* src, size_t size)
{
	const char* p = (const char*)src;
	dst.insert(dst.end(), p, p + size);
}

template<typename T>
void Put(std::vector<char>& dst, const T& value)
{
	Put(dst, &value, sizeof(T));
}

#endif // D1_UTIL_H
