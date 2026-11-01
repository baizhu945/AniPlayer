#include "selftest.h"

#include "i18n.h"
#include "mainwindow.h"
#include "shortcuts.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <functional>

namespace {

int gFailures = 0;

void check(bool ok, const QString &message)
{
    fprintf(stderr, "%s: %s\n", ok ? "PASS" : "FAIL", message.toUtf8().constData());
    if (!ok)
        ++gFailures;
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        if (predicate())
            return true;
        QThread::msleep(20);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
    return predicate();
}

bool runProcess(const QString &program, const QStringList &arguments, QString *output, int timeoutMs)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(timeoutMs)) {
        process.kill();
        if (output)
            *output = QStringLiteral("timed out");
        return false;
    }
    if (output)
        *output = QString::fromUtf8(process.readAllStandardError() + process.readAllStandardOutput());
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool makeSample(const QString &path, const QString &source, int frequency)
{
    QString output;
    const bool ok = runProcess(
        QStringLiteral("ffmpeg"),
        {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
         QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("testsrc2=size=426x240:rate=24:duration=6"), QStringLiteral("-f"),
         QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("sine=frequency=%1:sample_rate=48000:duration=6").arg(frequency),
         QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
         QStringLiteral("yuv420p"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
         QStringLiteral("-crf"), QStringLiteral("18"), QStringLiteral("-c:a"), QStringLiteral("aac"),
         QStringLiteral("-b:a"), QStringLiteral("160k"), QStringLiteral("-shortest"), path},
        &output, 30000);
    if (!ok)
        fprintf(stderr, "ffmpeg %s: %s\n", qPrintable(path), qPrintable(output));
    Q_UNUSED(source);
    return ok && QFileInfo::exists(path);
}

QString psnrAverage(const QString &left, const QString &right)
{
    QString output;
    runProcess(QStringLiteral("ffmpeg"),
               {QStringLiteral("-hide_banner"), QStringLiteral("-i"), left, QStringLiteral("-i"), right,
                QStringLiteral("-lavfi"), QStringLiteral("psnr"), QStringLiteral("-f"), QStringLiteral("null"),
                QStringLiteral("-")},
               &output, 20000);
    const QRegularExpression pattern(QStringLiteral("average:([0-9inf.]+)"));
    const QRegularExpressionMatch match = pattern.match(output);
    return match.hasMatch() ? match.captured(1) : output;
}

bool shaderLogFailed(const QStringList &logs)
{
    for (const QString &line : logs) {
        const QString lower = line.toLower();
        if ((lower.contains(QLatin1String("shader")) || lower.contains(QLatin1String("glsl")))
            && (lower.contains(QLatin1String("error")) || lower.contains(QLatin1String("fail"))
                || lower.contains(QLatin1String("cannot")) || lower.contains(QLatin1String("invalid"))))
            return true;
    }
    return false;
}

QString dialogText(QWidget *dialog)
{
    QStringList parts;
    const auto labels = dialog->findChildren<QLabel *>();
    for (const QLabel *label : labels)
        parts << label->text();
    return parts.join(QStringLiteral("\n"));
}

} // namespace

