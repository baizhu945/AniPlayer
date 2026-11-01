#include "mainwindow.h"

#include "i18n.h"
#include "mediainfo.h"
#include "shortcuts.h"

#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QEasingCurve>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <cmath>

namespace {

class Ripple : public QWidget {
public:
    Ripple(QWidget *host, const QPoint &origin)
        : QWidget(host)
        , origin_(origin)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setGeometry(host->rect());
        raise();
        show();

        auto *animation = new QVariantAnimation(this);
        animation->setDuration(170);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            progress_ = value.toReal();
            update();
        });
        connect(animation, &QVariantAnimation::finished, this, &QObject::deleteLater);
        animation->start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal limit = qBound(16.0, qreal(qMin(width(), height())) * 0.72, 36.0);
        const qreal radius = limit * (0.35 + 0.65 * progress_);
        const int alpha = qRound(190 * (1.0 - progress_));
        QPen pen(QColor(61, 174, 233, alpha));
        pen.setWidthF(2.0);
        painter.setPen(pen);
        painter.setBrush(QColor(61, 174, 233, qRound(alpha * 0.28)));
        painter.drawEllipse(QPointF(origin_), radius, radius);
    }

private:
    QPoint origin_;
    qreal progress_ = 0;
};

void spawnRipple(QWidget *host, const QPoint &origin)
{
    if (!host || !host->isVisible() || host->width() < 8 || host->height() < 8)
        return;
    new Ripple(host, origin);
}

class SeekSlider : public QSlider {
public:
    explicit SeekSlider(QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
        setObjectName(QStringLiteral("seekSlider"));
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setRange(0, 0);
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && maximum() > minimum()) {
            setSliderDown(true);
            jump(event->position().x());
            emit sliderPressed();
            event->accept();
            return;
        }
        QSlider::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if ((event->buttons() & Qt::LeftButton) && maximum() > minimum())
            jump(event->position().x());
        if (maximum() > 0) {
            const int value = QStyle::sliderValueFromPosition(minimum(), maximum(),
                                                              static_cast<int>(event->position().x()), width());
            QToolTip::showText(event->globalPosition().toPoint(), formatClock(value / 1000.0), this);
        }
        QSlider::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (isSliderDown()) {
            jump(event->position().x());
            setSliderDown(false);
            emit sliderReleased();
            event->accept();
            return;
        }
        QSlider::mouseReleaseEvent(event);
    }

private:
    void jump(int x)
    {
        const int value = QStyle::sliderValueFromPosition(minimum(), maximum(), x, width());
        setValue(value);
        emit sliderMoved(value);
    }

    static QString formatClock(double seconds)
    {
        if (!std::isfinite(seconds) || seconds < 0)
            seconds = 0;
        const int total = static_cast<int>(seconds + 0.5);
        const int hours = total / 3600;
        const int minutes = (total % 3600) / 60;
        const int secs = total % 60;
        if (hours > 0) {
            return QStringLiteral("%1:%2:%3")
                .arg(hours)
                .arg(minutes, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'));
        }
        return QStringLiteral("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
    }
};

QPushButton *chromeButton(const QString &objectName, const QString &tip, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(objectName);
    button->setProperty("chrome", true);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(34, 34);
    button->setFlat(true);
    return button;
}

void setThemeIcon(QAbstractButton *button, const QString &theme, const QString &fallback)
{
    const QIcon icon = QIcon::fromTheme(theme);
    if (icon.isNull())
        button->setText(fallback);
    else {
        button->setIcon(icon);
        button->setIconSize(QSize(18, 18));
        button->setText(QString());
    }
}

QString sliderStyle()
{
    return QStringLiteral(
        "QSlider::groove:horizontal { height: 5px; background: #4a5158; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #3daee9; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 16px; height: 16px; margin: -6px 0; "
        "background: #3daee9; border-radius: 8px; }"
        "QSlider::handle:horizontal:pressed { background: #f4f6f8; }");
}

QIcon playerIcon()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList sizes = {
        QStringLiteral("1024x1024"),
        QStringLiteral("256x256"),
        QStringLiteral("128x128"),
        QStringLiteral("64x64"),
    };
    QStringList candidates;
    candidates << QDir(appDir).filePath(QStringLiteral("icons/aniplayer.png"));
    candidates << QDir(appDir).filePath(QStringLiteral("../icons/aniplayer.png"));
    for (const QString &size : sizes) {
        candidates << QDir(appDir).filePath(
            QStringLiteral("../share/icons/hicolor/%1/apps/aniplayer.png").arg(size));
    }
    QDir walk(appDir);
    for (int i = 0; i < 5; ++i) {
        candidates << walk.filePath(QStringLiteral("icons/aniplayer.png"));
        for (const QString &size : sizes) {
            candidates << walk.filePath(
                QStringLiteral("share/icons/hicolor/%1/apps/aniplayer.png").arg(size));
        }
        if (!walk.cdUp())
            break;
    }
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString &dataDir : dataDirs) {
        for (const QString &size : sizes) {
            candidates << QDir(dataDir).filePath(
                QStringLiteral("icons/hicolor/%1/apps/aniplayer.png").arg(size));
        }
    }
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return QIcon(candidate);
    }
    if (QIcon::hasThemeIcon(QStringLiteral("aniplayer")))
        return QIcon::fromTheme(QStringLiteral("aniplayer"));
    return QIcon::fromTheme(QStringLiteral("multimedia-video-player"));
}

QToolButton *modeButton(const QString &objectName, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setProperty("chrome", true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::PointingHandCursor);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setFixedHeight(34);
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return button;
}

} // namespace

