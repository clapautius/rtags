/* This file is part of RTags.

   RTags is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   RTags is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with RTags.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef Location_h
#define Location_h

#include <rct/String.h>
#include <rct/Log.h>
#include <rct/Path.h>
#include <rct/Serializer.h>
#include <assert.h>
#include <clang-c/Index.h>
#include <stdio.h>
#ifndef RTAGS_SINGLE_THREAD
#include <mutex>
#define LOCK() const std::lock_guard<std::mutex> lock(sMutex)
#else
#define LOCK() do {} while (0)
#endif

static inline int intCompare(uint32_t l, uint32_t r)
{
    if (l < r)
        return -1;
    if (l > r)
        return 1;
    return 0;
}
static inline int comparePosition(uint32_t lline, uint32_t lcol, uint32_t rline, uint32_t rcol)
{
    int ret = intCompare(lline, rline);
    if (!ret)
        ret = intCompare(lcol, rcol);
    return ret;
}
class Location
{
public:
    uint64_t mData;

    Location()
        : mData(0)
    {}

    Location(uint32_t fileId, uint32_t line, uint32_t col)
        : mData((static_cast<uint64_t>(fileId) << (64 - FileBits)) | (static_cast<uint64_t>(line) << (64 - FileBits - LineBits)) | col)
    {
    }

    static inline uint32_t fileId(const Path &path)
    {
        LOCK();
        return sPathsToIds.value(path);
    }
    static inline Path path(uint32_t id)
    {
        LOCK();
        return sIdsToPaths.value(id);
    }

    static inline uint32_t insertFile(const Path &path)
    {
        assert(!path.contains(".."));
        assert(path.resolved() == path);
        LOCK();
        uint32_t &id = sPathsToIds[path];
        if (!id) {
            id = ++sLastId;
            sIdsToPaths[id] = path;
        }
        return id;
    }

    inline uint32_t fileId() const { return ((mData & FILEID_MASK) >> (64 - FileBits)); }
    inline uint32_t line() const { return ((mData & LINE_MASK) >> (64 - FileBits - LineBits)); }
    inline uint32_t column() const { return static_cast<uint32_t>(mData & COLUMN_MASK); }

    inline Path path() const
    {
        if (mCachedPath.isEmpty()) {
            LOCK();
            mCachedPath = sIdsToPaths.value(fileId());
        }
        return mCachedPath;
    }
    inline bool isNull() const { return !mData; }
    inline bool isValid() const { return mData; }
    inline void clear() { mData = 0; mCachedPath.clear(); }
    inline bool operator==(const String &str) const
    {
        const Location fromPath = Location::fromPathLineAndColumn(str);
        return operator==(fromPath);
    }
    inline bool operator!=(const String &str) const
    {
        const Location fromPath = Location::fromPathLineAndColumn(str);
        return operator!=(fromPath);
    }
    inline bool operator==(const Location &other) const { return mData == other.mData; }
    inline bool operator!=(const Location &other) const { return mData != other.mData; }
    inline int compare(const Location &other) const
    {
        int ret = intCompare(fileId(), other.fileId());
        if (!ret) {
            ret = intCompare(line(), other.line());
            if (!ret)
                ret = intCompare(column(), other.column());
        }
        return ret;
    }
    inline bool operator<(const Location &other) const
    {
        return compare(other) < 0;
    }

    inline bool operator>(const Location &other) const
    {
        return compare(other) > 0;
    }

    String context() const;

    enum KeyFlag {
        NoFlag = 0x0,
        ShowContext = 0x1
    };

    String key(unsigned flags = NoFlag) const;
    static Location decode(const String &data)
    {
        uint32_t col;
        uint32_t line;
        memcpy(&col, data.constData() + data.size() - sizeof(col), sizeof(col));
        memcpy(&line, data.constData() + data.size() - sizeof(line) - sizeof(col), sizeof(line));
        const Path path(data.constData(), data.size() - sizeof(col) - sizeof(line));
        uint32_t fileId = Location::fileId(path);
        if (!fileId)
            fileId = Location::fileId(path.resolved());
        if (fileId)
            return Location(fileId, line, col);
        error("Failed to make location from [%s:%d:%d]", path.constData(), line, col);
        return Location();
    }
    static String encode(const String &key)
    {
        char path[PATH_MAX];
        uint32_t line, col;
        if (sscanf(key.constData(), "%[^':']:%d:%d", path, &line, &col) != 3)
            return String();

        Path resolved = Path::resolved(path, Path::MakeAbsolute);
        {
            char buf[8];
            memcpy(buf, &line, sizeof(line));
            memcpy(buf + 4, &col, sizeof(col));
            resolved.append(buf, 8);
        }

        return resolved;
    }

    static Location fromPathLineAndColumn(const String &str)
    {
        char path[PATH_MAX];
        uint32_t line, col;
        if (sscanf(str.constData(), "%[^':']:%d:%d", path, &line, &col) != 3)
            return Location();

        const Path resolved = Path::resolved(path);
        return Location(Location::insertFile(resolved), line, col);
    }
    static Hash<uint32_t, Path> idsToPaths()
    {
        LOCK();
        return sIdsToPaths;
    }
    static Hash<Path, uint32_t> pathsToIds()
    {
        LOCK();
        return sPathsToIds;
    }
    static void init(const Hash<Path, uint32_t> &pathsToIds)
    {
        LOCK();
        sPathsToIds = pathsToIds;
        sLastId = sPathsToIds.size();
        for (const auto &it : sPathsToIds) {
            sIdsToPaths[it.second] = it.first;
        }
    }

    static void init(const Hash<uint32_t, Path> &idsToPaths)
    {
        LOCK();
        sIdsToPaths = idsToPaths;
        sLastId = sIdsToPaths.size();
        for (const auto &it : sIdsToPaths) {
            sPathsToIds[it.second] = it.first;
        }
    }

    static void set(const Path &path, uint32_t fileId)
    {
        LOCK();
        sPathsToIds[path] = fileId;
        Path &p = sIdsToPaths[fileId];
        if (p.isEmpty())
            p = path;
    }
private:
    static std::mutex sMutex;
    static Hash<Path, uint32_t> sPathsToIds;
    static Hash<uint32_t, Path> sIdsToPaths;
    static uint32_t sLastId;
    mutable Path mCachedPath;
    enum {
        FileBits = 22,
        LineBits = 21,
        ColumnBits = 64 - FileBits - LineBits
    };
    static const uint64_t FILEID_MASK;
    static const uint64_t LINE_MASK;
    static const uint64_t COLUMN_MASK;
};

template <> inline int fixedSize(const Location &)
{
    return sizeof(uint64_t);
}

template <> inline Serializer &operator<<(Serializer &s, const Location &t)
{
    s.write(reinterpret_cast<const char*>(&t.mData), sizeof(uint64_t));
    return s;
}

template <> inline Deserializer &operator>>(Deserializer &s, Location &t)
{
    s.read(reinterpret_cast<char*>(&t), sizeof(uint64_t));
    return s;
}

static inline Log operator<<(Log dbg, const Location &loc)
{
    const String out = "Location(" + loc.key() + ")";
    return (dbg << out);
}

#endif
