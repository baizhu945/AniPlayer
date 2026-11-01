#include "mediainfo.h"

#include "i18n.h"

#include <QFormLayout>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QString jsonText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (qFuzzyCompare(number + 1.0, qRound(number) + 1.0))
            return QString::number(qRound(number));
        return QString::number(number, 'f', 3);
    }
    if (value.isBool())
        return value.toBool() ? ui("是", "Yes") : ui("否", "No");
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        QStringList parts;
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString text = jsonText(it.value());
            if (!text.isEmpty())
                parts << it.key() + QStringLiteral("=") + text;
        }
        return parts.join(QStringLiteral(", "));
    }
    return {};
}

QString field(const QJsonObject &object, const char *key)
{
    return jsonText(object.value(QLatin1String(key)));
}

QString humanRate(const QString &fraction)
{
    const int slash = fraction.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return fraction;
    bool okA = false;
    bool okB = false;
    const double numerator = fraction.left(slash).toDouble(&okA);
    const double denominator = fraction.mid(slash + 1).toDouble(&okB);
    if (!okA || !okB || denominator == 0.0)
        return fraction;
    return QString::number(numerator / denominator, 'f', 3) + QStringLiteral(" fps");
}

QString humanBitrate(const QString &bitsPerSecond)
{
    bool ok = false;
    const double value = bitsPerSecond.toDouble(&ok);
    if (!ok || value <= 0)
        return {};
    if (value >= 1000000.0)
        return QString::number(value / 1000000.0, 'f', 2) + QStringLiteral(" Mb/s");
    if (value >= 1000.0)
        return QString::number(value / 1000.0, 'f', 0) + QStringLiteral(" kb/s");
    return QString::number(value, 'f', 0) + QStringLiteral(" b/s");
}

QString humanBytes(const QString &bytesText)
{
    bool ok = false;
    const double value = bytesText.toDouble(&ok);
    if (!ok || value < 0)
        return {};
    if (value >= 1073741824.0)
        return QString::number(value / 1073741824.0, 'f', 2) + QStringLiteral(" GiB");
    if (value >= 1048576.0)
        return QString::number(value / 1048576.0, 'f', 2) + QStringLiteral(" MiB");
    if (value >= 1024.0)
        return QString::number(value / 1024.0, 'f', 1) + QStringLiteral(" KiB");
    return QString::number(value, 'f', 0) + QStringLiteral(" B");
}

