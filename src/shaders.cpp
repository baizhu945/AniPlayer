#include "shaders.h"

#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QStringList ShaderLibrary::efficiencyFiles()
{
    // Kazumi mpvAnime4KShadersLite. Both Restore passes run at native
    // resolution, then a medium 2x and a small 2x.
    return {
        QStringLiteral("Anime4K_Clamp_Highlights.glsl"),
        QStringLiteral("Anime4K_Restore_CNN_M.glsl"),
        QStringLiteral("Anime4K_Restore_CNN_S.glsl"),
        QStringLiteral("Anime4K_Upscale_CNN_x2_M.glsl"),
        QStringLiteral("Anime4K_AutoDownscalePre_x2.glsl"),
        QStringLiteral("Anime4K_AutoDownscalePre_x4.glsl"),
        QStringLiteral("Anime4K_Upscale_CNN_x2_S.glsl"),
    };
}

QStringList ShaderLibrary::qualityFiles()
{
    // Kazumi mpvAnime4KShaders, which is Anime4K Mode A (HQ).
    return {
        QStringLiteral("Anime4K_Clamp_Highlights.glsl"),
        QStringLiteral("Anime4K_Restore_CNN_VL.glsl"),
        QStringLiteral("Anime4K_Upscale_CNN_x2_VL.glsl"),
        QStringLiteral("Anime4K_AutoDownscalePre_x2.glsl"),
        QStringLiteral("Anime4K_AutoDownscalePre_x4.glsl"),
        QStringLiteral("Anime4K_Upscale_CNN_x2_M.glsl"),
    };
}

QStringList ShaderLibrary::filesFor(SuperResolution mode)
{
    switch (mode) {
    case SuperResolution::Efficiency:
        return efficiencyFiles();
    case SuperResolution::Quality:
        return qualityFiles();
    case SuperResolution::Off:
        break;
    }
    return {};
}

QString ShaderLibrary::directory()
{
    const QString env = qEnvironmentVariable("ANIPLAYER_SHADER_DIR");
    QStringList candidates;
    if (!env.isEmpty())
        candidates << env;

    const QString appDir = QCoreApplication::applicationDirPath();
    candidates << QDir(appDir).filePath(QStringLiteral("shaders"));
    candidates << QDir(appDir).filePath(QStringLiteral("../shaders"));
    candidates << QDir(appDir).filePath(QStringLiteral("../share/aniplayer/shaders"));

    QDir walk(appDir);
    for (int i = 0; i < 5; ++i) {
        candidates << walk.filePath(QStringLiteral("shaders"));
        candidates << walk.filePath(QStringLiteral("share/aniplayer/shaders"));
        if (!walk.cdUp())
            break;
    }

    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataDir : dataDirs)
        candidates << QDir(dataDir).filePath(QStringLiteral("aniplayer/shaders"));

    const QString marker = QStringLiteral("Anime4K_Clamp_Highlights.glsl");
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir(candidate).absolutePath();
        if (QFileInfo::exists(QDir(cleaned).filePath(marker)))
            return cleaned;
    }
    return {};
}

QString ShaderLibrary::joinPaths(const QString &directory, const QStringList &files)
{
    QStringList absolute;
    absolute.reserve(files.size());
    for (const QString &file : files)
        absolute << QDir(directory).filePath(file);
    return absolute.join(QLatin1Char(':'));
}

bool ShaderLibrary::allPresent(const QString &directory, const QStringList &files, QString *missing)
{
    for (const QString &file : files) {
        const QString path = QDir(directory).filePath(file);
        if (!QFileInfo::exists(path)) {
            if (missing)
                *missing = path;
            return false;
        }
    }
    return true;
}

QString ShaderLibrary::argumentFor(SuperResolution mode, QString *error)
{
    if (mode == SuperResolution::Off)
        return {};
    const QString dir = directory();
    if (dir.isEmpty()) {
        if (error)
            *error = ui("找不到 Anime4K 着色器目录", "Anime4K shader directory was not found");
        return {};
    }
    const QStringList files = filesFor(mode);
    QString missing;
    if (!allPresent(dir, files, &missing)) {
        if (error)
            *error = ui("缺少着色器文件：%1", "Missing shader file: %1").arg(missing);
        return {};
    }
    return joinPaths(dir, files);
}
