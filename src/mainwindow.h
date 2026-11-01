#pragma once

#include "mpvwidget.h"

#include <QMainWindow>

class QAction;
class QLabel;
class QMenu;
class QPushButton;
class QSlider;
class QListWidget;
class QTimer;
class QToolButton;
class MediaInfoDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool testMode = false, QWidget *parent = nullptr);

    MpvWidget *player() const { return player_; }
    void openPaths(const QStringList &paths, bool append);
    void openUrl(const QString &url);
    void setSuperResolution(SuperResolution mode);
    void setChromeAutohide(bool enabled);
    void showChrome();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void buildMenus();
    void applyStyle();
    void loadSettings();
    void saveSettings();
    void openFilesDialog(bool append);
    void openUrlDialog();
    void openSubtitleDialog();
    void showMediaInfo();
    void showContextMenu(const QPoint &globalPos);
    void fillContextMenu(QMenu *menu);
    void showSettings();
    void showShortcutDialog();
    void showAbout();
    void togglePlaylist(bool visible);
    void toggleFullscreen();
    void togglePlayback();
    void updatePlayIcon();
    void updateTimeLabel();
    void updateTitle();
    void applyTitleElide();
    void updatePlaylistWidget();
    void updateTrackMenus();
    void fillTrackMenu(QMenu *menu, const QString &type);
    void updateModeButtons();
    void updateEmptyState();
    void showOsd(const QString &text);
    void flash(QWidget *widget);
    void markActive(QWidget *widget, bool active);
    QString loopLabel() const;
    void noteActivity();
    void hideChrome();
    void layoutOverlays();
    void retranslate();
    QString formatTime(double seconds) const;
    QString superResolutionLabel(SuperResolution mode) const;

    MpvWidget *player_ = nullptr;
    QWidget *header_ = nullptr;
    QWidget *surface_ = nullptr;
    QWidget *footer_ = nullptr;
    QWidget *emptyState_ = nullptr;
    QWidget *playlistPanel_ = nullptr;
    QLabel *titleLabel_ = nullptr;
    QLabel *footerTitle_ = nullptr;
    QLabel *timeLabel_ = nullptr;
    QLabel *osd_ = nullptr;
    QPushButton *openButton_ = nullptr;
    QPushButton *playButton_ = nullptr;
    QToolButton *srButton_ = nullptr;
    QToolButton *speedButton_ = nullptr;
    QToolButton *loopButton_ = nullptr;
    QToolButton *audioButton_ = nullptr;
    QToolButton *subtitleButton_ = nullptr;
    QPushButton *prevButton_ = nullptr;
    QPushButton *nextButton_ = nullptr;
    QPushButton *playlistButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QPushButton *muteButton_ = nullptr;
    QPushButton *menuButton_ = nullptr;
    QSlider *seekSlider_ = nullptr;
    QSlider *volumeSlider_ = nullptr;
    QListWidget *playlistView_ = nullptr;
    QMenu *mainMenu_ = nullptr;
    QMenu *audioMenu_ = nullptr;
    QMenu *subtitleMenu_ = nullptr;
    QAction *srOffAction_ = nullptr;
    QAction *srEfficiencyAction_ = nullptr;
    QAction *srQualityAction_ = nullptr;
    QAction *hwdecAction_ = nullptr;
    QTimer *hideTimer_ = nullptr;
    QTimer *osdTimer_ = nullptr;
    bool seeking_ = false;
    bool chromeAutohide_ = true;
    bool playlistVisible_ = false;
    bool testMode_ = false;
    bool applyingSettings_ = false;
    QString lastDirectory_;
    QString titleText_;
    bool warnedQuality_ = false;
    MediaInfoDialog *mediaInfo_ = nullptr;
};
