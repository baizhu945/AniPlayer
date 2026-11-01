#include "mpvwidget.h"

#include "i18n.h"
#include "shortcuts.h"

#include <locale.h>

#include <mpv/render.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Qt::StringLiterals;

namespace {

QString nodeString(const mpv_node &node)
{
    if (node.format == MPV_FORMAT_STRING && node.u.string)
        return QString::fromUtf8(node.u.string);
    if (node.format == MPV_FORMAT_INT64)
        return QString::number(static_cast<qlonglong>(node.u.int64));
    if (node.format == MPV_FORMAT_FLAG)
        return node.u.flag ? QStringLiteral("yes") : QStringLiteral("no");
    if (node.format == MPV_FORMAT_DOUBLE)
        return QString::number(node.u.double_);
    return {};
}

const mpv_node *mapValue(const mpv_node &map, const char *key)
{
    if (map.format != MPV_FORMAT_NODE_MAP || !map.u.list)
        return nullptr;
    const mpv_node_list *list = map.u.list;
    for (int i = 0; i < list->num; ++i) {
        if (list->keys && list->keys[i] && std::strcmp(list->keys[i], key) == 0)
            return &list->values[i];
    }
    return nullptr;
}

QString mapString(const mpv_node &map, const char *key)
{
    const mpv_node *value = mapValue(map, key);
    return value ? nodeString(*value) : QString();
}

qint64 mapInt(const mpv_node &map, const char *key)
{
    const mpv_node *value = mapValue(map, key);
    if (!value)
        return 0;
    if (value->format == MPV_FORMAT_INT64)
        return value->u.int64;
    if (value->format == MPV_FORMAT_FLAG)
        return value->u.flag;
    if (value->format == MPV_FORMAT_DOUBLE)
        return static_cast<qint64>(value->u.double_);
    return 0;
}

bool mapFlag(const mpv_node &map, const char *key)
{
    const mpv_node *value = mapValue(map, key);
    if (!value)
        return false;
    if (value->format == MPV_FORMAT_FLAG)
        return value->u.flag != 0;
    if (value->format == MPV_FORMAT_STRING && value->u.string)
        return std::strcmp(value->u.string, "yes") == 0;
    return false;
}

} // namespace

namespace {

// mpv refuses to start unless LC_NUMERIC uses a '.' decimal point. Qt installs
// a per-thread locale from the desktop language, which hides a later setlocale().
void forceCNumericLocale()
{
    locale_t threadLocale = uselocale(LC_GLOBAL_LOCALE);
    setlocale(LC_NUMERIC, "C");
    if (threadLocale && threadLocale != LC_GLOBAL_LOCALE) {
        locale_t fixed = newlocale(LC_NUMERIC_MASK, "C", threadLocale);
        if (fixed)
            uselocale(fixed);
    }
}

} // namespace

MpvWidget::MpvWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    forceCNumericLocale();
    mpv_ = mpv_create();
    if (!mpv_) {
        lastError_ = ui("mpv_create 失败", "mpv_create failed");
        return;
    }

    auto opt = [this](const char *name, const char *value) {
        mpv_set_option_string(mpv_, name, value);
    };
    // Ignore the user's mpv.conf so a forced vo= cannot break embedding.
    opt("config", "no");
    opt("load-scripts", "no");
    opt("terminal", "no");
    opt("input-default-bindings", "no");
    opt("input-vo-keyboard", "no");
    opt("osc", "no");
    opt("osd-level", "0");
    opt("vo", "libmpv");
    opt("gpu-api", "opengl");
    opt("hwdec", "auto-copy");
    opt("vd-lavc-dr", "no");
    opt("idle", "yes");
    opt("keep-open", "yes");
    opt("force-window", "no");
    opt("cursor-autohide", "no");
    opt("ytdl", "no");
    opt("audio-client-name", "aniplayer");
    opt("hr-seek", "yes");
    opt("screenshot-format", "png");
    opt("msg-level", "all=warn");
    // Same mechanism as mpv --save-position-on-quit. Only the start position is
    // stored, under AniPlayer's config directory rather than mpv's own.
    const QString watchDir = ShortcutStore::directory() + QStringLiteral("/watch_later");
    QDir().mkpath(watchDir);
    const QByteArray watchDirUtf8 = watchDir.toUtf8();
    opt("watch-later-dir", watchDirUtf8.constData());
    opt("watch-later-options", "start");
    opt("save-position-on-quit", "no");
    opt("resume-playback", "no");

    mpv_set_wakeup_callback(mpv_, &MpvWidget::onWakeup, this);
    const int rc = mpv_initialize(mpv_);
    if (rc < 0) {
        lastError_ = ui("mpv_initialize 失败：%1", "mpv_initialize failed: %1")
                         .arg(QString::fromUtf8(mpv_error_string(rc)));
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        return;
    }
    mpv_request_log_messages(mpv_, "warn");
    observeProperties();
}