QString humanDuration(const QString &secondsText)
{
    bool ok = false;
    const double seconds = secondsText.toDouble(&ok);
    if (!ok || seconds < 0)
        return {};
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

void add(QVector<MediaField> *fields, const QString &label, const QString &value)
{
    if (!value.isEmpty())
        fields->append({label, value});
}

QString languageOf(const QJsonObject &object)
{
    const QJsonObject tags = object.value(QStringLiteral("tags")).toObject();
    QString language = jsonText(tags.value(QStringLiteral("language")));
    if (language.isEmpty())
        language = jsonText(tags.value(QStringLiteral("lang")));
    if (language == QLatin1String("und"))
        return {};
    return language;
}

MediaSection videoSection(const QJsonObject &stream, int order)
{
    MediaSection section;
    const bool attached = stream.value(QStringLiteral("disposition")).toObject()
                              .value(QStringLiteral("attached_pic")).toInt() == 1;
    section.heading = attached ? ui("附加图片 %1", "Attached picture %1").arg(order)
                               : ui("视频流 %1", "Video stream %1").arg(order);
    const QString resolution = field(stream, "width").isEmpty()
                                   ? QString()
                                   : field(stream, "width") + QStringLiteral(" × ") + field(stream, "height");
    QStringList colorParts;
    for (const char *key : {"color_space", "color_primaries", "color_transfer"}) {
        const QString part = field(stream, key);
        if (!part.isEmpty())
            colorParts << part;
    }
    const QString color = colorParts.join(QStringLiteral(" / "));
    add(&section.fields, ui("编码器", "Encoder"), field(stream, "codec_long_name"));
    add(&section.fields, ui("编码", "Codec"), field(stream, "codec_name"));
    add(&section.fields, QStringLiteral("Profile"), field(stream, "profile"));
    add(&section.fields, QStringLiteral("Level"), field(stream, "level"));
    add(&section.fields, ui("分辨率", "Resolution"), resolution);
    add(&section.fields, ui("编码分辨率", "Coded resolution"),
        field(stream, "coded_width").isEmpty()
            ? QString()
            : field(stream, "coded_width") + QStringLiteral(" × ") + field(stream, "coded_height"));
    add(&section.fields, ui("显示宽高比", "Display aspect ratio"), field(stream, "display_aspect_ratio"));
    add(&section.fields, ui("像素宽高比", "Pixel aspect ratio"), field(stream, "sample_aspect_ratio"));
    add(&section.fields, ui("像素格式", "Pixel format"), field(stream, "pix_fmt"));
    add(&section.fields, ui("位深", "Bit depth"), field(stream, "bits_per_raw_sample"));
    add(&section.fields, ui("帧率", "Frame rate"), humanRate(field(stream, "avg_frame_rate")));
    add(&section.fields, ui("原始帧率", "Raw frame rate"), humanRate(field(stream, "r_frame_rate")));
    add(&section.fields, ui("码率", "Bit rate"), humanBitrate(field(stream, "bit_rate")));
    add(&section.fields, ui("时长", "Duration"), humanDuration(field(stream, "duration")));
    add(&section.fields, ui("帧数", "Frames"), field(stream, "nb_frames"));
    add(&section.fields, ui("扫描", "Scan"), field(stream, "field_order"));
    add(&section.fields, ui("色度位置", "Chroma location"), field(stream, "chroma_location"));
    add(&section.fields, ui("色彩", "Color"), color);
    add(&section.fields, ui("色彩范围", "Color range"), field(stream, "color_range"));
    add(&section.fields, ui("标签", "Tag"), field(stream, "codec_tag_string"));
    add(&section.fields, QStringLiteral("MIME"), field(stream, "mime_codec_string"));
    add(&section.fields, ui("语言", "Language"), languageOf(stream));
    const QString encoder = jsonText(stream.value(QStringLiteral("tags")).toObject().value(QStringLiteral("encoder")));
    add(&section.fields, ui("编码库", "Encoder library"), encoder);
    return section;
}

MediaSection audioSection(const QJsonObject &stream, int order)
{
    MediaSection section;
    section.heading = ui("音频流 %1", "Audio stream %1").arg(order);
    const QString channels = field(stream, "channels");
    const QString layout = field(stream, "channel_layout");
    const QString channelText = channels.isEmpty()
                                    ? layout
                                    : channels + (layout.isEmpty() ? QString() : ui("（%1）", " (%1)").arg(layout));
    add(&section.fields, ui("编码器", "Encoder"), field(stream, "codec_long_name"));
    add(&section.fields, ui("编码", "Codec"), field(stream, "codec_name"));
    add(&section.fields, QStringLiteral("Profile"), field(stream, "profile"));
    add(&section.fields, ui("采样格式", "Sample format"), field(stream, "sample_fmt"));
    add(&section.fields, ui("采样率", "Sample rate"),
        field(stream, "sample_rate").isEmpty() ? QString()
                                               : field(stream, "sample_rate") + QStringLiteral(" Hz"));
    add(&section.fields, ui("声道", "Channels"), channelText);
    add(&section.fields, ui("位深", "Bit depth"), field(stream, "bits_per_sample"));
    add(&section.fields, ui("码率", "Bit rate"), humanBitrate(field(stream, "bit_rate")));
    add(&section.fields, ui("时长", "Duration"), humanDuration(field(stream, "duration")));
    add(&section.fields, ui("帧数", "Frames"), field(stream, "nb_frames"));
    add(&section.fields, ui("标签", "Tag"), field(stream, "codec_tag_string"));
    add(&section.fields, QStringLiteral("MIME"), field(stream, "mime_codec_string"));
    add(&section.fields, ui("语言", "Language"), languageOf(stream));
    return section;
}

void paintDark(QWidget *widget)
{
    widget->setAutoFillBackground(true);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, QColor(0x23, 0x26, 0x29));
    palette.setColor(QPalette::Base, QColor(0x23, 0x26, 0x29));
    palette.setColor(QPalette::WindowText, QColor(0xf4, 0xf6, 0xf8));
    palette.setColor(QPalette::Text, QColor(0xf4, 0xf6, 0xf8));
    widget->setPalette(palette);
}