MainWindow::MainWindow(bool testMode, QWidget *parent)
    : QMainWindow(parent)
    , testMode_(testMode)
{
    setWindowTitle(QStringLiteral("AniPlayer"));
    setWindowIcon(playerIcon());
    setAcceptDrops(true);
    setMinimumSize(900, 480);
    if (QIcon::themeName().isEmpty())
        QIcon::setThemeName(QStringLiteral("breeze"));

    buildUi();
    buildMenus();
    applyStyle();

    hideTimer_ = new QTimer(this);
    hideTimer_->setSingleShot(true);
    hideTimer_->setInterval(1600);
    connect(hideTimer_, &QTimer::timeout, this, &MainWindow::hideChrome);

    osdTimer_ = new QTimer(this);
    osdTimer_->setSingleShot(true);
    connect(osdTimer_, &QTimer::timeout, osd_, &QWidget::hide);

    connect(&I18n::instance(), &I18n::languageChanged, this, &MainWindow::retranslate);
    loadSettings();
    retranslate();

    if (testMode_) {
        chromeAutohide_ = false;
        resize(960, 640);
        player_->setMuted(true);
    }
    showChrome();
    qApp->installEventFilter(this);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    header_ = new QWidget(central);
    header_->setObjectName(QStringLiteral("header"));
    header_->setFixedHeight(46);
    auto *headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(8, 4, 8, 4);
    headerLayout->setSpacing(6);

    menuButton_ = chromeButton(QStringLiteral("menuButton"), QString(), header_);
    setThemeIcon(menuButton_, QStringLiteral("application-menu"), QStringLiteral("☰"));

    openButton_ = new QPushButton(header_);
    openButton_->setObjectName(QStringLiteral("openButton"));
    openButton_->setCursor(Qt::PointingHandCursor);
    openButton_->setFocusPolicy(Qt::NoFocus);
    openButton_->setFixedHeight(34);
    connect(openButton_, &QPushButton::clicked, this, [this] { openFilesDialog(false); });

    titleLabel_ = new QLabel(QStringLiteral("AniPlayer"), header_);
    titleLabel_->setObjectName(QStringLiteral("headerTitle"));
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLabel_->setMinimumWidth(0);
    titleLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *languageButton = chromeButton(QStringLiteral("languageButton"), QString(), header_);
    languageButton->setFixedWidth(52);
    connect(languageButton, &QPushButton::clicked, this, [] {
        auto &i18n = I18n::instance();
        i18n.setLanguage(i18n.language() == I18n::Language::English ? I18n::Language::Chinese
                                                                    : I18n::Language::English);
    });

    auto *infoButton = chromeButton(QStringLiteral("mediaInfoButton"), QString(), header_);
    setThemeIcon(infoButton, QStringLiteral("dialog-information"), QStringLiteral("信息"));
    connect(infoButton, &QPushButton::clicked, this, &MainWindow::showMediaInfo);

    playlistButton_ = chromeButton(QStringLiteral("playlistButton"), QString(), header_);
    playlistButton_->setCheckable(true);
    setThemeIcon(playlistButton_, QStringLiteral("view-media-playlist"), QStringLiteral("列表"));
    connect(playlistButton_, &QPushButton::toggled, this, &MainWindow::togglePlaylist);

    headerLayout->addWidget(menuButton_);
    headerLayout->addWidget(openButton_);
    headerLayout->addWidget(titleLabel_, 1);
    headerLayout->addWidget(languageButton);
    headerLayout->addWidget(infoButton);
    headerLayout->addWidget(playlistButton_);
    root->addWidget(header_);

    surface_ = new QWidget(central);
    surface_->setObjectName(QStringLiteral("surface"));
    surface_->setMouseTracking(true);
    root->addWidget(surface_, 1);

    player_ = new MpvWidget(surface_);
    player_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(player_, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        showContextMenu(player_->mapToGlobal(pos));
    });

    emptyState_ = new QWidget(surface_);
    emptyState_->setObjectName(QStringLiteral("emptyState"));
    auto *emptyLayout = new QVBoxLayout(emptyState_);
    emptyLayout->setSpacing(10);
    auto *emptyTitle = new QLabel(QStringLiteral("AniPlayer"), emptyState_);
    emptyTitle->setObjectName(QStringLiteral("emptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    auto *emptyHint = new QLabel(emptyState_);
    emptyHint->setObjectName(QStringLiteral("emptyHint"));
    emptyHint->setAlignment(Qt::AlignCenter);
    auto *emptyOpen = new QPushButton(QStringLiteral("打开文件"), emptyState_);
    emptyOpen->setObjectName(QStringLiteral("emptyOpenButton"));
    emptyOpen->setCursor(Qt::PointingHandCursor);
    emptyOpen->setFocusPolicy(Qt::NoFocus);
    connect(emptyOpen, &QPushButton::clicked, this, [this] { openFilesDialog(false); });
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyHint);
    emptyLayout->addWidget(emptyOpen, 0, Qt::AlignCenter);

    footer_ = new QFrame(surface_);
    footer_->setObjectName(QStringLiteral("footer"));
    footer_->setAttribute(Qt::WA_StyledBackground, true);
    auto *footerLayout = new QVBoxLayout(footer_);
    footerLayout->setContentsMargins(8, 6, 8, 8);
    footerLayout->setSpacing(4);

    auto *seekRow = new QHBoxLayout;
    seekRow->setSpacing(8);
    seekSlider_ = new SeekSlider(footer_);
    seekSlider_->setFixedHeight(22);
    seekSlider_->setStyleSheet(sliderStyle());
    timeLabel_ = new QLabel(QStringLiteral("00:00 / 00:00"), footer_);
    timeLabel_->setObjectName(QStringLiteral("timeLabel"));
    QFont timeFont = timeLabel_->font();
    timeFont.setFamilies({QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Noto Sans Mono"),
                          QStringLiteral("monospace")});
    timeFont.setStyleHint(QFont::Monospace);
    timeLabel_->setFont(timeFont);
    timeLabel_->setMinimumWidth(timeLabel_->fontMetrics().horizontalAdvance(QStringLiteral("0:00:00 / 0:00:00")));
    timeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    seekRow->addWidget(seekSlider_, 1);
    seekRow->addWidget(timeLabel_);
    footerLayout->addLayout(seekRow);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    playButton_ = chromeButton(QStringLiteral("playPauseButton"), QString(), footer_);
    prevButton_ = chromeButton(QStringLiteral("prevButton"), QString(), footer_);
    nextButton_ = chromeButton(QStringLiteral("nextButton"), QString(), footer_);
    setThemeIcon(prevButton_, QStringLiteral("media-skip-backward"), QStringLiteral("⏮"));
    setThemeIcon(nextButton_, QStringLiteral("media-skip-forward"), QStringLiteral("⏭"));
    muteButton_ = chromeButton(QStringLiteral("muteButton"), QString(), footer_);
    volumeSlider_ = new QSlider(Qt::Horizontal, footer_);
    volumeSlider_->setObjectName(QStringLiteral("volumeSlider"));
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(80);
    volumeSlider_->setFixedWidth(84);
    volumeSlider_->setFixedHeight(22);
    volumeSlider_->setFocusPolicy(Qt::NoFocus);
    volumeSlider_->setStyleSheet(sliderStyle());
    srButton_ = modeButton(QStringLiteral("srButton"), footer_);
    speedButton_ = modeButton(QStringLiteral("speedButton"), footer_);
    loopButton_ = modeButton(QStringLiteral("loopButton"), footer_);
    subtitleButton_ = modeButton(QStringLiteral("subtitleButton"), footer_);
    audioButton_ = modeButton(QStringLiteral("audioButton"), footer_);
    fullscreenButton_ = chromeButton(QStringLiteral("fullscreenButton"), QString(), footer_);
    setThemeIcon(fullscreenButton_, QStringLiteral("view-fullscreen"), QStringLiteral("⛶"));
    footerTitle_ = new QLabel(footer_);
    footerTitle_->setObjectName(QStringLiteral("footerTitle"));
    footerTitle_->setMinimumWidth(0);
    footerTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    controls->addWidget(playButton_);
    controls->addWidget(prevButton_);
    controls->addWidget(nextButton_);
    controls->addWidget(footerTitle_, 1);
    controls->addWidget(muteButton_);
    controls->addWidget(volumeSlider_);
    controls->addWidget(srButton_);
    controls->addWidget(speedButton_);
    controls->addWidget(loopButton_);
    controls->addWidget(subtitleButton_);
    controls->addWidget(audioButton_);
    controls->addWidget(fullscreenButton_);
    footerLayout->addLayout(controls);

    playlistPanel_ = new QFrame(surface_);
    playlistPanel_->setObjectName(QStringLiteral("playlistPanel"));
    playlistPanel_->setAttribute(Qt::WA_StyledBackground, true);
    playlistPanel_->hide();
    auto *playlistLayout = new QVBoxLayout(playlistPanel_);
    playlistLayout->setContentsMargins(12, 12, 12, 12);
    auto *playlistHeader = new QHBoxLayout;
    auto *playlistTitle = new QLabel(playlistPanel_);
    playlistTitle->setObjectName(QStringLiteral("sectionLabel"));
    auto *addButton = new QPushButton(QStringLiteral("添加"), playlistPanel_);
    addButton->setObjectName(QStringLiteral("playlistAddButton"));
    addButton->setFocusPolicy(Qt::NoFocus);
    auto *removeButton = new QPushButton(QStringLiteral("移除"), playlistPanel_);
    removeButton->setObjectName(QStringLiteral("playlistRemoveButton"));
    removeButton->setFocusPolicy(Qt::NoFocus);
    auto *clearButton = new QPushButton(QStringLiteral("清空"), playlistPanel_);
    clearButton->setObjectName(QStringLiteral("playlistClearButton"));
    clearButton->setFocusPolicy(Qt::NoFocus);
    playlistHeader->addWidget(playlistTitle);
    playlistHeader->addStretch(1);
    playlistHeader->addWidget(addButton);
    playlistHeader->addWidget(removeButton);
    playlistHeader->addWidget(clearButton);
    playlistView_ = new QListWidget(playlistPanel_);
    playlistView_->setObjectName(QStringLiteral("playlistView"));
    playlistView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    playlistLayout->addLayout(playlistHeader);
    playlistLayout->addWidget(playlistView_, 1);

    osd_ = new QLabel(surface_);
    osd_->setObjectName(QStringLiteral("osdLabel"));
    osd_->setAlignment(Qt::AlignCenter);
    osd_->hide();

    connect(playButton_, &QPushButton::clicked, this, &MainWindow::togglePlayback);
    connect(prevButton_, &QPushButton::clicked, player_, &MpvWidget::playPrevious);
    connect(nextButton_, &QPushButton::clicked, player_, &MpvWidget::playNext);
    connect(fullscreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullscreen);
    connect(muteButton_, &QPushButton::clicked, this, [this] {
        player_->setMuted(!player_->muted());
        showOsd(player_->muted() ? ui("已静音", "Muted") : ui("取消静音", "Unmuted"));
    });
    connect(seekSlider_, &QSlider::sliderPressed, this, [this] { seeking_ = true; });
    connect(seekSlider_, &QSlider::sliderMoved, this, [this](int value) {
        showOsd(formatTime(value / 1000.0));
    });
    connect(seekSlider_, &QSlider::sliderReleased, this, [this] {
        seeking_ = false;
        player_->seekAbsolute(seekSlider_->value() / 1000.0);
    });
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        player_->setVolume(value);
    });
    connect(volumeSlider_, &QSlider::sliderMoved, this, [this](int value) {
        showOsd(ui("音量 %1%", "Volume %1%").arg(value));
    });
    connect(addButton, &QPushButton::clicked, this, [this] { openFilesDialog(true); });
    connect(removeButton, &QPushButton::clicked, this, [this] {
        const int row = playlistView_->currentRow();
        if (row >= 0)
            player_->removePlaylistIndex(row);
    });
    connect(clearButton, &QPushButton::clicked, player_, &MpvWidget::clearPlaylist);
    connect(playlistView_, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        player_->playIndex(playlistView_->row(item));
    });

    connect(player_, &MpvWidget::positionChanged, this, [this](double) { updateTimeLabel(); });
    connect(player_, &MpvWidget::durationChanged, this, [this](double seconds) {
        QSignalBlocker blocker(seekSlider_);
        seekSlider_->setRange(0, seconds > 0 ? static_cast<int>(seconds * 1000.0) : 0);
        seekSlider_->setEnabled(seconds > 0);
        updateTimeLabel();
    });
    connect(player_, &MpvWidget::pauseChanged, this, [this](bool) { updatePlayIcon(); });
    connect(player_, &MpvWidget::volumeChanged, this, [this](double volume) {
        if (!volumeSlider_->isSliderDown()) {
            QSignalBlocker blocker(volumeSlider_);
            volumeSlider_->setValue(static_cast<int>(std::lround(volume)));
        }
    });
    connect(player_, &MpvWidget::muteChanged, this, [this](bool) { updatePlayIcon(); });
    connect(player_, &MpvWidget::titleChanged, this, [this](const QString &) { updateTitle(); });
    connect(player_, &MpvWidget::pathChanged, this, [this](const QString &) {
        updateEmptyState();
        updateTitle();
    });
    connect(player_, &MpvWidget::playlistChanged, this, &MainWindow::updatePlaylistWidget);
    connect(player_, &MpvWidget::tracksChanged, this, &MainWindow::updateTrackMenus);
    connect(player_, &MpvWidget::cacheStateChanged, this, [this](bool caching) {
        if (caching)
            showOsd(ui("正在缓冲…", "Buffering…"));
    });
    connect(player_, &MpvWidget::fileLoaded, this, [this] {
        updateEmptyState();
        updateTrackMenus();
        updatePlaylistWidget();
    });
    connect(player_, &MpvWidget::fileEnded, this, [this] { showOsd(ui("播放结束", "Playback finished")); });
    connect(player_, &MpvWidget::playbackError, this, [this](const QString &message) { showOsd(message); });
    connect(player_, &MpvWidget::renderFailed, this, [this](const QString &message) {
        showOsd(message);
        if (!testMode_)
            QMessageBox::critical(this, QStringLiteral("AniPlayer"), message);
    });
    connect(player_, &MpvWidget::videoClicked, this, &MainWindow::togglePlayback);
    connect(player_, &MpvWidget::videoDoubleClicked, this, &MainWindow::toggleFullscreen);
    connect(player_, &MpvWidget::superResolutionChanged, this, [this](SuperResolution) {
        updateModeButtons();
    });
    connect(player_, &MpvWidget::speedChanged, this, [this](double) { updateModeButtons(); });

    const auto armContext = [this](QWidget *widget) {
        widget->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(widget, &QWidget::customContextMenuRequested, this, [this, widget](const QPoint &pos) {
            showContextMenu(widget->mapToGlobal(pos));
        });
    };
    armContext(header_);
    armContext(footer_);
    armContext(playlistPanel_);
    armContext(emptyState_);

    updatePlayIcon();
    updateEmptyState();
    updatePlaylistWidget();
}