MpvWidget::~MpvWidget()
{
    if (mpvGl_)
        mpv_render_context_set_update_callback(mpvGl_, nullptr, nullptr);
    if (mpv_)
        mpv_set_wakeup_callback(mpv_, nullptr, nullptr);

    if (context())
        makeCurrent();
    if (mpvGl_) {
        mpv_render_context_free(mpvGl_);
        mpvGl_ = nullptr;
    }
    if (mpv_) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
    }
    if (context())
        doneCurrent();
}

void MpvWidget::observeProperties()
{
    auto watch = [this](const char *name, mpv_format format) {
        mpv_observe_property(mpv_, 0, name, format);
    };
    watch("time-pos", MPV_FORMAT_DOUBLE);
    watch("duration", MPV_FORMAT_DOUBLE);
    watch("pause", MPV_FORMAT_FLAG);
    watch("volume", MPV_FORMAT_DOUBLE);
    watch("mute", MPV_FORMAT_FLAG);
    watch("speed", MPV_FORMAT_DOUBLE);
    watch("media-title", MPV_FORMAT_STRING);
    watch("path", MPV_FORMAT_STRING);
    watch("paused-for-cache", MPV_FORMAT_FLAG);
    watch("playlist-pos", MPV_FORMAT_INT64);
}

void MpvWidget::initializeGL()
{
    if (!mpv_) {
        emit renderFailed(lastError_.isEmpty() ? ui("mpv 未创建", "mpv was not created") : lastError_);
        return;
    }

    const QSurfaceFormat format = context()->format();
    glDescription_ = QStringLiteral("OpenGL %1.%2 %3, renderable=%4")
                         .arg(format.majorVersion())
                         .arg(format.minorVersion())
                         .arg(format.profile() == QSurfaceFormat::CoreProfile ? QStringLiteral("core")
                                                                              : QStringLiteral("compat"))
                         .arg(format.renderableType() == QSurfaceFormat::OpenGL ? QStringLiteral("gl")
                                                                                : QStringLiteral("gles"));

    mpv_opengl_init_params glInit{};
    glInit.get_proc_address = &MpvWidget::glProcAddress;
    glInit.get_proc_address_ctx = this;

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    const int rc = mpv_render_context_create(&mpvGl_, mpv_, params);
    if (rc < 0) {
        lastError_ = ui("创建 mpv 渲染上下文失败：%1（%2）", "Failed to create the mpv render context: %1 (%2)")
                         .arg(QString::fromUtf8(mpv_error_string(rc)), glDescription_);
        emit renderFailed(lastError_);
        return;
    }
    mpv_render_context_set_update_callback(mpvGl_, &MpvWidget::onRenderUpdate, this);
    applyPendingShaders();
    emit renderReady();
}

void MpvWidget::paintGL()
{
    if (!mpvGl_)
        return;

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{
        static_cast<int>(defaultFramebufferObject()),
        static_cast<int>(width() * dpr),
        static_cast<int>(height() * dpr),
        0,
    };
    int flipY = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(mpvGl_, params);
}

void *MpvWidget::glProcAddress(void *, const char *name)
{
    QOpenGLContext *gl = QOpenGLContext::currentContext();
    if (!gl)
        return nullptr;
    return reinterpret_cast<void *>(gl->getProcAddress(name));
}

