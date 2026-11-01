#include "i18n.h"
#include "mainwindow.h"
#include "selftest.h"
#include "shortcuts.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#include <clocale>

namespace {

void printHelp()
{
    const QString text = ui(
        "用法: aniplayer [文件...]\n"
        "       aniplayer --test\n"
        "\n"
        "AniPlayer 使用 libmpv 播放，Anime4K 超分辨率与 Kazumi 的效率档、质量档一致。\n"
        "媒体信息来自 ffprobe。播放、倍速、循环、音轨、字幕和超分辨率在底部控制条，右键画面也能打开。\n"
        "语言在顶部切换。快捷键保存在 ~/.config/AniPlayer/shortcuts.conf，也可在菜单「快捷键」里修改。\n",
        "Usage: aniplayer [files...]\n"
        "       aniplayer --test\n"
        "\n"
        "AniPlayer plays with libmpv. Anime4K efficiency and quality presets match Kazumi.\n"
        "Media details come from ffprobe. Playback, speed, loop, audio, subtitles, and super resolution\n"
        "are on the bottom bar and in the right-click menu. Switch language with the header button.\n"
        "Shortcuts are stored in ~/.config/AniPlayer/shortcuts.conf and can be edited from the Shortcuts menu.\n");
    fprintf(stdout, "%s", text.toUtf8().constData());
}

void prepareSelfTestConfig()
{
    const QString directory = QStringLiteral("/tmp/aniplayer-selftest");
    QDir(directory).removeRecursively();
    QDir().mkpath(directory);
    qputenv("ANIPLAYER_CONFIG_DIR", directory.toUtf8());
    QFile file(directory + QStringLiteral("/shortcuts.conf"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write("play-pause = K\nseek-forward = Ctrl+Right\n");
}

} // namespace

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSwapInterval(1);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    QSurfaceFormat::setDefaultFormat(format);

    bool testMode = false;
    bool help = false;
    QStringList files;
    for (int i = 1; i < argc; ++i) {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == QLatin1String("--test"))
            testMode = true;
        else if (argument == QLatin1String("--help") || argument == QLatin1String("-h"))
            help = true;
        else if (!argument.startsWith(QLatin1Char('-')))
            files << argument;
    }
    if (testMode)
        prepareSelfTestConfig();

    // libmpv rejects locales where the decimal separator is not '.'.
    setlocale(LC_NUMERIC, "C");
    QApplication application(argc, argv);
    setlocale(LC_NUMERIC, "C");
    QCoreApplication::setOrganizationName(QStringLiteral("AniPlayer"));
    QCoreApplication::setApplicationName(QStringLiteral("AniPlayer"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("AniPlayer"));
    QGuiApplication::setDesktopFileName(QStringLiteral("aniplayer"));
    if (QIcon::themeName().isEmpty())
        QIcon::setThemeName(QStringLiteral("breeze"));

    if (help) {
        printHelp();
        return 0;
    }

    MainWindow window(testMode);
    QApplication::setWindowIcon(window.windowIcon());
    window.show();
    if (!files.isEmpty() && !testMode)
        window.openPaths(files, false);

    if (!testMode)
        return application.exec();

    int code = 1;
    QTimer::singleShot(0, &window, [&window, &code] {
        code = runSelfTest(&window);
        QApplication::exit(code);
    });
    return application.exec();
}