void MainWindow::buildMenus()
{
    mainMenu_ = new QMenu(this);
    mainMenu_->setObjectName(QStringLiteral("mainMenu"));

    auto *openAction = new QAction(this);
    openAction->setObjectName(QStringLiteral("actionOpen"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this] { openFilesDialog(false); });

    auto *appendAction = new QAction(this);
    appendAction->setObjectName(QStringLiteral("actionAppend"));
    appendAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(appendAction, &QAction::triggered, this, [this] { openFilesDialog(true); });

    auto *urlAction = new QAction(this);
    urlAction->setObjectName(QStringLiteral("actionOpenUrl"));
    urlAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(urlAction, &QAction::triggered, this, &MainWindow::openUrlDialog);

    auto *playAction = new QAction(this);
    playAction->setObjectName(QStringLiteral("actionPlayPause"));
    playAction->setShortcut(Qt::Key_Space);
    connect(playAction, &QAction::triggered, this, &MainWindow::togglePlayback);

    auto *prevAction = new QAction(this);
    prevAction->setObjectName(QStringLiteral("actionPrevious"));
    prevAction->setShortcut(Qt::Key_P);
    connect(prevAction, &QAction::triggered, player_, &MpvWidget::playPrevious);

    auto *nextAction = new QAction(this);
    nextAction->setObjectName(QStringLiteral("actionNext"));
    nextAction->setShortcut(Qt::Key_N);
    connect(nextAction, &QAction::triggered, player_, &MpvWidget::playNext);

    auto *backAction = new QAction(this);
    backAction->setObjectName(QStringLiteral("actionSeekBack"));
    backAction->setShortcut(Qt::Key_Left);
    connect(backAction, &QAction::triggered, this, [this] {
        player_->seekRelative(-5);
        showOsd(ui("后退 5 秒", "Back 5 seconds"));
    });
    auto *forwardAction = new QAction(this);
    forwardAction->setObjectName(QStringLiteral("actionSeekForward"));
    forwardAction->setShortcut(Qt::Key_Right);
    connect(forwardAction, &QAction::triggered, this, [this] {
        player_->seekRelative(5);
        showOsd(ui("前进 5 秒", "Forward 5 seconds"));
    });

    auto *volumeUp = new QAction(this);
    volumeUp->setObjectName(QStringLiteral("actionVolumeUp"));
    volumeUp->setShortcut(Qt::Key_Up);
    connect(volumeUp, &QAction::triggered, this, [this] {
        const int value = std::min(100, volumeSlider_->value() + 5);
        volumeSlider_->setValue(value);
        showOsd(ui("音量 %1%", "Volume %1%").arg(value));
    });
    auto *volumeDown = new QAction(this);
    volumeDown->setObjectName(QStringLiteral("actionVolumeDown"));
    volumeDown->setShortcut(Qt::Key_Down);
    connect(volumeDown, &QAction::triggered, this, [this] {
        const int value = std::max(0, volumeSlider_->value() - 5);
        volumeSlider_->setValue(value);
        showOsd(ui("音量 %1%", "Volume %1%").arg(value));
    });

    auto *muteAction = new QAction(this);
    muteAction->setObjectName(QStringLiteral("actionMute"));
    muteAction->setShortcut(Qt::Key_M);
    connect(muteAction, &QAction::triggered, muteButton_, &QPushButton::click);

    auto *playlistAction = new QAction(this);
    playlistAction->setObjectName(QStringLiteral("actionPlaylist"));
    playlistAction->setShortcut(Qt::Key_L);
    connect(playlistAction, &QAction::triggered, this, [this] { togglePlaylist(!playlistVisible_); });

    auto *fullscreenAction = new QAction(this);
    fullscreenAction->setObjectName(QStringLiteral("actionFullscreen"));
    fullscreenAction->setShortcuts({QKeySequence(Qt::Key_F), QKeySequence(Qt::Key_F11)});
    connect(fullscreenAction, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    auto *escapeAction = new QAction(this);
    escapeAction->setObjectName(QStringLiteral("actionExitFullscreen"));
    escapeAction->setShortcut(Qt::Key_Escape);
    connect(escapeAction, &QAction::triggered, this, [this] {
        if (isFullScreen())
            toggleFullscreen();
    });

    auto *mediaInfoAction = new QAction(this);
    mediaInfoAction->setObjectName(QStringLiteral("actionMediaInfo"));
    mediaInfoAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    mediaInfoAction->setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
    connect(mediaInfoAction, &QAction::triggered, this, &MainWindow::showMediaInfo);

    auto *subtitleFileAction = new QAction(this);
    subtitleFileAction->setObjectName(QStringLiteral("actionSubtitleFile"));
    connect(subtitleFileAction, &QAction::triggered, this, &MainWindow::openSubtitleDialog);

    auto *settingsAction = new QAction(this);
    settingsAction->setObjectName(QStringLiteral("actionSettings"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    auto *aboutAction = new QAction(this);
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    auto *quitAction = new QAction(this);
    quitAction->setObjectName(QStringLiteral("actionQuit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *srMenu = new QMenu(this);
    srMenu->setObjectName(QStringLiteral("srMenu"));
    auto *srGroup = new QActionGroup(this);
    srGroup->setExclusive(true);
    auto addSr = [this, srMenu, srGroup](const QString &text, SuperResolution mode, const QString &name) {
        auto *action = srMenu->addAction(text);
        action->setObjectName(name);
        action->setCheckable(true);
        srGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] { setSuperResolution(mode); });
        return action;
    };
    srOffAction_ = addSr(QString(), SuperResolution::Off, QStringLiteral("actionSrOff"));
    srEfficiencyAction_ = addSr(QString(), SuperResolution::Efficiency, QStringLiteral("actionSrEfficiency"));
    srQualityAction_ = addSr(QString(), SuperResolution::Quality, QStringLiteral("actionSrQuality"));
    srOffAction_->setChecked(true);

    auto *speedMenu = new QMenu(this);
    speedMenu->setObjectName(QStringLiteral("speedMenu"));
    auto *speedGroup = new QActionGroup(this);
    speedGroup->setExclusive(true);
    for (double speed : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0}) {
        auto *action = speedMenu->addAction(QString::number(speed, 'g', 3) + QLatin1Char('x'));
        action->setData(speed);
        action->setCheckable(true);
        action->setChecked(speed == 1.0);
        speedGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, speed] {
            player_->setSpeed(speed);
            if (speedButton_) {
                speedButton_->setText(QString::number(speed, 'g', 3) + QStringLiteral("× ▾"));
                markActive(speedButton_, std::abs(speed - 1.0) > 0.01);
            }
            showOsd(ui("倍速 %1×", "Speed %1×").arg(QString::number(speed, 'g', 3)));
            flash(speedButton_);
        });
    }

    auto *loopMenu = new QMenu(this);
    loopMenu->setObjectName(QStringLiteral("loopMenu"));
    auto *loopGroup = new QActionGroup(this);
    loopGroup->setExclusive(true);
    auto addLoop = [this, loopMenu, loopGroup](const QString &text, const QString &mode) {
        auto *action = loopMenu->addAction(text);
        action->setCheckable(true);
        action->setData(mode);
        loopGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            player_->setLoopMode(mode);
            updateModeButtons();
            if (mode == QLatin1String("file"))
                showOsd(ui("循环：单个文件", "Loop: one file"));
            else if (mode == QLatin1String("playlist"))
                showOsd(ui("循环：整个列表", "Loop: playlist"));
            else
                showOsd(ui("循环已关闭", "Loop off"));
            flash(loopButton_);
        });
        return action;
    };
    auto *loopOff = addLoop(QString(), QStringLiteral("off"));
    loopOff->setObjectName(QStringLiteral("actionLoopOff"));
    loopOff->setChecked(true);
    addLoop(QString(), QStringLiteral("file"))->setObjectName(QStringLiteral("actionLoopFile"));
    addLoop(QString(), QStringLiteral("playlist"))->setObjectName(QStringLiteral("actionLoopList"));

    hwdecAction_ = new QAction(this);
    hwdecAction_->setObjectName(QStringLiteral("actionHwdec"));
    hwdecAction_->setCheckable(true);
    hwdecAction_->setChecked(true);
    connect(hwdecAction_, &QAction::toggled, this, [this](bool checked) {
        player_->setHwdecEnabled(checked);
        if (!applyingSettings_) {
            showOsd(checked ? ui("硬件解码：auto-copy", "Hardware decoding: auto-copy")
                            : ui("硬件解码已关闭", "Hardware decoding off"));
        }
    });

    audioMenu_ = new QMenu(this);
    audioMenu_->setObjectName(QStringLiteral("audioMenu"));
    subtitleMenu_ = new QMenu(this);
    subtitleMenu_->setObjectName(QStringLiteral("subtitleMenu"));
    connect(audioMenu_, &QMenu::aboutToShow, this, &MainWindow::updateTrackMenus);
    connect(subtitleMenu_, &QMenu::aboutToShow, this, &MainWindow::updateTrackMenus);

    auto *languageMenu = new QMenu(this);
    languageMenu->setObjectName(QStringLiteral("languageMenu"));
    auto *langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    auto *langZh = languageMenu->addAction(QStringLiteral("中文"));
    langZh->setObjectName(QStringLiteral("actionLangZh"));
    langZh->setCheckable(true);
    auto *langEn = languageMenu->addAction(QStringLiteral("English"));
    langEn->setObjectName(QStringLiteral("actionLangEn"));
    langEn->setCheckable(true);
    langGroup->addAction(langZh);
    langGroup->addAction(langEn);
    connect(langZh, &QAction::triggered, this, [] {
        I18n::instance().setLanguage(I18n::Language::Chinese);
    });
    connect(langEn, &QAction::triggered, this, [] {
        I18n::instance().setLanguage(I18n::Language::English);
    });

    mainMenu_->addAction(openAction);
    mainMenu_->addAction(appendAction);
    mainMenu_->addAction(urlAction);
    mainMenu_->addAction(subtitleFileAction);
    mainMenu_->addSeparator();
    mainMenu_->addMenu(languageMenu);
    mainMenu_->addAction(settingsAction);
    mainMenu_->addAction(aboutAction);
    mainMenu_->addSeparator();
    mainMenu_->addAction(quitAction);

    const QList<QAction *> shortcuts = {
        openAction, appendAction, urlAction, playAction, prevAction, nextAction, backAction, forwardAction,
        volumeUp, volumeDown, muteAction, playlistAction, fullscreenAction, escapeAction, mediaInfoAction,
        quitAction};
    for (QAction *action : shortcuts)
        addAction(action);

    auto *shortcutAction = new QAction(this);
    shortcutAction->setObjectName(QStringLiteral("actionShortcuts"));
    connect(shortcutAction, &QAction::triggered, this, &MainWindow::showShortcutDialog);
    addAction(shortcutAction);
    mainMenu_->insertAction(settingsAction, shortcutAction);

    connect(menuButton_, &QPushButton::clicked, this, [this] {
        mainMenu_->popup(menuButton_->mapToGlobal(QPoint(0, menuButton_->height())));
    });

    srButton_->setMenu(srMenu);
    speedButton_->setMenu(speedMenu);
    loopButton_->setMenu(loopMenu);
    audioButton_->setMenu(audioMenu_);
    subtitleButton_->setMenu(subtitleMenu_);

    ShortcutStore::ensureFile();
    ShortcutStore::apply(this, ShortcutStore::load());
}