QWidget *buildOverview(const MediaDetails &details)
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("infoPage"));
    paintDark(page);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    auto addSection = [layout](const QString &title, const QVector<MediaField> &fields) {
        if (fields.isEmpty())
            return;
        auto *heading = new QLabel(title);
        heading->setObjectName(QStringLiteral("sectionHeading"));
        heading->setStyleSheet(QStringLiteral(
            "color: #3daee9; background: transparent; font-weight: 600; font-size: 15px;"));
        layout->addWidget(heading);
        auto *formHost = new QWidget;
        formHost->setObjectName(QStringLiteral("infoForm"));
        paintDark(formHost);
        auto *form = new QFormLayout(formHost);
        form->setContentsMargins(0, 0, 0, 0);
        form->setHorizontalSpacing(18);
        form->setVerticalSpacing(6);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
        for (const MediaField &field : fields) {
            auto *name = new QLabel(field.label);
            name->setObjectName(QStringLiteral("fieldLabel"));
            name->setStyleSheet(QStringLiteral("color: #b7c0c7; background: transparent;"));
            auto *value = new QLabel(field.value);
            value->setObjectName(QStringLiteral("fieldValue"));
            value->setStyleSheet(QStringLiteral("color: #f4f6f8; background: transparent;"));
            value->setWordWrap(true);
            value->setTextInteractionFlags(Qt::TextSelectableByMouse);
            form->addRow(name, value);
        }
        layout->addWidget(formHost);
    };

    addSection(ui("文件", "File"), details.container);
    for (const MediaSection &section : details.videos)
        addSection(section.heading, section.fields);
    for (const MediaSection &section : details.audios)
        addSection(section.heading, section.fields);
    for (const MediaSection &section : details.others)
        addSection(section.heading, section.fields);
    addSection(ui("章节", "Chapters"), details.chapters);
    if (details.videos.isEmpty() && details.audios.isEmpty()) {
        auto *empty = new QLabel(ui("没有找到视频或音频流", "No video or audio streams found"));
        empty->setObjectName(QStringLiteral("fieldLabel"));
        layout->addWidget(empty);
    }
    layout->addStretch(1);
    return page;
}

} // namespace

QString MediaProbe::executable()
{
    const QString overridePath = qEnvironmentVariable("ANIPLAYER_FFPROBE");
    if (!overridePath.isEmpty())
        return overridePath;
    return QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
}

MediaDetails MediaProbe::inspect(const QString &mediaPath, int timeoutMs)
{
    MediaDetails details;
    const QString program = executable();
    if (program.isEmpty()) {
        details.error = ui("找不到 ffprobe。请确认它在 PATH 中。",
                           "ffprobe was not found. Make sure it is on PATH.");
        return details;
    }
    if (mediaPath.isEmpty()) {
        details.error = ui("没有正在播放的文件", "Nothing is playing");
        return details;
    }

    QProcess process;
    process.start(program,
                  {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                   QStringLiteral("-print_format"), QStringLiteral("json"), QStringLiteral("-show_format"),
                   QStringLiteral("-show_streams"), QStringLiteral("-show_chapters"), mediaPath});
    if (!process.waitForStarted(3000)) {
        details.error = ui("无法启动 ffprobe", "Could not start ffprobe");
        return details;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        details.error = ui("ffprobe 超时", "ffprobe timed out");
        return details;
    }

    const QByteArray output = process.readAllStandardOutput();
    const QString errors = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        details.error = errors.isEmpty() ? ui("ffprobe 读取失败", "ffprobe failed") : errors;
        return details;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        details.error = ui("无法解析 ffprobe 输出：%1", "Could not parse ffprobe output: %1")
                            .arg(parseError.errorString());
        return details;
    }
    details.rawJson = QString::fromUtf8(document.toJson(QJsonDocument::Indented));
    const QJsonObject root = document.object();
    const QJsonObject format = root.value(QStringLiteral("format")).toObject();

    add(&details.container, ui("路径", "Path"), field(format, "filename"));
    const QString containerName = field(format, "format_long_name");
    const QString containerShort = field(format, "format_name");
    add(&details.container, ui("容器", "Container"),
        containerName.isEmpty() ? containerShort
                                : containerName + ui("（%1）", " (%1)").arg(containerShort));
    add(&details.container, ui("时长", "Duration"), humanDuration(field(format, "duration")));
    add(&details.container, ui("大小", "Size"), humanBytes(field(format, "size")));
    add(&details.container, ui("总码率", "Overall bit rate"), humanBitrate(field(format, "bit_rate")));
    add(&details.container, ui("流数量", "Streams"), field(format, "nb_streams"));
    const QJsonObject formatTags = format.value(QStringLiteral("tags")).toObject();
    add(&details.container, ui("标题", "Title"), jsonText(formatTags.value(QStringLiteral("title"))));
    add(&details.container, ui("创建时间", "Created"), jsonText(formatTags.value(QStringLiteral("creation_time"))));

    int videoOrder = 0;
    int audioOrder = 0;
    int otherOrder = 0;
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        const QString type = field(stream, "codec_type");
        if (type == QLatin1String("video"))
            details.videos.append(videoSection(stream, ++videoOrder));
        else if (type == QLatin1String("audio"))
            details.audios.append(audioSection(stream, ++audioOrder));
        else {
            MediaSection section;
            section.heading = ui("其他流 %1（%2）", "Other stream %1 (%2)").arg(++otherOrder).arg(type);
            add(&section.fields, ui("编码", "Codec"), field(stream, "codec_long_name"));
            add(&section.fields, ui("标签", "Tag"), field(stream, "codec_tag_string"));
            details.others.append(section);
        }
    }

    const QJsonArray chapters = root.value(QStringLiteral("chapters")).toArray();
    int chapterIndex = 0;
    for (const QJsonValue &value : chapters) {
        const QJsonObject chapter = value.toObject();
        ++chapterIndex;
        const QString title = jsonText(chapter.value(QStringLiteral("tags")).toObject().value(QStringLiteral("title")));
        const QString start = humanDuration(field(chapter, "start_time"));
        add(&details.chapters, ui("章节 %1", "Chapter %1").arg(chapterIndex),
            (title.isEmpty() ? ui("未命名", "Untitled") : title)
                + (start.isEmpty() ? QString() : QStringLiteral("  ·  %1").arg(start)));
    }
    return details;
}