void MpvWidget::onWakeup(void *ctx)
{
    auto *self = static_cast<MpvWidget *>(ctx);
    QMetaObject::invokeMethod(self, "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::onRenderUpdate(void *ctx)
{
    auto *self = static_cast<MpvWidget *>(ctx);
    QMetaObject::invokeMethod(self, "update", Qt::QueuedConnection);
}

void MpvWidget::onMpvEvents()
{
    if (!mpv_ || drainingEvents_)
        return;
    drainingEvents_ = true;
    while (true) {
        mpv_event *event = mpv_wait_event(mpv_, 0);
        if (!event || event->event_id == MPV_EVENT_NONE)
            break;
        handleEvent(event);
    }
    drainingEvents_ = false;
}

void MpvWidget::handleEvent(mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto *prop = static_cast<mpv_event_property *>(event->data);
        if (!prop || !prop->name)
            break;
        const QLatin1String name(prop->name);
        if (name == "time-pos"_L1) {
            position_ = (prop->format == MPV_FORMAT_DOUBLE && prop->data)
                            ? *static_cast<double *>(prop->data)
                            : 0;
            emit positionChanged(position_);
        } else if (name == "duration"_L1) {
            duration_ = (prop->format == MPV_FORMAT_DOUBLE && prop->data)
                            ? *static_cast<double *>(prop->data)
                            : 0;
            emit durationChanged(duration_);
        } else         if (name == "pause"_L1 && prop->format == MPV_FORMAT_FLAG && prop->data) {
            paused_ = *static_cast<int *>(prop->data) != 0;
            emit pauseChanged(paused_);
        } else if (name == "volume"_L1 && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            volume_ = *static_cast<double *>(prop->data);
            emit volumeChanged(volume_);
        } else if (name == "mute"_L1 && prop->format == MPV_FORMAT_FLAG && prop->data) {
            muted_ = *static_cast<int *>(prop->data) != 0;
            emit muteChanged(muted_);
        } else if (name == "speed"_L1 && prop->format == MPV_FORMAT_DOUBLE && prop->data) {
            speed_ = *static_cast<double *>(prop->data);
            emit speedChanged(speed_);
        } else if (name == "media-title"_L1) {
            mediaTitle_ = (prop->format == MPV_FORMAT_STRING && prop->data)
                              ? QString::fromUtf8(*static_cast<char **>(prop->data))
                              : QString();
            emit titleChanged(mediaTitle_);
        } else if (name == "path"_L1) {
            path_ = (prop->format == MPV_FORMAT_STRING && prop->data)
                        ? QString::fromUtf8(*static_cast<char **>(prop->data))
                        : QString();
            emit pathChanged(path_);
        } else if (name == "paused-for-cache"_L1 && prop->format == MPV_FORMAT_FLAG && prop->data) {
            pausedForCache_ = *static_cast<int *>(prop->data) != 0;
            emit cacheStateChanged(pausedForCache_);
        } else if (name == "playlist-pos"_L1) {
            refreshPlaylist();
        }
        break;
    }
    case MPV_EVENT_START_FILE:
        refreshPlaylist();
        break;
    case MPV_EVENT_FILE_LOADED:
        refreshPlaylist();
        emit tracksChanged();
        emit fileLoaded();
        break;
    case MPV_EVENT_END_FILE: {
        auto *end = static_cast<mpv_event_end_file *>(event->data);
        refreshPlaylist();
        if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
            const QString message = ui("无法播放：%1", "Cannot play: %1")
                                        .arg(QString::fromUtf8(mpv_error_string(end->error)));
            lastError_ = message;
            emit playbackError(message);
        } else if (end && end->reason == MPV_END_FILE_REASON_EOF) {
            emit fileEnded();
        }
        break;
    }
    case MPV_EVENT_LOG_MESSAGE: {
        auto *log = static_cast<mpv_event_log_message *>(event->data);
        if (log && log->text) {
            QString text = QString::fromUtf8(log->prefix ? log->prefix : "")
                           + QStringLiteral(": ")
                           + QString::fromUtf8(log->text).trimmed();
            appendLog(text);
        }
        break;
    }
    case MPV_EVENT_SHUTDOWN:
        break;
    default:
        break;
    }
}