void MainWindow::fillContextMenu(QMenu *menu)
{
    auto take = [this, menu](const char *name) {
        if (QAction *action = findChild<QAction *>(QLatin1String(name)))
            menu->addAction(action);
    };
    auto copyMenu = [menu](QMenu *source, const QString &title) {
        if (!source)
            return;
        auto *copy = menu->addMenu(title);
        for (QAction *action : source->actions())
            copy->addAction(action);
    };

    take("actionPlayPause");
    take("actionPrevious");
    take("actionNext");
    menu->addSeparator();
    copyMenu(findChild<QMenu *>(QStringLiteral("srMenu")), ui("超分辨率", "Super Resolution"));
    copyMenu(findChild<QMenu *>(QStringLiteral("speedMenu")), ui("倍速", "Speed"));
    copyMenu(findChild<QMenu *>(QStringLiteral("loopMenu")), ui("循环", "Loop"));
    auto *audio = menu->addMenu(ui("音轨", "Audio Track"));
    fillTrackMenu(audio, QStringLiteral("audio"));
    auto *subtitle = menu->addMenu(ui("字幕", "Subtitles"));
    fillTrackMenu(subtitle, QStringLiteral("sub"));
    take("actionSubtitleFile");
    take("actionHwdec");
    menu->addSeparator();
    take("actionMediaInfo");
    take("actionPlaylist");
    take("actionFullscreen");
    menu->addSeparator();
    take("actionOpen");
    take("actionAppend");
    take("actionOpenUrl");
    menu->addSeparator();
    take("actionShortcuts");
    take("actionSettings");
}

