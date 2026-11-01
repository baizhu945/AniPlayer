#pragma once

#include "shaders.h"

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QOpenGLWidget>
#include <QStringList>

struct PlaylistItem {
    QString path;
    QString title;
    bool current = false;
};

struct MediaTrack {
    int id = 0;
    QString type;
    QString label;
    bool selected = false;
};

class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    bool isReady() const { return mpvGl_ != nullptr; }
    QString lastError() const { return lastError_; }
    QStringList recentLogs() const { return logs_; }
    QString glDescription() const { return glDescription_; }

    QString path() const { return path_; }
    QString mediaTitle() const { return mediaTitle_; }
    double position() const { return position_; }
    double duration() const { return duration_; }
    bool paused() const { return paused_; }
    double volume() const { return volume_; }
    bool muted() const { return muted_; }
    double speed() const { return speed_; }
    bool pausedForCache() const { return pausedForCache_; }
    SuperResolution superResolution() const { return superResolution_; }
    QString hwdec() const;
    QString shaderProperty() const;
    QVector<PlaylistItem> playlist() const { return playlist_; }
    QVector<MediaTrack> tracks() const;

    void setResumeEnabled(bool enabled);
    bool resumeEnabled() const { return resumeEnabled_; }
    // Remember this file with mpv watch-later, or forget it when it is finished.
    void leaveCurrentFile();
    void openReplace(const QStringList &paths);
    void openAppend(const QStringList &paths);
    void playIndex(int index);
    void playNext();
    void playPrevious();
    void removePlaylistIndex(int index);
    void clearPlaylist();
    void togglePause();
    void setPaused(bool paused);
    void seekAbsolute(double seconds);
    void seekRelative(double delta);
    void setVolume(double volume);
    void setMuted(bool muted);
    void setSpeed(double speed);
    void setLoopMode(const QString &mode);
    QString loopMode() const;
    bool setSuperResolution(SuperResolution mode);
    void setHwdecEnabled(bool enabled);
    void setAudioTrack(int id);
    void setSubtitleTrack(int id);
    void addSubtitle(const QString &path);
    bool screenshotTo(const QString &path, QString *error);

signals:
    void renderReady();
    void renderFailed(const QString &message);
    void playbackError(const QString &message);
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pauseChanged(bool paused);
    void volumeChanged(double volume);
    void muteChanged(bool muted);
    void speedChanged(double speed);
    void titleChanged(const QString &title);
    void pathChanged(const QString &path);
    void playlistChanged();
    void tracksChanged();
    void cacheStateChanged(bool pausedForCache);
    void superResolutionChanged(SuperResolution mode);
    void fileLoaded();
    void fileEnded();
    void videoClicked();
    void videoDoubleClicked();

public slots:
    void onMpvEvents();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    static void onWakeup(void *ctx);
    static void onRenderUpdate(void *ctx);
    static void *glProcAddress(void *ctx, const char *name);

    void handleEvent(mpv_event *event);
    void observeProperties();
    void applyPendingShaders();
    void refreshPlaylist();
    void appendLog(const QString &line);
    int command(const QByteArrayList &args);
    QString propertyString(const char *name) const;
    bool setPropertyString(const char *name, const QString &value);
    bool setPropertyDouble(const char *name, double value);
    bool setPropertyFlag(const char *name, bool value);

    mpv_handle *mpv_ = nullptr;
    mpv_render_context *mpvGl_ = nullptr;
    bool pendingRenderUpdate_ = false;
    SuperResolution superResolution_ = SuperResolution::Off;
    bool shadersDirty_ = false;
    QString lastError_;
    QStringList logs_;
    QString glDescription_;
    QString path_;
    QString mediaTitle_;
    double position_ = 0;
    double duration_ = 0;
    bool paused_ = true;
    double volume_ = 100;
    bool muted_ = false;
    double speed_ = 1;
    bool pausedForCache_ = false;
    bool drainingEvents_ = false;
    bool resumeEnabled_ = false;
    QVector<PlaylistItem> playlist_;

};