void MpvWidget::appendLog(const QString &line)
{
    logs_.append(line);
    if (logs_.size() > 80)
        logs_.removeFirst();
}

void MpvWidget::refreshPlaylist()
{
    playlist_.clear();
    if (!mpv_) {
        emit playlistChanged();
        return;
    }
    mpv_node node;
    if (mpv_get_property(mpv_, "playlist", MPV_FORMAT_NODE, &node) >= 0) {
        if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
            const mpv_node_list *list = node.u.list;
            for (int i = 0; i < list->num; ++i) {
                const mpv_node &item = list->values[i];
                PlaylistItem entry;
                entry.path = mapString(item, "filename");
                entry.title = mapString(item, "title");
                if (entry.title.isEmpty())
                    entry.title = QFileInfo(entry.path).fileName();
                entry.current = mapFlag(item, "current");
                playlist_.append(entry);
            }
        }
        mpv_free_node_contents(&node);
    }
    emit playlistChanged();
}

QVector<MediaTrack> MpvWidget::tracks() const
{
    QVector<MediaTrack> result;
    if (!mpv_)
        return result;
    mpv_node node;
    if (mpv_get_property(mpv_, "track-list", MPV_FORMAT_NODE, &node) < 0)
        return result;
    if (node.format == MPV_FORMAT_NODE_ARRAY && node.u.list) {
        const mpv_node_list *list = node.u.list;
        for (int i = 0; i < list->num; ++i) {
            const mpv_node &item = list->values[i];
            MediaTrack track;
            track.id = static_cast<int>(mapInt(item, "id"));
            track.type = mapString(item, "type");
            const QString lang = mapString(item, "lang");
            const QString title = mapString(item, "title");
            const QString codec = mapString(item, "codec");
            QStringList parts;
            parts << QStringLiteral("%1").arg(track.id);
            if (!lang.isEmpty())
                parts << lang;
            if (!title.isEmpty())
                parts << title;
            else if (!codec.isEmpty())
                parts << codec;
            track.label = parts.join(QStringLiteral(" · "));
            track.selected = mapFlag(item, "selected");
            result.append(track);
        }
    }
    mpv_free_node_contents(&node);
    return result;
}

int MpvWidget::command(const QByteArrayList &args)
{
    if (!mpv_)
        return MPV_ERROR_UNINITIALIZED;
    QVector<const char *> argv;
    argv.reserve(args.size() + 1);
    for (const QByteArray &arg : args)
        argv.append(arg.constData());
    argv.append(nullptr);
    const int rc = mpv_command(mpv_, argv.data());
    if (rc < 0)
        appendLog(QStringLiteral("command failed: %1 (%2)")
                      .arg(QString::fromUtf8(args.value(0)), QString::fromUtf8(mpv_error_string(rc))));
    onMpvEvents();
    return rc;
}

QString MpvWidget::propertyString(const char *name) const
{
    if (!mpv_)
        return {};
    char *value = mpv_get_property_string(mpv_, name);
    if (!value)
        return {};
    const QString text = QString::fromUtf8(value);
    mpv_free(value);
    return text;
}

bool MpvWidget::setPropertyString(const char *name, const QString &value)
{
    if (!mpv_)
        return false;
    const QByteArray utf8 = value.toUtf8();
    const int rc = mpv_set_property_string(mpv_, name, utf8.constData());
    return rc >= 0;
}

bool MpvWidget::setPropertyDouble(const char *name, double value)
{
    if (!mpv_)
        return false;
    return mpv_set_property(mpv_, name, MPV_FORMAT_DOUBLE, &value) >= 0;
}

bool MpvWidget::setPropertyFlag(const char *name, bool value)
{
    if (!mpv_)
        return false;
    int flag = value ? 1 : 0;
    return mpv_set_property(mpv_, name, MPV_FORMAT_FLAG, &flag) >= 0;
}