MediaInfoDialog::MediaInfoDialog(QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("mediaInfoDialog"));
    setWindowTitle(ui("媒体信息", "Media Info"));
    resize(760, 560);
    setMinimumSize(520, 360);

    auto *layout = new QVBoxLayout(this);
    status_ = new QLabel(ui("正在读取…", "Reading…"));
    status_->setObjectName(QStringLiteral("fieldLabel"));
    status_->setWordWrap(true);
    layout->addWidget(status_);

    tabs_ = new QTabWidget;
    overview_ = new QWidget;
    auto *overviewLayout = new QVBoxLayout(overview_);
    overviewLayout->setContentsMargins(0, 0, 0, 0);
    auto *placeholder = new QWidget;
    overviewLayout->addWidget(placeholder);
    tabs_->addTab(overview_, ui("概览", "Overview"));

    raw_ = new QPlainTextEdit;
    raw_->setReadOnly(true);
    raw_->setObjectName(QStringLiteral("rawProbe"));
    tabs_->addTab(raw_, ui("原始数据", "Raw data"));
    layout->addWidget(tabs_, 1);
    tabs_->hide();

    connect(&I18n::instance(), &I18n::languageChanged, this, [this] {
        setWindowTitle(ui("媒体信息", "Media Info"));
        tabs_->setTabText(0, ui("概览", "Overview"));
        tabs_->setTabText(1, ui("原始数据", "Raw data"));
        if (!lastPath_.isEmpty())
            probe(lastPath_);
    });

    setStyleSheet(QStringLiteral(
        "QDialog { background: #232629; color: #f4f6f8; }"
        "QTabWidget::pane { background: #232629; border: 1px solid #3a4044; }"
        "QTabBar::tab { background: #2a2e32; color: #f4f6f8; padding: 6px 14px; }"
        "QTabBar::tab:selected { background: #31363b; color: #3daee9; }"
        "QWidget#infoOverview, QWidget#infoPage, QWidget#infoForm { background: #232629; }"
        "QScrollArea#infoScroll { background: #232629; border: none; }"
        "QPlainTextEdit { background: #1b1e20; color: #f4f6f8; border: none; "
        "font-family: 'DejaVu Sans Mono', monospace; }"));
    overview_->setObjectName(QStringLiteral("infoOverview"));
    paintDark(overview_);
    paintDark(this);
}

void MediaInfoDialog::probe(const QString &mediaPath)
{
    lastPath_ = mediaPath;
    status_->setText(ui("正在用 ffprobe 读取…", "Reading with ffprobe…"));
    status_->show();
    tabs_->hide();
    raw_->clear();
    showDetails(MediaProbe::inspect(mediaPath));
}

void MediaInfoDialog::showDetails(const MediaDetails &details)
{
    if (!details.error.isEmpty()) {
        status_->setText(details.error);
        status_->show();
        tabs_->hide();
        return;
    }
    status_->hide();
    tabs_->show();

    auto *previous = overview_->layout()->takeAt(0);
    if (previous) {
        if (previous->widget())
            previous->widget()->deleteLater();
        delete previous;
    }
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("infoScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(buildOverview(details));
    paintDark(scroll);
    paintDark(scroll->viewport());
    overview_->layout()->addWidget(scroll);
    raw_->setPlainText(details.rawJson);
}
