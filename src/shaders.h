#pragma once

#include <QString>
#include <QStringList>

// Presets match Kazumi's mpvAnime4KShaders and mpvAnime4KShadersLite.
enum class SuperResolution {
    Off = 1,
    Efficiency = 2,
    Quality = 3,
};

class ShaderLibrary {
public:
    static QString directory();
    static QStringList efficiencyFiles();
    static QStringList qualityFiles();
    static QStringList filesFor(SuperResolution mode);
    // mpv path-list. Linux uses ':'.
    static QString joinPaths(const QString &directory, const QStringList &files);
    static QString argumentFor(SuperResolution mode, QString *error);
    static bool allPresent(const QString &directory, const QStringList &files, QString *missing);
};