void MainWindow::showContextMenu(const QPoint &globalPos)
{
    auto *menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("contextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    fillContextMenu(menu);
    menu->popup(globalPos);
}

void MainWindow::applyStyle()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow, #surface { background: #111315; }"
        "#header { background: #2a2e32; border-bottom: 1px solid #191b1d; }"
        "#headerTitle { color: #eff0f1; font-size: 14px; }"
        "#srLabel { color: #8bd0f5; font-size: 12px; padding-right: 6px; }"
        "#footer { background: rgba(35, 38, 41, 230); border: 1px solid rgba(255,255,255,0.08); "
        "border-radius: 10px; }"
        "#footerTitle, #timeLabel { color: #c5c8cb; font-size: 12px; }"
        "#playlistPanel { background: rgba(32, 35, 38, 236); border: 1px solid rgba(255,255,255,0.08); "
        "border-radius: 10px; }"
        "#sectionLabel { color: #eff0f1; font-weight: 600; }"
        "QPushButton[chrome=\"true\"], QToolButton[chrome=\"true\"] { background: transparent; "
        "color: #eff0f1; border: none; border-radius: 6px; }"
        "QToolButton[chrome=\"true\"] { padding: 0 8px; }"
        "QPushButton[chrome=\"true\"]:hover, QToolButton[chrome=\"true\"]:hover { "
        "background: rgba(255,255,255,0.10); }"
        "QPushButton[chrome=\"true\"]:pressed, QToolButton[chrome=\"true\"]:pressed { "
        "background: rgba(61, 174, 233, 0.50); }"
        "QPushButton[chrome=\"true\"]:checked { background: rgba(61, 174, 233, 0.30); }"
        "QPushButton[chrome=\"true\"]:checked:hover { background: rgba(61, 174, 233, 0.40); }"
        "QToolButton[chrome=\"true\"][active=\"true\"] { background: rgba(61, 174, 233, 0.22); }"
        "QToolButton[chrome=\"true\"][active=\"true\"]:hover { background: rgba(61, 174, 233, 0.34); }"
        "QToolButton[chrome=\"true\"]::menu-indicator { image: none; width: 0px; }"
        "QPushButton[chrome=\"true\"]:disabled, QToolButton[chrome=\"true\"]:disabled { color: #6d7378; }"
        "#openButton { background: transparent; color: #eff0f1; border: 1px solid rgba(255,255,255,0.18); "
        "border-radius: 6px; padding: 0 12px; }"
        "#openButton:hover { background: rgba(255,255,255,0.08); }"
        "#openButton:pressed { background: rgba(61, 174, 233, 0.45); }"
        "#playlistPanel QPushButton { background: rgba(255,255,255,0.06); color: #eff0f1; "
        "border: none; border-radius: 6px; padding: 4px 8px; }"
        "#playlistPanel QPushButton:hover { background: rgba(255,255,255,0.12); }"
        "QListWidget { background: transparent; color: #eff0f1; border: none; outline: none; }"
        "QListWidget::item { padding: 7px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: rgba(61, 174, 233, 0.35); }"
        "QListWidget::item:hover { background: rgba(255,255,255,0.06); }"
        "#emptyTitle { color: #eff0f1; font-size: 28px; font-weight: 600; }"
        "#emptyHint { color: #a8adb2; font-size: 14px; }"
        "#emptyOpenButton { background: #3daee9; color: #172026; border: none; border-radius: 8px; "
        "padding: 10px 22px; font-weight: 600; }"
        "#emptyOpenButton:hover { background: #61bff2; }"
        "#osdLabel { background: rgba(20, 22, 24, 210); color: white; border-radius: 8px; "
        "padding: 10px 16px; font-size: 16px; }"));
}

void MainWindow::loadSettings()
{
    QSettings settings(ShortcutStore::settingsPath(), QSettings::IniFormat);
    lastDirectory_ = settings.value(QStringLiteral("lastDirectory")).toString();
    warnedQuality_ = settings.value(QStringLiteral("warnedQuality"), false).toBool();
    const QString language = settings.value(QStringLiteral("language")).toString();
    if (language == QLatin1String("en"))
        I18n::instance().setLanguage(I18n::Language::English);
    else if (language == QLatin1String("zh"))
        I18n::instance().setLanguage(I18n::Language::Chinese);
    const int volume = settings.value(QStringLiteral("volume"), 80).toInt();
    volumeSlider_->setValue(std::clamp(volume, 0, 100));
    player_->setVolume(volumeSlider_->value());
    player_->setMuted(settings.value(QStringLiteral("muted"), false).toBool());
    player_->setSpeed(settings.value(QStringLiteral("speed"), 1.0).toDouble());
    player_->setHwdecEnabled(settings.value(QStringLiteral("hwdec"), true).toBool());
    hwdecAction_->setChecked(settings.value(QStringLiteral("hwdec"), true).toBool());
    player_->setLoopMode(settings.value(QStringLiteral("loop"), QStringLiteral("off")).toString());
    const auto mode = static_cast<SuperResolution>(settings.value(QStringLiteral("superResolution"), 1).toInt());
    applyingSettings_ = true;
    setSuperResolution(mode == SuperResolution::Efficiency || mode == SuperResolution::Quality
                           ? mode
                           : SuperResolution::Off);
    applyingSettings_ = false;
    if (!testMode_ && settings.contains(QStringLiteral("geometry")))
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
    else
        resize(1100, 680);
    if (testMode_)
        resize(960, 640);
}

void MainWindow::saveSettings()
{
    if (testMode_)
        return;
    QSettings settings(ShortcutStore::settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("volume"), volumeSlider_->value());
    settings.setValue(QStringLiteral("muted"), player_->muted());
    settings.setValue(QStringLiteral("speed"), player_->speed());
    settings.setValue(QStringLiteral("hwdec"), hwdecAction_->isChecked());
    settings.setValue(QStringLiteral("loop"), player_->loopMode());
    settings.setValue(QStringLiteral("superResolution"), static_cast<int>(player_->superResolution()));
    settings.setValue(QStringLiteral("lastDirectory"), lastDirectory_);
    settings.setValue(QStringLiteral("warnedQuality"), warnedQuality_);
    settings.setValue(QStringLiteral("language"),
                      I18n::instance().language() == I18n::Language::English ? QStringLiteral("en")
                                                                            : QStringLiteral("zh"));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths << url.toLocalFile();
        else if (!url.scheme().isEmpty())
            paths << url.toString();
    }
    if (paths.isEmpty())
        return;
    openPaths(paths, !player_->path().isEmpty());
    event->acceptProposedAction();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto *button = qobject_cast<QAbstractButton *>(watched)) {
                if (button->isEnabled() && button->isVisible())
                    spawnRipple(button, mouse->position().toPoint());
            } else if (auto *slider = qobject_cast<QSlider *>(watched)) {
                if (slider->isEnabled() && slider->isVisible())
                    spawnRipple(slider, mouse->position().toPoint());
            }
        }
    }
    if (event->type() == QEvent::MouseMove && chromeAutohide_) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && (widget == this || isAncestorOf(widget)))
            noteActivity();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::openPaths(const QStringList &paths, bool append)
{
    if (paths.isEmpty())
        return;
    const QFileInfo info(paths.first());
    if (info.exists())
        lastDirectory_ = info.absolutePath();
    if (append)
        player_->openAppend(paths);
    else
        player_->openReplace(paths);
    showOsd(ui("正在打开…", "Opening…"));
    updatePlaylistWidget();
    updateEmptyState();
}

void MainWindow::openUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (!trimmed.isEmpty())
        openPaths({trimmed}, false);
}

