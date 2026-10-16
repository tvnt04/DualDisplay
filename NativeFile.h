#pragma once

#include <QFile>
#include <QString>
#include <cstdio>

#ifdef _WIN32
#include <cwchar>
#endif

inline FILE* openBinaryFile(const QString& path)
{
#ifdef _WIN32
    return _wfopen(reinterpret_cast<const wchar_t*>(path.utf16()), L"wb");
#else
    const QByteArray encodedPath = QFile::encodeName(path);
    return std::fopen(encodedPath.constData(), "wb");
#endif
}