int runSelfTest(MainWindow *window)
{
    window->setChromeAutohide(false);
    window->resize(960, 640);
    window->show();
    window->raise();
    window->activateWindow();

    auto *player = window->player();
    const bool ready = waitUntil([player] { return player->isReady(); }, 8000);
    check(ready, QStringLiteral("OpenGL render context is ready (%1)").arg(player->glDescription()));
    if (!ready) {
        fprintf(stderr, "render error: %s\n", qPrintable(player->lastError()));
        for (const QString &line : player->recentLogs())
            fprintf(stderr, "log: %s\n", qPrintable(line));
        return 1;
    }

    auto *playAction = window->findChild<QAction *>(QStringLiteral("actionPlayPause"));
    auto *seekAction = window->findChild<QAction *>(QStringLiteral("actionSeekForward"));
    auto *nextAction = window->findChild<QAction *>(QStringLiteral("actionNext"));
    check(playAction && playAction->shortcut().toString(QKeySequence::PortableText) == QLatin1String("K"),
          QStringLiteral("startup reads play-pause = K from shortcuts.conf (got %1)")
              .arg(playAction ? playAction->shortcut().toString(QKeySequence::PortableText) : QStringLiteral("<missing>")));
    check(seekAction && seekAction->shortcut().toString(QKeySequence::PortableText) == QLatin1String("Ctrl+Right"),
          QStringLiteral("startup reads seek-forward from shortcuts.conf"));
    check(nextAction && nextAction->shortcut().toString(QKeySequence::PortableText) == QLatin1String("N"),
          QStringLiteral("missing shortcut keys fall back to the default"));

    I18n::instance().setLanguage(I18n::Language::English);
    auto *mediaAction = window->findChild<QAction *>(QStringLiteral("actionMediaInfo"));
    auto *languageButton = window->findChild<QAbstractButton *>(QStringLiteral("languageButton"));
    auto *emptyHint = window->findChild<QLabel *>(QStringLiteral("emptyHint"));
    auto *shortcutAction = window->findChild<QAction *>(QStringLiteral("actionShortcuts"));
    check(mediaAction && mediaAction->text() == QLatin1String("Media Info"),
          QStringLiteral("English translation of the media-info action"));
    check(languageButton && languageButton->text() == QLatin1String("EN"),
          QStringLiteral("header language button shows EN"));
    check(emptyHint && emptyHint->text().contains(QLatin1String("Drop")),
          QStringLiteral("English empty-state hint"));
    check(shortcutAction && shortcutAction->text().contains(QLatin1String("Shortcut")),
          QStringLiteral("English shortcuts menu item"));

    I18n::instance().setLanguage(I18n::Language::Chinese);
    check(mediaAction && mediaAction->text() == QStringLiteral("媒体信息"),
          QStringLiteral("Chinese translation of the media-info action"));
    check(languageButton && languageButton->text() == QStringLiteral("中文"),
          QStringLiteral("header language button shows 中文"));
    check(shortcutAction && shortcutAction->text().contains(QStringLiteral("快捷键")),
          QStringLiteral("Chinese shortcuts menu item"));

    auto *openButton = window->findChild<QAbstractButton *>(QStringLiteral("openButton"));
    auto *srButton = window->findChild<QAbstractButton *>(QStringLiteral("srButton"));
    auto *speedButton = window->findChild<QAbstractButton *>(QStringLiteral("speedButton"));
    auto *loopButton = window->findChild<QAbstractButton *>(QStringLiteral("loopButton"));
    auto *audioButton = window->findChild<QAbstractButton *>(QStringLiteral("audioButton"));
    auto *subtitleButton = window->findChild<QAbstractButton *>(QStringLiteral("subtitleButton"));
    check(openButton && srButton && speedButton && loopButton && audioButton && subtitleButton,
          QStringLiteral("open, super-resolution, speed, loop, audio, and subtitles are toolbar buttons"));
    auto *appMenu = window->findChild<QMenu *>(QStringLiteral("mainMenu"));
    bool playBuriedInAppMenu = false;
    if (appMenu) {
        for (QAction *action : appMenu->actions()) {
            if (action->objectName() == QLatin1String("actionPlayPause"))
                playBuriedInAppMenu = true;
        }
    }
    check(appMenu && !playBuriedInAppMenu, QStringLiteral("playback is not buried in the app menu"));

    auto *srMenu = window->findChild<QMenu *>(QStringLiteral("srMenu"));
    auto *speedMenu = window->findChild<QMenu *>(QStringLiteral("speedMenu"));
    const int srCount = srMenu ? srMenu->actions().size() : 0;
    const int speedCount = speedMenu ? speedMenu->actions().size() : 0;
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, QPoint(24, 24),
                                   player->mapToGlobal(QPoint(24, 24)));
    QCoreApplication::sendEvent(player, &contextEvent);
    auto *popup = window->findChild<QMenu *>(QStringLiteral("contextMenu"));
    const int popupCount = popup ? popup->actions().size() : 0;
    bool hasPlayback = false;
    bool hasOpen = false;
    bool hasSuperResolution = false;
    bool hasSpeed = false;
    bool hasSubtitles = false;
    if (popup) {
        for (QAction *action : popup->actions()) {
            if (action->objectName() == QLatin1String("actionPlayPause"))
                hasPlayback = true;
            if (action->objectName() == QLatin1String("actionOpen"))
                hasOpen = true;
            if (QMenu *child = action->menu()) {
                if (child->title().contains(QStringLiteral("超分辨率")))
                    hasSuperResolution = true;
                if (child->title().contains(QStringLiteral("倍速")))
                    hasSpeed = true;
                if (child->title().contains(QStringLiteral("字幕")))
                    hasSubtitles = true;
            }
        }
    }
    check(popupCount >= 16 && hasPlayback && hasOpen && hasSuperResolution && hasSpeed && hasSubtitles,
          QStringLiteral("right-click menu exposes playback, open, super-resolution, speed, and subtitles (%1)")
              .arg(popupCount));
    check(srMenu && srMenu->actions().size() == srCount && srCount == 3,
          QStringLiteral("right-click menu keeps the super-resolution entries in place"));
    check(speedMenu && speedMenu->actions().size() == speedCount && speedCount >= 6,
          QStringLiteral("right-click menu keeps the speed entries in place"));
    if (popup)
        popup->close();
    QCoreApplication::processEvents();

    if (openButton) {
        const QPointF local(8, 8);
        QMouseEvent press(QEvent::MouseButtonPress, local, openButton->mapToGlobal(local.toPoint()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(openButton, &press);
        bool ripple = false;
        const auto children = openButton->findChildren<QWidget *>(Qt::FindDirectChildrenOnly);
        for (QWidget *child : children) {
            if (child->testAttribute(Qt::WA_TransparentForMouseEvents))
                ripple = true;
        }
        check(ripple, QStringLiteral("a click leaves a short confirmation mark on the button"));
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(-40, -40),
                            openButton->mapToGlobal(QPoint(-40, -40)), Qt::LeftButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(openButton, &release);
    }

    const QString mediaDir = QStringLiteral("/tmp/aniplayer-selftest-media");
    QDir(mediaDir).removeRecursively();
    QDir().mkpath(mediaDir);
    const QString first = mediaDir + QStringLiteral("/a.mp4");
    const QString second = mediaDir + QStringLiteral("/b.mp4");
    check(makeSample(first, QStringLiteral("testsrc2"), 440) && makeSample(second, QStringLiteral("testsrc2"), 880),
          QStringLiteral("ffmpeg created two sample videos"));
    if (!QFileInfo::exists(first))
        return 1;

    window->openPaths({first, second}, false);
    check(waitUntil([player] { return player->position() > 0.2 && player->duration() > 1.0; }, 8000),
          QStringLiteral("playback position advances (pos=%1 dur=%2)")
              .arg(player->position())
              .arg(player->duration()));
    check(player->playlist().size() == 2, QStringLiteral("playlist contains both files"));

    auto *playButton = window->findChild<QPushButton *>(QStringLiteral("playPauseButton"));
    if (playButton)
        playButton->click();
    check(waitUntil([player] { return player->paused(); }, 2000), QStringLiteral("pause button pauses playback"));

    player->seekAbsolute(1.0);
    check(waitUntil([player] { return std::abs(player->position() - 1.0) < 0.45; }, 3000),
          QStringLiteral("exact seek lands near 1s (pos=%1)").arg(player->position()));

    player->setVolume(30);
    check(waitUntil([player] { return std::abs(player->volume() - 30.0) < 0.5; }, 1500),
          QStringLiteral("volume property follows setVolume"));
    player->setMuted(true);
    check(waitUntil([player] { return player->muted(); }, 1500), QStringLiteral("mute property follows setMuted"));
    player->setSpeed(1.5);
    check(waitUntil([player] { return std::abs(player->speed() - 1.5) < 0.05; }, 1500),
          QStringLiteral("playback speed follows setSpeed"));

    const QString stillPng = mediaDir + QStringLiteral("/still.png");
    const QString stillVideo = mediaDir + QStringLiteral("/still.mp4");
    const QString offShot = mediaDir + QStringLiteral("/off.png");
    const QString qualityShot = mediaDir + QStringLiteral("/quality.png");
    QString ffmpegOutput;
    check(runProcess(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                      QStringLiteral("-i"), QStringLiteral("testsrc2=size=426x240:rate=1:duration=1"),
                      QStringLiteral("-frames:v"), QStringLiteral("1"), stillPng},
                     &ffmpegOutput, 20000)
              && runProcess(QStringLiteral("ffmpeg"),
                            {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                             QStringLiteral("error"), QStringLiteral("-loop"), QStringLiteral("1"),
                             QStringLiteral("-i"), stillPng, QStringLiteral("-t"), QStringLiteral("2"),
                             QStringLiteral("-r"), QStringLiteral("24"), QStringLiteral("-c:v"),
                             QStringLiteral("libx264"), QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                             stillVideo},
                            &ffmpegOutput, 20000),
          QStringLiteral("created a still video so shader output can be compared at one timestamp"));
    window->openPaths({stillVideo}, false);
    check(waitUntil([player] { return player->position() > 0.05 && player->duration() > 0.5; }, 5000),
          QStringLiteral("still video is playing"));
    auto saveDisplayedFrame = [&](const QString &path) {
        player->update();
        return waitUntil([player, &path] {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 30);
            const QImage frame = player->grabFramebuffer();
            return !frame.isNull() && frame.width() > 200 && frame.save(path);
        }, 2000);
    };
    player->setPaused(true);
    player->seekAbsolute(1.0);
    check(saveDisplayedFrame(offShot), QStringLiteral("captured the unshaded OpenGL frame"));

    const int logMark = player->recentLogs().size();
    window->setSuperResolution(SuperResolution::Quality);
    const bool qualitySet = waitUntil([player] {
        return player->shaderProperty().contains(QLatin1String("Anime4K_Restore_CNN_VL.glsl"))
               && player->shaderProperty().contains(QLatin1String("Anime4K_Upscale_CNN_x2_VL.glsl"));
    }, 5000);
    check(qualitySet, QStringLiteral("quality preset installs the Kazumi Mode A chain (%1)")
                          .arg(player->shaderProperty()));
    check(!shaderLogFailed(player->recentLogs().mid(logMark)),
          QStringLiteral("quality shaders compile without mpv errors"));
    // A paused libmpv screenshot can reuse the pre-shader image. Play a new
    // frame, then return to the same timestamp so only the shader pass differs.
    player->setPaused(false);
    waitUntil([player] { return player->position() > 1.3; }, 2000);
    player->setPaused(true);
    player->seekAbsolute(1.0);
    check(saveDisplayedFrame(qualityShot), QStringLiteral("captured the shaded OpenGL frame"));
    const QString average = psnrAverage(offShot, qualityShot);
    bool psnrOk = false;
    if (average != QLatin1String("inf")) {
        bool numeric = false;
        const double value = average.toDouble(&numeric);
        psnrOk = numeric && value < 60.0;
    }
    check(psnrOk, QStringLiteral("quality shaders change the picture (PSNR average=%1)").arg(average));

    window->setSuperResolution(SuperResolution::Efficiency);
    check(waitUntil([player] {
              const QString shaders = player->shaderProperty();
              return shaders.contains(QLatin1String("Anime4K_Restore_CNN_S.glsl"))
                     && shaders.contains(QLatin1String("Anime4K_Upscale_CNN_x2_S.glsl"))
                     && !shaders.contains(QLatin1String("Anime4K_Restore_CNN_VL.glsl"));
          }, 4000),
          QStringLiteral("efficiency preset matches Kazumi's light chain"));
    check(!shaderLogFailed(player->recentLogs()), QStringLiteral("efficiency shaders compile"));
    window->setSuperResolution(SuperResolution::Off);
    check(waitUntil([player] { return player->shaderProperty().trimmed().isEmpty(); }, 3000),
          QStringLiteral("turning super resolution off clears glsl-shaders"));

    window->openPaths({first, second}, false);
    check(waitUntil([player] { return player->path().contains(QStringLiteral("a.mp4")) && player->playlist().size() == 2; },
                    5000),
          QStringLiteral("reopened the two sample videos for playlist and media-info checks"));
    player->playNext();
    check(waitUntil([player, &second] { return player->path().contains(QStringLiteral("b.mp4")); }, 4000),
          QStringLiteral("playlist next plays the second file"));
    player->playPrevious();
    check(waitUntil([player] { return player->path().contains(QStringLiteral("a.mp4")); }, 4000),
          QStringLiteral("playlist previous returns to the first file"));

    player->setHwdecEnabled(false);
    check(waitUntil([player] { return player->hwdec() == QLatin1String("no"); }, 1500),
          QStringLiteral("hardware decoding can be turned off"));
    player->setHwdecEnabled(true);
    check(waitUntil([player] { return player->hwdec().contains(QLatin1String("auto")); }, 1500),
          QStringLiteral("hardware decoding returns to auto-copy"));

    player->setLoopMode(QStringLiteral("file"));
    check(player->loopMode() == QLatin1String("file"), QStringLiteral("loop-file mode"));
    player->setLoopMode(QStringLiteral("playlist"));
    check(player->loopMode() == QLatin1String("playlist"), QStringLiteral("loop-playlist mode"));
    player->setLoopMode(QStringLiteral("off"));
    check(player->loopMode() == QLatin1String("off"), QStringLiteral("loop off"));

    window->showFullScreen();
    check(waitUntil([window] { return window->isFullScreen(); }, 1500), QStringLiteral("enter fullscreen"));
    window->showNormal();
    check(waitUntil([window] { return !window->isFullScreen(); }, 1500), QStringLiteral("leave fullscreen"));

    I18n::instance().setLanguage(I18n::Language::English);
    mediaAction->trigger();
    auto *infoDialog = window->findChild<QDialog *>(QStringLiteral("mediaInfoDialog"));
    check(infoDialog && infoDialog->isVisible(), QStringLiteral("media info dialog opens from the header action"));
    const bool englishInfo = waitUntil([infoDialog] {
        const QString text = dialogText(infoDialog);
        return text.contains(QLatin1String("Video stream")) && text.contains(QLatin1String("Audio stream"))
               && text.contains(QLatin1String("h264")) && text.contains(QLatin1String("aac"))
               && text.contains(QLatin1String("426"));
    }, 5000);
    check(englishInfo, QStringLiteral("ffprobe details in English include video, audio, h264, aac, and 426"));
    if (englishInfo && infoDialog) {
        auto *page = infoDialog->findChild<QWidget *>(QStringLiteral("infoPage"));
        auto *value = infoDialog->findChild<QLabel *>(QStringLiteral("fieldValue"));
        QImage pageImage = page ? page->grab().toImage() : QImage();
        QImage valueImage = value ? value->grab().toImage() : QImage();
        const QColor background = pageImage.isNull() ? QColor() : pageImage.pixelColor(8, 8);
        int lightPixels = 0;
        for (int y = 0; y < valueImage.height(); y += 2) {
            for (int x = 0; x < valueImage.width(); x += 2) {
                if (valueImage.pixelColor(x, y).lightness() > 160)
                    ++lightPixels;
            }
        }
        check(background.isValid() && background.lightness() < 90,
              QStringLiteral("media info background is dark (lightness %1)").arg(background.lightness()));
        check(lightPixels > 0,
              QStringLiteral("media info values are light on the dark background (light pixels %1)")
                  .arg(lightPixels));
    }
    I18n::instance().setLanguage(I18n::Language::Chinese);
    const bool chineseInfo = waitUntil([infoDialog] {
        const QString text = dialogText(infoDialog);
        return text.contains(QStringLiteral("视频流")) && text.contains(QStringLiteral("音频流"));
    }, 5000);
    check(chineseInfo, QStringLiteral("ffprobe details switch to Chinese while the dialog stays open"));
    if (infoDialog)
        infoDialog->close();

    QTimer::singleShot(0, window, [] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        if (auto *edit = dialog->findChild<QLineEdit *>(QStringLiteral("shortcutEdit:playlist")))
            edit->setText(QStringLiteral("Ctrl+Shift+L"));
        if (auto *save = dialog->findChild<QPushButton *>(QStringLiteral("shortcutSave")))
            save->click();
    });
    shortcutAction->trigger();
    auto *playlistAction = window->findChild<QAction *>(QStringLiteral("actionPlaylist"));
    check(playlistAction
              && playlistAction->shortcut().toString(QKeySequence::PortableText) == QLatin1String("Ctrl+Shift+L"),
          QStringLiteral("shortcut dialog saves and applies playlist = Ctrl+Shift+L"));
    QFile shortcutFile(ShortcutStore::filePath());
    QString saved;
    if (shortcutFile.open(QIODevice::ReadOnly))
        saved = QString::fromUtf8(shortcutFile.readAll());
    check(saved.contains(QLatin1String("playlist = Ctrl+Shift+L"))
              && saved.contains(QLatin1String("play-pause = K")),
          QStringLiteral("shortcuts.conf keeps the edited binding and the original play-pause key"));

    const QString resumeFile = mediaDir + QStringLiteral("/resume.mp4");
    QString resumeOutput;
    check(runProcess(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                      QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                      QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x180:rate=24:duration=15"),
                      QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                      QStringLiteral("yuv420p"), QStringLiteral("-preset"), QStringLiteral("veryfast"),
                      QStringLiteral("-an"), resumeFile},
                     &resumeOutput, 30000),
          QStringLiteral("ffmpeg created a longer clip for resume"));
    player->setResumeEnabled(true);
    player->setPaused(false);
    window->openPaths({resumeFile}, false);
    check(waitUntil([player] { return player->duration() > 10.0 && player->path().contains(QStringLiteral("resume.mp4")); }, 8000),
          QStringLiteral("resume clip is open (dur=%1 path=%2)")
              .arg(player->duration())
              .arg(player->path()));
    player->setPaused(true);
    player->seekAbsolute(8.0);
    check(waitUntil([player] { return std::abs(player->position() - 8.0) < 0.8; }, 3000),
          QStringLiteral("resume clip seeks to 8s (pos=%1)").arg(player->position()));
    window->openPaths({second}, false);
    check(waitUntil([player] { return player->path().contains(QStringLiteral("b.mp4")); }, 4000),
          QStringLiteral("leaving the resume clip stores its position"));
    window->openPaths({resumeFile}, false);
    auto *osd = window->findChild<QLabel *>(QStringLiteral("osdLabel"));
    const bool resumed = waitUntil([player, osd] {
        return std::abs(player->position() - 8.0) < 1.5 && osd && osd->isVisible()
               && osd->text().contains(QStringLiteral("继续"));
    }, 5000);
    check(resumed, QStringLiteral("reopening the clip resumes near 8s (pos=%1 osd=%2)")
                       .arg(player->position())
                       .arg(osd ? osd->text() : QString()));
    player->setResumeEnabled(false);

    window->openUrl(QUrl::fromLocalFile(first).toString());
    check(waitUntil([player] { return player->path().contains(QStringLiteral("a.mp4")); }, 4000),
          QStringLiteral("open URL accepts a file:// media address"));

    window->openPaths({QStringLiteral("/tmp/aniplayer-does-not-exist.mp4")}, false);
    check(waitUntil([player] {
              return player->lastError().contains(QStringLiteral("无法播放"))
                     || player->lastError().contains(QLatin1String("Cannot play"))
                     || player->recentLogs().join(QStringLiteral("\n")).contains(QLatin1String("error"));
          }, 4000),
          QStringLiteral("a missing file reports a playback error"));

    fprintf(stderr, gFailures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECKS FAILED\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