void MainWindow::openFilesDialog(bool append)
{
    const QString filter = ui("视频和音频 (*.mp4 *.mkv *.webm *.avi *.mov *.flv *.m4v *.wmv "
                              "*.ts *.mpeg *.mpg *.m3u8 *.mp3 *.flac *.aac *.opus *.ogg);;所有文件 (*)",
                              "Video and audio (*.mp4 *.mkv *.webm *.avi *.mov *.flv *.m4v *.wmv "
                              "*.ts *.mpeg *.mpg *.m3u8 *.mp3 *.flac *.aac *.opus *.ogg);;All files (*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, append ? ui("添加到播放列表", "Add to Playlist") : ui("打开文件", "Open File"), lastDirectory_, filter);
    if (!paths.isEmpty())
        openPaths(paths, append);
}

void MainWindow::openUrlDialog()
{
    bool ok = false;
    const QString url = QInputDialog::getText(
        this, ui("打开地址", "Open URL"),
        ui("直接媒体地址（mp4、mkv、m3u8 等）", "Direct media URL (mp4, mkv, m3u8, and so on)"),
        QLineEdit::Normal, QString(), &ok);
    if (ok)
        openUrl(url);
}

void MainWindow::openSubtitleDialog()
{
    if (player_->path().isEmpty()) {
        showOsd(ui("请先打开视频", "Open a video first"));
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, ui("载入字幕", "Load Subtitle"), lastDirectory_,
        ui("字幕 (*.srt *.ass *.ssa *.vtt *.sub);;所有文件 (*)",
           "Subtitles (*.srt *.ass *.ssa *.vtt *.sub);;All files (*)"));
    if (!path.isEmpty()) {
        player_->addSubtitle(path);
        showOsd(ui("已载入字幕", "Subtitle loaded"));
    }
}

void MainWindow::showMediaInfo()
{
    if (player_->path().isEmpty()) {
        showOsd(ui("请先打开一个视频", "Open a video first"));
        return;
    }
    if (!mediaInfo_)
        mediaInfo_ = new MediaInfoDialog(this);
    mediaInfo_->show();
    mediaInfo_->raise();
    mediaInfo_->activateWindow();
    mediaInfo_->probe(player_->path());
}

void MainWindow::showSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(ui("设置", "Settings"));
    dialog.resize(520, 340);
    auto *layout = new QFormLayout(&dialog);
    auto *languageBox = new QComboBox(&dialog);
    languageBox->setObjectName(QStringLiteral("settingsLanguage"));
    languageBox->addItem(QStringLiteral("中文"), QStringLiteral("zh"));
    languageBox->addItem(QStringLiteral("English"), QStringLiteral("en"));
    languageBox->setCurrentIndex(I18n::instance().language() == I18n::Language::English ? 1 : 0);
    auto *sr = new QComboBox(&dialog);
    auto refreshSettings = [sr] {
        const int selected = sr->currentData().isValid() ? sr->currentData().toInt()
                                                         : static_cast<int>(SuperResolution::Off);
        QSignalBlocker blocker(sr);
        sr->clear();
        sr->addItem(ui("关闭", "Off"), static_cast<int>(SuperResolution::Off));
        sr->addItem(ui("效率档（Kazumi 轻量链）", "Efficiency (Kazumi light chain)"),
                    static_cast<int>(SuperResolution::Efficiency));
        sr->addItem(ui("质量档（Anime4K Mode A HQ）", "Quality (Anime4K Mode A HQ)"),
                    static_cast<int>(SuperResolution::Quality));
        const int index = sr->findData(selected);
        sr->setCurrentIndex(index < 0 ? 0 : index);
    };
    refreshSettings();
    {
        QSignalBlocker blocker(sr);
        const int index = sr->findData(static_cast<int>(player_->superResolution()));
        if (index >= 0)
            sr->setCurrentIndex(index);
    }
    auto *hw = new QCheckBox(&dialog);
    hw->setChecked(hwdecAction_->isChecked());
    auto *note = new QLabel(&dialog);
    note->setWordWrap(true);
    auto *languageCaption = new QLabel(&dialog);
    auto *srCaption = new QLabel(&dialog);
    auto *close = new QPushButton(&dialog);
    auto applySettingsText = [languageCaption, srCaption, hw, note, close, &dialog] {
        dialog.setWindowTitle(ui("设置", "Settings"));
        languageCaption->setText(ui("语言", "Language"));
        srCaption->setText(ui("超分辨率", "Super Resolution"));
        hw->setText(ui("硬件解码（auto-copy，着色器可用）",
                       "Hardware decoding (auto-copy, required for shaders)"));
        note->setText(ui("超分辨率使用与 Kazumi 相同的 Anime4K 着色器链，在 mpv 的 OpenGL 视频输出里逐帧运行。"
                         "质量档更重。媒体信息由 ffprobe 读取。语言会立即切换。",
                         "Super resolution uses the same Anime4K shader chains as Kazumi and runs on "
                         "every frame in mpv's OpenGL output. Quality is heavier. Media details come "
                         "from ffprobe. The language switch applies immediately."));
        close->setText(ui("关闭", "Close"));
    };
    applySettingsText();
    connect(&I18n::instance(), &I18n::languageChanged, &dialog, [applySettingsText, refreshSettings] {
        applySettingsText();
        refreshSettings();
    });
    layout->addRow(languageCaption, languageBox);
    layout->addRow(srCaption, sr);
    layout->addRow(QString(), hw);
    layout->addRow(note);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(close);
    layout->addRow(buttons);
    connect(languageBox, &QComboBox::currentIndexChanged, &dialog, [languageBox](int index) {
        const QString code = languageBox->itemData(index).toString();
        I18n::instance().setLanguage(code == QLatin1String("en") ? I18n::Language::English
                                                                 : I18n::Language::Chinese);
    });
    connect(sr, &QComboBox::currentIndexChanged, this, [this, sr](int index) {
        setSuperResolution(static_cast<SuperResolution>(sr->itemData(index).toInt()));
    });
    connect(hw, &QCheckBox::toggled, hwdecAction_, &QAction::setChecked);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::showShortcutDialog()
{
    ShortcutDialog dialog(this);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, ui("关于 AniPlayer", "About AniPlayer"),
        ui("AniPlayer 是一个 Linux 播放器，界面参照 Haruna：顶部标题栏、"
           "悬浮控制条和覆盖在画面上的播放列表。\n\n"
           "播放由 libmpv 完成。超分辨率直接使用 Anime4K 的 GLSL 着色器，"
           "效率档和质量档与 Kazumi 的预设一致。\n\n"
           "视频和音频详细信息通过 ffprobe 读取。界面可以在中文和 English 之间切换。\n\n"
           "Anime4K © bloc97，MIT / Unlicense。",
           "AniPlayer is a Linux player with a Haruna-style layout: a header bar, "
           "a floating control bar, and a playlist over the video.\n\n"
           "Playback uses libmpv. Super resolution runs Anime4K GLSL shaders, and the "
           "efficiency and quality presets match Kazumi.\n\n"
           "Video and audio details come from ffprobe. The interface can switch between "
           "Chinese and English.\n\n"
           "Anime4K © bloc97, MIT / Unlicense."));
}

void MainWindow::togglePlaylist(bool visible)
{
    playlistVisible_ = visible;
    if (playlistButton_->isChecked() != visible) {
        QSignalBlocker blocker(playlistButton_);
        playlistButton_->setChecked(visible);
    }
    playlistPanel_->setVisible(visible);
    layoutOverlays();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
        header_->show();
        setThemeIcon(fullscreenButton_, QStringLiteral("view-fullscreen"), QStringLiteral("⛶"));
        fullscreenButton_->setToolTip(ui("全屏", "Full Screen"));
    } else {
        header_->hide();
        showFullScreen();
        setThemeIcon(fullscreenButton_, QStringLiteral("view-restore"), QStringLiteral("🡼"));
        fullscreenButton_->setToolTip(ui("退出全屏", "Exit Full Screen"));
    }
    showChrome();
    layoutOverlays();
}

void MainWindow::togglePlayback()
{
    const bool pausing = !player_->paused();
    player_->togglePause();
    showOsd(pausing ? ui("已暂停", "Paused") : ui("播放", "Playing"));
    if (sender() != playButton_)
        flash(playButton_);
}

void MainWindow::updatePlayIcon()
{
    const bool showPlay = player_->paused() || player_->path().isEmpty();
    setThemeIcon(playButton_, showPlay ? QStringLiteral("media-playback-start")
                                       : QStringLiteral("media-playback-pause"),
                 showPlay ? QStringLiteral("▶") : QStringLiteral("⏸"));
    playButton_->setToolTip(showPlay ? ui("播放", "Play") : ui("暂停", "Pause"));
    setThemeIcon(muteButton_, player_->muted() || volumeSlider_->value() == 0
                                  ? QStringLiteral("player-volume-muted")
                                  : QStringLiteral("player-volume"),
                 player_->muted() ? ui("静", "M") : ui("音", "V"));
    muteButton_->setToolTip(ui("静音", "Mute"));
}