void MpvWidget::setResumeEnabled(bool enabled)
{
    resumeEnabled_ = enabled;
    setPropertyFlag("save-position-on-quit", enabled);
    setPropertyFlag("resume-playback", enabled);
}

void MpvWidget::leaveCurrentFile()
{
    if (!mpv_ || !resumeEnabled_ || path_.isEmpty())
        return;
    // A few seconds in is not worth a seek. A file left at the end should
    // start over, which is what most local players do.
    const bool keep = position_ >= 3.0 && (duration_ <= 0.0 || duration_ - position_ >= 5.0);
    if (keep) {
        command({QByteArrayLiteral("write-watch-later-config")});
        return;
    }
    setPropertyFlag("save-position-on-quit", false);
    command({QByteArrayLiteral("delete-watch-later-config")});
}

void MpvWidget::openReplace(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    // Drop the flag before loadfile so a finished file is not written again.
    leaveCurrentFile();
    QByteArray first = paths.first().toUtf8();
    command({QByteArrayLiteral("loadfile"), first, QByteArrayLiteral("replace")});
    for (int i = 1; i < paths.size(); ++i)
        command({QByteArrayLiteral("loadfile"), paths.at(i).toUtf8(), QByteArrayLiteral("append")});
    if (resumeEnabled_)
        setPropertyFlag("save-position-on-quit", true);
}

void MpvWidget::openAppend(const QStringList &paths)
{
    const bool wasEmpty = playlist_.isEmpty() && path_.isEmpty();
    for (int i = 0; i < paths.size(); ++i) {
        const QByteArray mode = (wasEmpty && i == 0) ? QByteArrayLiteral("replace")
                                                      : QByteArrayLiteral("append");
        command({QByteArrayLiteral("loadfile"), paths.at(i).toUtf8(), mode});
    }
}

void MpvWidget::playIndex(int index)
{
    leaveCurrentFile();
    command({QByteArrayLiteral("playlist-play-index"), QByteArray::number(index)});
    if (resumeEnabled_)
        setPropertyFlag("save-position-on-quit", true);
}

void MpvWidget::playNext()
{
    leaveCurrentFile();
    command({QByteArrayLiteral("playlist-next"), QByteArrayLiteral("force")});
    if (resumeEnabled_)
        setPropertyFlag("save-position-on-quit", true);
}

void MpvWidget::playPrevious()
{
    leaveCurrentFile();
    command({QByteArrayLiteral("playlist-prev"), QByteArrayLiteral("force")});
    if (resumeEnabled_)
        setPropertyFlag("save-position-on-quit", true);
}

void MpvWidget::removePlaylistIndex(int index)
{
    command({QByteArrayLiteral("playlist-remove"), QByteArray::number(index)});
    refreshPlaylist();
}

void MpvWidget::clearPlaylist()
{
    leaveCurrentFile();
    command({QByteArrayLiteral("stop")});
    if (resumeEnabled_)
        setPropertyFlag("save-position-on-quit", true);
    command({QByteArrayLiteral("playlist-clear")});
    path_.clear();
    mediaTitle_.clear();
    position_ = 0;
    duration_ = 0;
    refreshPlaylist();
    emit pathChanged(path_);
    emit titleChanged(mediaTitle_);
    update();
}

void MpvWidget::togglePause()
{
    setPaused(!paused_);
}

void MpvWidget::setPaused(bool paused)
{
    setPropertyFlag("pause", paused);
}

void MpvWidget::seekAbsolute(double seconds)
{
    if (!std::isfinite(seconds))
        return;
    command({QByteArrayLiteral("seek"), QByteArray::number(seconds, 'f', 3),
             QByteArrayLiteral("absolute"), QByteArrayLiteral("exact")});
}

void MpvWidget::seekRelative(double delta)
{
    command({QByteArrayLiteral("seek"), QByteArray::number(delta, 'f', 3), QByteArrayLiteral("relative")});
}

void MpvWidget::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 130.0);
    setPropertyDouble("volume", volume);
}

