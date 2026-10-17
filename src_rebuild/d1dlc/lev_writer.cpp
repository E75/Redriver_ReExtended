#include "lev_writer.h"

#include <stdio.h>
#include <string.h>

void LevPadToSector(std::vector<char>& data)
{
	data.resize((data.size() + LEV_SECTOR_SIZE - 1) / LEV_SECTOR_SIZE * LEV_SECTOR_SIZE, 0);
}

static void Append(std::vector<char>& dst, const void* src, size_t size)
{
	const char* p = (const char*)src;
	dst.insert(dst.end(), p, p + size);
}

static void AppendInt(std::vector<char>& dst, int value)
{
	Append(dst, &value, sizeof(value));
}

void LevWriter::AddLump(int section, int type, const void* data, int size)
{
	LevBlob blob;
	blob.type = type;
	Append(blob.data, data, size);

	(section == LEV_SECTION_LOADTIME ? loadtime : inmem).push_back(std::move(blob));
}

// LUMP{sectionType,size} + 4-byte aligned sub-lumps + LUMP{255,0}, padded to a sector
static std::vector<char> BuildSection(int sectionType, const std::vector<LevBlob>& lumps)
{
	std::vector<char> body;

	for (const LevBlob& lump : lumps)
	{
		AppendInt(body, lump.type);
		AppendInt(body, (int)lump.data.size());
		Append(body, lump.data.data(), lump.data.size());
		body.resize((body.size() + 3) & ~3, 0);
	}

	AppendInt(body, LUMP_END);
	AppendInt(body, 0);

	std::vector<char> section;
	AppendInt(section, sectionType);
	AppendInt(section, (int)body.size());
	Append(section, body.data(), body.size());

	LevPadToSector(section);
	return section;
}

bool LevWriter::Build(std::vector<char>& out) const
{
	std::vector<char> loadtimeSection = BuildSection(LUMP_LOADTIME_DATA, loadtime);
	std::vector<char> inmemSection = BuildSection(LUMP_INMEMORY_DATA, inmem);

	if ((tpages.size() | spool.size() | tail.size()) % LEV_SECTOR_SIZE)
		return false;

	OUT_CITYLUMP_INFO info;
	info.loadtime_offset = LEV_SECTOR_SIZE;
	info.loadtime_size = (int)loadtimeSection.size();
	info.tpage_offset = info.loadtime_offset + info.loadtime_size;
	info.tpage_size = (int)tpages.size();
	info.inmem_offset = info.tpage_offset + info.tpage_size;
	info.inmem_size = (int)inmemSection.size();
	info.spooled_offset = info.inmem_offset + info.inmem_size;
	info.spooled_size = (int)spool.size();

	out.clear();
	AppendInt(out, LUMP_LUMPDESC);
	AppendInt(out, LEV_SECTOR_SIZE - (int)sizeof(LUMP));
	Append(out, &info, sizeof(info));

	if (headerFiller.empty())
	{
		for (size_t i = out.size(); i < LEV_SECTOR_SIZE; i += 4)
			Append(out, "DES!", 4);
	}
	else
		Append(out, headerFiller.data(), headerFiller.size());

	if (out.size() != LEV_SECTOR_SIZE)
		return false;

	Append(out, loadtimeSection.data(), loadtimeSection.size());
	Append(out, tpages.data(), tpages.size());
	Append(out, inmemSection.data(), inmemSection.size());
	Append(out, spool.data(), spool.size());
	Append(out, tail.data(), tail.size());

	return true;
}

bool LevWriter::Write(const char* path) const
{
	std::vector<char> data;
	if (!Build(data))
		return false;

	FILE* fp = fopen(path, "wb");
	if (!fp)
		return false;

	bool ok = fwrite(data.data(), 1, data.size(), fp) == data.size();
	fclose(fp);
	return ok;
}

bool LevRepack(const LevFile& src, LevWriter& out)
{
	if (src.GetFormat() != LEV_FORMAT_DRIVER2)
		return false;

	const OUT_CITYLUMP_INFO& li = src.GetLumpInfo();

	const int64_t fileSize = src.Size();

	if ((int64_t)li.tpage_offset + li.tpage_size > fileSize || (int64_t)li.spooled_offset + li.spooled_size > fileSize)
		return false;

	out = LevWriter();
	out.headerFiller.assign(src.Data(sizeof(LUMP) + sizeof(OUT_CITYLUMP_INFO)), src.Data(LEV_SECTOR_SIZE));

	for (const LevLump& lump : src.GetLumps())
		out.AddLump(lump.section, lump.type, src.Data(lump.offset), lump.size);

	out.tpages.assign(src.Data(li.tpage_offset), src.Data(li.tpage_offset + li.tpage_size));
	out.spool.assign(src.Data(li.spooled_offset), src.Data(li.spooled_offset + li.spooled_size));

	int tailStart = li.spooled_offset + li.spooled_size;
	out.tail.assign(src.Data(tailStart), src.Data(src.Size()));
	LevPadToSector(out.tail);

	return true;
}