void MainWindow::updateTimeLabel()
{
    if (!seeking_) {
        QSignalBlocker blocker(seekSlider_);
        seekSlider_->setValue(static_cast<int>(std::lround(player_->position() * 1000.0)));
    }
    timeLabel_->setText(formatTime(player_->position()) + QStringLiteral(" / ") + formatTime(player_->duration()));
}

void MainWindow::updateTitle()
{
    const QString title = player_->mediaTitle().isEmpty() ? QFileInfo(player_->path()).fileName()
                                                          : player_->mediaTitle();
    titleText_ = title.isEmpty() ? QStringLiteral("AniPlayer") : title;
    setWindowTitle(title.isEmpty() ? QStringLiteral("AniPlayer")
                                   : title + QStringLiteral(" — AniPlayer"));
    applyTitleElide();
}

void MainWindow::applyTitleElide()
{
    const auto elide = [](QLabel *label, const QString &text) {
        if (!label)
            return;
        const int width = label->contentsRect().width();
        label->setToolTip(text == QStringLiteral("AniPlayer") ? QString() : text);
        label->setText(width < 12 ? text : label->fontMetrics().elidedText(text, Qt::ElideRight, width));
    };
    elide(titleLabel_, titleText_.isEmpty() ? QStringLiteral("AniPlayer") : titleText_);
    elide(footerTitle_, titleText_);
}

void MainWindow::updatePlaylistWidget()
{
    const int previousRow = playlistView_->currentRow();
    playlistView_->clear();
    int current = -1;
    const QVector<PlaylistItem> items = player_->playlist();
    for (int i = 0; i < items.size(); ++i) {
        auto *item = new QListWidgetItem((items.at(i).current ? QStringLiteral("▶ ") : QString())
                                         + items.at(i).title);
        item->setToolTip(items.at(i).path);
        playlistView_->addItem(item);
        if (items.at(i).current)
            current = i;
    }
    if (current >= 0)
        playlistView_->setCurrentRow(current);
    else if (previousRow >= 0 && previousRow < playlistView_->count())
        playlistView_->setCurrentRow(previousRow);
    const bool multi = items.size() > 1;
    prevButton_->setEnabled(multi);
    nextButton_->setEnabled(multi);
}

void MainWindow::updateTrackMenus()
{
    if (!audioMenu_ || !subtitleMenu_)
        return;
    fillTrackMenu(audioMenu_, QStringLiteral("audio"));
    fillTrackMenu(subtitleMenu_, QStringLiteral("sub"));
    updateModeButtons();
}

void MainWindow::fillTrackMenu(QMenu *menu, const QString &type)
{
    menu->clear();
    const bool audio = type == QLatin1String("audio");
    bool any = false;
    if (!audio) {
        auto *disable = menu->addAction(ui("关闭字幕", "Disable subtitles"));
        connect(disable, &QAction::triggered, this, [this] {
            player_->setSubtitleTrack(-1);
            showOsd(ui("字幕已关闭", "Subtitles off"));
            flash(subtitleButton_);
        });
        menu->addSeparator();
    }
    for (const MediaTrack &track : player_->tracks()) {
        if (track.type != type)
            continue;
        any = true;
        auto *action = menu->addAction(track.label);
        action->setCheckable(true);
        action->setChecked(track.selected);
        const int id = track.id;
        const QString label = track.label;
        if (audio) {
            connect(action, &QAction::triggered, this, [this, id, label] {
                player_->setAudioTrack(id);
                showOsd(ui("音轨：%1", "Audio: %1").arg(label));
                flash(audioButton_);
            });
        } else {
            connect(action, &QAction::triggered, this, [this, id, label] {
                player_->setSubtitleTrack(id);
                showOsd(ui("字幕：%1", "Subtitle: %1").arg(label));
                flash(subtitleButton_);
            });
        }
    }
    if (!any)
        menu->addAction(audio ? ui("没有音轨", "No audio tracks") : ui("没有字幕", "No subtitles"))->setEnabled(false);
}