void MpvWidget::setMuted(bool muted)
{
    setPropertyFlag("mute", muted);
}

void MpvWidget::setSpeed(double speed)
{
    speed = std::clamp(speed, 0.25, 4.0);
    setPropertyDouble("speed", speed);
}

void MpvWidget::setLoopMode(const QString &mode)
{
    if (mode == QLatin1String("file")) {
        setPropertyString("loop-file", QStringLiteral("inf"));
        setPropertyString("loop-playlist", QStringLiteral("no"));
    } else if (mode == QLatin1String("playlist")) {
        setPropertyString("loop-file", QStringLiteral("no"));
        setPropertyString("loop-playlist", QStringLiteral("inf"));
    } else {
        setPropertyString("loop-file", QStringLiteral("no"));
        setPropertyString("loop-playlist", QStringLiteral("no"));
    }
}

QString MpvWidget::loopMode() const
{
    if (propertyString("loop-playlist") == QLatin1String("inf"))
        return QStringLiteral("playlist");
    const QString file = propertyString("loop-file");
    if (file == QLatin1String("inf") || file == QLatin1String("yes"))
        return QStringLiteral("file");
    return QStringLiteral("off");
}

void MpvWidget::applyPendingShaders()
{
    if (!mpv_ || !mpvGl_)
        return;
    shadersDirty_ = false;
    if (superResolution_ == SuperResolution::Off) {
        command({QByteArrayLiteral("change-list"), QByteArrayLiteral("glsl-shaders"),
                 QByteArrayLiteral("clr"), QByteArray()});
        return;
    }
    QString error;
    const QString argument = ShaderLibrary::argumentFor(superResolution_, &error);
    if (argument.isEmpty()) {
        lastError_ = error;
        appendLog(error);
        emit playbackError(error);
        return;
    }
    const int rc = command({QByteArrayLiteral("change-list"), QByteArrayLiteral("glsl-shaders"),
                            QByteArrayLiteral("set"), argument.toUtf8()});
    if (rc < 0) {
        lastError_ = ui("设置着色器失败：%1", "Failed to set shaders: %1")
                         .arg(QString::fromUtf8(mpv_error_string(rc)));
        emit playbackError(lastError_);
    }
}

bool MpvWidget::setSuperResolution(SuperResolution mode)
{
    superResolution_ = mode;
    shadersDirty_ = true;
    if (mpvGl_)
        applyPendingShaders();
    emit superResolutionChanged(mode);
    return lastError_.isEmpty();
}

QString MpvWidget::hwdec() const
{
    return propertyString("hwdec");
}

QString MpvWidget::shaderProperty() const
{
    return propertyString("glsl-shaders");
}

void MpvWidget::setHwdecEnabled(bool enabled)
{
    setPropertyString("hwdec", enabled ? QStringLiteral("auto-copy") : QStringLiteral("no"));
}

void MpvWidget::setAudioTrack(int id)
{
    setPropertyString("aid", QString::number(id));
}

void MpvWidget::setSubtitleTrack(int id)
{
    setPropertyString("sid", id < 0 ? QStringLiteral("no") : QString::number(id));
}

void MpvWidget::addSubtitle(const QString &path)
{
    command({QByteArrayLiteral("sub-add"), path.toUtf8()});
    emit tracksChanged();
}

bool MpvWidget::screenshotTo(const QString &path, QString *error)
{
    QFile::remove(path);
    const int rc = command({QByteArrayLiteral("screenshot-to-file"), path.toUtf8(),
                            QByteArrayLiteral("window")});
    if (rc < 0) {
        if (error)
            *error = ui("截图失败：%1", "Screenshot failed: %1").arg(QString::fromUtf8(mpv_error_string(rc)));
        return false;
    }
    return true;
}

void MpvWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !path_.isEmpty())
        emit videoClicked();
    QOpenGLWidget::mousePressEvent(event);
}

void MpvWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        emit videoDoubleClicked();
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void MpvWidget::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0)
        setVolume(volume_ + steps * 5.0);
    event->accept();
}