void MainWindow::markActive(QWidget *widget, bool active)
{
    if (!widget || widget->property("active").toBool() == active)
        return;
    widget->setProperty("active", active);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

void MainWindow::flash(QWidget *widget)
{
    if (!widget || !widget->isVisible())
        return;
    spawnRipple(widget, widget->rect().center());
}

QString MainWindow::loopLabel() const
{
    const QString mode = player_->loopMode();
    if (mode == QLatin1String("file"))
        return ui("单曲 ▾", "File ▾");
    if (mode == QLatin1String("playlist"))
        return ui("列表 ▾", "List ▾");
    return ui("不循环 ▾", "No loop ▾");
}

void MainWindow::updateModeButtons()
{
    if (!player_ || !srButton_)
        return;
    const SuperResolution mode = player_->superResolution();
    srButton_->setText(superResolutionLabel(mode));
    markActive(srButton_, mode != SuperResolution::Off);
    switch (mode) {
    case SuperResolution::Efficiency:
        srButton_->setToolTip(ui("超分辨率：效率档", "Super resolution: Efficiency"));
        break;
    case SuperResolution::Quality:
        srButton_->setToolTip(ui("超分辨率：质量档", "Super resolution: Quality"));
        break;
    case SuperResolution::Off:
        srButton_->setToolTip(ui("超分辨率：关闭", "Super resolution: Off"));
        break;
    }
    if (srOffAction_ && srEfficiencyAction_ && srQualityAction_) {
        QSignalBlocker off(srOffAction_);
        QSignalBlocker efficiency(srEfficiencyAction_);
        QSignalBlocker quality(srQualityAction_);
        srOffAction_->setChecked(mode == SuperResolution::Off);
        srEfficiencyAction_->setChecked(mode == SuperResolution::Efficiency);
        srQualityAction_->setChecked(mode == SuperResolution::Quality);
    }

    if (speedButton_) {
        speedButton_->setText(QString::number(player_->speed(), 'g', 3) + QStringLiteral("× ▾"));
        markActive(speedButton_, std::abs(player_->speed() - 1.0) > 0.01);
    }
    if (auto *speedMenu = findChild<QMenu *>(QStringLiteral("speedMenu"))) {
        for (QAction *action : speedMenu->actions()) {
            QSignalBlocker blocker(action);
            action->setChecked(std::abs(action->data().toDouble() - player_->speed()) < 0.05);
        }
    }

    const QString loop = player_->loopMode();
    if (loopButton_) {
        loopButton_->setText(loopLabel());
        markActive(loopButton_, loop != QLatin1String("off"));
        if (loop == QLatin1String("file"))
            loopButton_->setToolTip(ui("循环：单个文件", "Loop: one file"));
        else if (loop == QLatin1String("playlist"))
            loopButton_->setToolTip(ui("循环：整个列表", "Loop: playlist"));
        else
            loopButton_->setToolTip(ui("循环：关闭", "Loop: off"));
    }
    auto checkLoop = [this, &loop](const char *name, const char *expected) {
        if (auto *action = findChild<QAction *>(QLatin1String(name))) {
            QSignalBlocker blocker(action);
            action->setChecked(loop == QLatin1String(expected));
        }
    };
    checkLoop("actionLoopOff", "off");
    checkLoop("actionLoopFile", "file");
    checkLoop("actionLoopList", "playlist");

    if (audioButton_)
        audioButton_->setText(ui("音轨 ▾", "Audio ▾"));
    if (subtitleButton_)
        subtitleButton_->setText(ui("字幕 ▾", "Sub ▾"));
    bool subtitleOn = false;
    for (const MediaTrack &track : player_->tracks()) {
        if (track.type == QLatin1String("sub") && track.selected)
            subtitleOn = true;
    }
    markActive(subtitleButton_, subtitleOn);
}

void MainWindow::updateEmptyState()
{
    const bool empty = player_->path().isEmpty();
    emptyState_->setVisible(empty);
    layoutOverlays();
}

void MainWindow::showOsd(const QString &text)
{
    if (!osd_ || !osdTimer_)
        return;
    osd_->setText(text);
    osd_->adjustSize();
    layoutOverlays();
    osd_->show();
    osd_->raise();
    osdTimer_->start(1600);
}

void MainWindow::noteActivity()
{
    showChrome();
    if (chromeAutohide_)
        hideTimer_->start();
}

void MainWindow::showChrome()
{
    footer_->setVisible(true);
    if (isFullScreen())
        unsetCursor();
    layoutOverlays();
}

void MainWindow::hideChrome()
{
    if (!chromeAutohide_ || footer_->underMouse() || header_->underMouse() || playlistPanel_->underMouse()
        || QApplication::activePopupWidget())
        return;
    footer_->setVisible(false);
    if (isFullScreen())
        setCursor(Qt::BlankCursor);
}

void MainWindow::layoutOverlays()
{
    if (!surface_)
        return;
    const QRect area = surface_->rect();
    player_->setGeometry(area);
    footer_->setFixedWidth(std::max(280, area.width() - 16));
    footer_->adjustSize();
    footer_->move(8, area.height() - footer_->height() - 8);
    applyTitleElide();
    const int playlistTop = 8;
    const int playlistBottom = footer_->isVisible() ? footer_->y() - 8 : area.height() - 8;
    playlistPanel_->setGeometry(area.width() - 328, playlistTop, 320, std::max(120, playlistBottom - playlistTop));
    emptyState_->adjustSize();
    emptyState_->move((area.width() - emptyState_->width()) / 2, (area.height() - emptyState_->height()) / 2);
    osd_->adjustSize();
    osd_->move((area.width() - osd_->width()) / 2, area.height() / 2 - 48);
    if (playlistVisible_)
        playlistPanel_->raise();
    if (emptyState_->isVisible())
        emptyState_->raise();
    footer_->raise();
    osd_->raise();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    layoutOverlays();
}

void MainWindow::retranslate()
{
    auto setAction = [this](const char *name, const char *zh, const char *en) {
        if (auto *action = findChild<QAction *>(QLatin1String(name)))
            action->setText(ui(zh, en));
    };
    auto setMenu = [this](const char *name, const char *zh, const char *en) {
        if (auto *menu = findChild<QMenu *>(QLatin1String(name)))
            menu->setTitle(ui(zh, en));
    };
    auto setTip = [this](const char *name, const char *zh, const char *en) {
        if (auto *button = findChild<QAbstractButton *>(QLatin1String(name)))
            button->setToolTip(ui(zh, en));
    };
    auto setButtonText = [this](const char *name, const char *zh, const char *en) {
        if (auto *button = findChild<QAbstractButton *>(QLatin1String(name)))
            button->setText(ui(zh, en));
    };

    setAction("actionOpen", "打开文件…", "Open File…");
    setAction("actionAppend", "添加到播放列表…", "Add to Playlist…");
    setAction("actionOpenUrl", "打开地址…", "Open URL…");
    setAction("actionPlayPause", "播放/暂停", "Play/Pause");
    setAction("actionPrevious", "上一个", "Previous");
    setAction("actionNext", "下一个", "Next");
    setAction("actionSeekBack", "后退 5 秒", "Seek Backward 5 Seconds");
    setAction("actionSeekForward", "前进 5 秒", "Seek Forward 5 Seconds");
    setAction("actionMute", "静音", "Mute");
    setAction("actionPlaylist", "播放列表", "Playlist");
    setAction("actionFullscreen", "全屏", "Full Screen");
    setAction("actionMediaInfo", "媒体信息", "Media Info");
    setAction("actionSubtitleFile", "载入字幕…", "Load Subtitle…");
    setAction("actionShortcuts", "快捷键…", "Shortcuts…");
    setAction("actionSettings", "设置…", "Settings…");
    setAction("actionAbout", "关于", "About");
    setAction("actionQuit", "退出", "Quit");
    setAction("actionHwdec", "硬件解码", "Hardware Decoding");
    setAction("actionSrOff", "关闭", "Off");
    setAction("actionSrEfficiency", "效率档", "Efficiency");
    setAction("actionSrQuality", "质量档", "Quality");
    setAction("actionLoopOff", "关闭", "Off");
    setAction("actionLoopFile", "单个文件", "Single File");
    setAction("actionLoopList", "整个列表", "Whole Playlist");
    setMenu("srMenu", "超分辨率", "Super Resolution");
    setMenu("speedMenu", "倍速", "Speed");
    setMenu("loopMenu", "循环", "Loop");
    setMenu("audioMenu", "音轨", "Audio Track");
    setMenu("subtitleMenu", "字幕", "Subtitles");
    setMenu("languageMenu", "语言", "Language");
    setTip("menuButton", "文件和设置", "File and settings");
    setTip("openButton", "打开文件", "Open File");
    setButtonText("openButton", "打开", "Open");
    setTip("playlistButton", "播放列表", "Playlist");
    setTip("speedButton", "倍速", "Speed");
    setTip("audioButton", "音轨", "Audio Track");
    setTip("subtitleButton", "字幕", "Subtitles");
    setTip("prevButton", "上一个", "Previous");
    setTip("nextButton", "下一个", "Next");
    setButtonText("emptyOpenButton", "打开文件", "Open File");
    setButtonText("playlistAddButton", "添加", "Add");
    setButtonText("playlistRemoveButton", "移除", "Remove");
    setButtonText("playlistClearButton", "清空", "Clear");
    if (auto *hint = findChild<QLabel *>(QStringLiteral("emptyHint"))) {
        hint->setText(ui("把视频拖到这里，或打开一个文件", "Drop a video here, or open a file"));
    }
    if (auto *playlistTitle = findChild<QLabel *>(QStringLiteral("sectionLabel")))
        playlistTitle->setText(ui("播放列表", "Playlist"));
    if (auto *info = findChild<QAbstractButton *>(QStringLiteral("mediaInfoButton"))) {
        info->setToolTip(ui("媒体信息", "Media Info"));
        if (info->icon().isNull())
            info->setText(ui("信息", "Info"));
    }
    if (auto *languageButton = findChild<QAbstractButton *>(QStringLiteral("languageButton"))) {
        const bool english = I18n::instance().language() == I18n::Language::English;
        languageButton->setText(english ? QStringLiteral("EN") : QStringLiteral("中文"));
        languageButton->setToolTip(ui("切换语言", "Switch language"));
    }
    if (auto *chinese = findChild<QAction *>(QStringLiteral("actionLangZh"))) {
        QSignalBlocker blocker(chinese);
        chinese->setChecked(I18n::instance().language() == I18n::Language::Chinese);
    }
    if (auto *english = findChild<QAction *>(QStringLiteral("actionLangEn"))) {
        QSignalBlocker blocker(english);
        english->setChecked(I18n::instance().language() == I18n::Language::English);
    }
    updateModeButtons();
    updatePlayIcon();
    updateTrackMenus();
    fullscreenButton_->setToolTip(isFullScreen() ? ui("退出全屏", "Exit Full Screen")
                                                 : ui("全屏", "Full Screen"));
}

QString MainWindow::formatTime(double seconds) const
{
    if (!std::isfinite(seconds) || seconds < 0)
        seconds = 0;
    const int total = static_cast<int>(seconds + 0.5);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    if (hours > 0 || player_->duration() >= 3600.0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(secs, 2, 10, QChar('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
}

QString MainWindow::superResolutionLabel(SuperResolution mode) const
{
    switch (mode) {
    case SuperResolution::Efficiency:
        return ui("效率 ▾", "Efficiency ▾");
    case SuperResolution::Quality:
        return ui("质量 ▾", "Quality ▾");
    case SuperResolution::Off:
        break;
    }
    return ui("超分 ▾", "SR ▾");
}

void MainWindow::setSuperResolution(SuperResolution mode)
{
    if (mode == SuperResolution::Quality && !warnedQuality_ && !testMode_ && !applyingSettings_) {
        warnedQuality_ = true;
        showOsd(ui("质量档对 GPU 要求较高", "Quality mode is demanding on the GPU"));
    }
    player_->setSuperResolution(mode);
    updateModeButtons();
    if (!applyingSettings_) {
        flash(srButton_);
        if (mode == SuperResolution::Efficiency)
            showOsd(ui("超分辨率：效率档", "Super resolution: Efficiency"));
        else if (mode == SuperResolution::Quality)
            showOsd(ui("超分辨率：质量档", "Super resolution: Quality"));
        else
            showOsd(ui("超分辨率已关闭", "Super resolution off"));
    }
}

void MainWindow::setChromeAutohide(bool enabled)
{
    chromeAutohide_ = enabled;
    if (!enabled)
        showChrome();
}
