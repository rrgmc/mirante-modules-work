#include "JiraIssueView.h"

#include "JiraFields.h"
#include "JiraSettings.h"
#include "JiraStatus.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/LinkStyle.h>
#include <mirante/widgets/SummaryView.h>

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QList>
#include <QLocale>
#include <QScrollBar>
#include <QString>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace mirante::modules::jira {

namespace {

// How many comments the section shows, newest first.
constexpr int kLatestComments = 5;

const QString kOpenPage = QStringLiteral("openPage");

const QString kDetails = QStringLiteral("details");
const QString kDescription = QStringLiteral("description");
const QString kComments = QStringLiteral("comments");
const QString kLinks = QStringLiteral("links");

// A text from a server as a button, where "&" marks a shortcut.
QString withoutShortcuts(QString text)
{
    return text.replace(QLatin1Char('&'), QLatin1String("&&"));
}

// A label for text from a server, which wraps and is never read as markup.
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setText(text);
    return label;
}

QString dotted(const QString &first, const QString &second)
{
    if (first.isEmpty() || second.isEmpty())
        return first + second;
    return first + QStringLiteral(" ") + QChar(0x00B7) + QStringLiteral(" ") + second;
}

// Only pages on the web open from here. An address can come from a server, and
// opening another scheme could start a program.
bool isWebAddress(const QUrl &url)
{
    return url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http");
}

// HTML that Jira rendered, which holds text that anyone with access to the
// issue wrote. It loads nothing, so it cannot call out or show tracking images,
// and it opens only web links, in the external browser. It grows to its text,
// and the view around it scrolls.
class RichText : public QTextBrowser
{
public:
    RichText(const QUrl &baseUrl, QWidget *parent)
        : QTextBrowser(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        viewport()->setAutoFillBackground(false);
        document()->setDocumentMargin(0);

        // A relative link is below the base, also on a site installed at a path.
        QUrl base = baseUrl;
        if (!base.path().endsWith(QLatin1Char('/')))
            base.setPath(base.path() + QLatin1Char('/'));

        connect(this, &QTextBrowser::anchorClicked, this, [base](const QUrl &link) {
            const QUrl target = base.resolved(link);
            if (isWebAddress(target))
                QDesktopServices::openUrl(target);
        });
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
                [this] { fitHeight(); });
    }

    // Never answers with nothing, because the document would then read a file
    // or network path in the HTML itself.
    QVariant loadResource(int type, const QUrl &name) override
    {
        Q_UNUSED(name)
        if (type == QTextDocument::ImageResource) {
            QImage blank(1, 1, QImage::Format_ARGB32);
            blank.fill(Qt::transparent);
            return blank;
        }
        return QString(QLatin1Char(' '));
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QTextBrowser::resizeEvent(event);
        fitHeight();
    }

private:
    void fitHeight()
    {
        int height = int(std::ceil(document()->size().height())) + 2 * frameWidth();
        if (horizontalScrollBar()->isVisible())
            height += horizontalScrollBar()->height();
        setFixedHeight(height);
    }
};

// A line of the sub-tasks and links section.
struct IssueRef
{
    QString key;
    QString text;
};

// An issue named in the sub-tasks or links: its summary and status, after the
// relation it has to this one.
IssueRef readRef(const QJsonObject &issue, const QString &relation)
{
    const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
    const QString status =
        fields.value(QStringLiteral("status")).toObject().value(QStringLiteral("name")).toString();
    const QString summary = fields.value(QStringLiteral("summary")).toString();
    return {issue.value(QStringLiteral("key")).toString(), dotted(relation, dotted(summary, status))};
}

QList<IssueRef> readRefs(const QJsonObject &fields)
{
    QList<IssueRef> refs;
    const QJsonArray subtasks = fields.value(QStringLiteral("subtasks")).toArray();
    for (const QJsonValue &subtask : subtasks)
        refs << readRef(subtask.toObject(), QCoreApplication::translate("jira", "Sub-task"));

    const QJsonArray links = fields.value(QStringLiteral("issuelinks")).toArray();
    for (const QJsonValue &value : links) {
        const QJsonObject link = value.toObject();
        const QJsonObject type = link.value(QStringLiteral("type")).toObject();
        if (link.contains(QStringLiteral("outwardIssue")))
            refs << readRef(link.value(QStringLiteral("outwardIssue")).toObject(),
                            type.value(QStringLiteral("outward")).toString());
        else if (link.contains(QStringLiteral("inwardIssue")))
            refs << readRef(link.value(QStringLiteral("inwardIssue")).toObject(),
                            type.value(QStringLiteral("inward")).toString());
    }
    return refs;
}

// The summary of one issue, drawn from the item's status. See decision 0070.
class IssueView : public widgets::SummaryView
{
public:
    IssueView(ItemContext &context, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_context(context)
    {
        m_base = baseUrlOf(context.moduleSettings(kGeneralScreen));

        actions()->addLinkAction(kOpenPage, QCoreApplication::translate("jira", "Open page"),
                                 QCoreApplication::translate("jira", "Page"),
                                 [this] { openPage(m_context, pageUrl()); });
        actions()->setWebLink(pageUrl());
        applySectionFields();

        if (ItemStatusSource *source = context.status()) {
            source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
            showStatus(source->current());
        } else {
            setMessage(QCoreApplication::translate("jira", "Loading…"));
        }
    }

private:
    QString key() const { return m_context.config().value(QStringLiteral("key")).toString().trimmed(); }

    QUrl pageUrl() const
    {
        return m_base.isEmpty() ? QUrl() : issueUrl(m_base, QJsonObject{{QStringLiteral("key"), key()}});
    }

    void showStatus(const ItemStatus &status)
    {
        const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();
        if (issue.isEmpty()) {
            setMessage(status.message.isEmpty() ? QCoreApplication::translate("jira", "Loading…") : status.message);
            return;
        }
        // An error keeps the old data, and the cell header shows the error.
        setMessage(QString());
        draw(status.data);
    }

    void applySectionFields()
    {
        const QJsonObject config = m_context.config();
        setSectionVisible(kDetails, config.value(QStringLiteral("showDetails")).toBool(true));
        setSectionVisible(kDescription, config.value(QStringLiteral("showDescription")).toBool(true));
        setSectionVisible(kComments, config.value(QStringLiteral("showComments")).toBool(true));
        setSectionVisible(kLinks, config.value(QStringLiteral("showLinks")).toBool(true));
    }

    void draw(const QJsonObject &answer)
    {
        // A poll usually brings the same answer, and drawing would lose a
        // selection in the text.
        if (answer == m_drawn)
            return;
        m_drawn = answer;

        const QJsonObject issue = answer.value(QStringLiteral("issue")).toObject();
        const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
        const QString issueKey = issue.value(QStringLiteral("key")).toString();
        setTitle(issueKey + QLatin1Char(' ') + fields.value(QStringLiteral("summary")).toString());
        setStateRow(makeStateRow(fields));
        setSection(kDetails, QCoreApplication::translate("jira", "Details"), makeDetails(issue));
        drawDescription(issue);
        setSection(kComments, QCoreApplication::translate("jira", "Comments"), makeComments(issue));
        setSection(kLinks, QCoreApplication::translate("jira", "Sub-tasks and links"), makeLinks(fields, answer));
    }

    QWidget *makeStateRow(const QJsonObject &fields)
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        const QJsonObject status = fields.value(QStringLiteral("status")).toObject();
        const QString category =
            status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
        const QString statusName = status.value(QStringLiteral("name")).toString();
        if (!statusName.isEmpty())
            flow->addWidget(new widgets::StatusPill(statusName, statusCategoryRole(category), row));
        const QString type =
            fields.value(QStringLiteral("issuetype")).toObject().value(QStringLiteral("name")).toString();
        if (!type.isEmpty())
            flow->addWidget(new widgets::StatusPill(type, widgets::StatusRole::Neutral, row));
        return row;
    }

    QWidget *makeDetails(const QJsonObject &issue)
    {
        const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
        const auto person = [&fields](const QString &field) {
            return fields.value(field).toObject().value(QStringLiteral("displayName")).toString();
        };
        const QString assignee = person(QStringLiteral("assignee"));

        auto *details = new QWidget;
        auto *form = new QFormLayout(details);
        form->setContentsMargins(0, 0, 0, 0);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        const auto addRow = [form, details](const QString &label, const QString &value) {
            if (!value.isEmpty())
                form->addRow(label, plainLabel(value, details));
        };
        addRow(QCoreApplication::translate("jira", "Priority"),
               fields.value(QStringLiteral("priority")).toObject().value(QStringLiteral("name")).toString());
        addRow(QCoreApplication::translate("jira", "Assignee"),
               assignee.isEmpty() ? QCoreApplication::translate("jira", "Unassigned") : assignee);
        addRow(QCoreApplication::translate("jira", "Reporter"), person(QStringLiteral("reporter")));
        addRow(QCoreApplication::translate("jira", "Sprint"), sprintOf(issue));

        // A name the site does not have is left out quietly. See decision 0073.
        const QJsonObject names = issue.value(QStringLiteral("names")).toObject();
        const QStringList extraFields = extraFieldsOf(m_context.moduleSettings(kGeneralScreen));
        for (const QString &name : extraFields) {
            const QString value = fieldText(fields.value(fieldIdByName(names, name)));
            if (!value.isEmpty())
                form->addRow(plainLabel(name, details), plainLabel(value, details));
        }
        return details;
    }

    // Changes the text in place, so that the height stays until the new one is
    // known, and the view keeps its scroll position.
    void drawDescription(const QJsonObject &issue)
    {
        if (!m_description) {
            m_description = new RichText(m_base, nullptr);
            setSection(kDescription, QCoreApplication::translate("jira", "Description"), m_description);
        }
        const QString html = issue.value(QStringLiteral("renderedFields"))
                                 .toObject()
                                 .value(QStringLiteral("description"))
                                 .toString();
        const QJsonValue raw = issue.value(QStringLiteral("fields")).toObject().value(QStringLiteral("description"));
        if (!html.trimmed().isEmpty())
            m_description->setHtml(html);
        else if (raw.isString() && !raw.toString().isEmpty())
            m_description->setPlainText(raw.toString());
        else
            m_description->setPlainText(QCoreApplication::translate("jira", "No description."));
    }

    QWidget *makeComments(const QJsonObject &issue)
    {
        const QJsonArray rendered = issue.value(QStringLiteral("renderedFields"))
                                  .toObject()
                                  .value(QStringLiteral("comment"))
                                  .toObject()
                                  .value(QStringLiteral("comments"))
                                  .toArray();
        QHash<QString, QString> htmlById;
        for (const QJsonValue &value : rendered) {
            const QJsonObject comment = value.toObject();
            htmlById.insert(comment.value(QStringLiteral("id")).toString(),
                            comment.value(QStringLiteral("body")).toString());
        }

        const QJsonArray all = issue.value(QStringLiteral("fields"))
                                   .toObject()
                                   .value(QStringLiteral("comment"))
                                   .toObject()
                                   .value(QStringLiteral("comments"))
                                   .toArray();
        QList<QJsonObject> comments;
        for (const QJsonValue &value : all)
            comments << value.toObject();
        std::stable_sort(comments.begin(), comments.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return readJiraDate(a.value(QStringLiteral("created")).toString())
                   > readJiraDate(b.value(QStringLiteral("created")).toString());
        });

        auto *section = new QWidget;
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        if (comments.isEmpty())
            layout->addWidget(plainLabel(QCoreApplication::translate("jira", "No comments."), section));
        for (qsizetype i = 0; i < std::min<qsizetype>(comments.size(), kLatestComments); ++i) {
            const QJsonObject &comment = comments.at(i);
            const QString author =
                comment.value(QStringLiteral("author")).toObject().value(QStringLiteral("displayName")).toString();
            const QString date = shortJiraDate(comment.value(QStringLiteral("created")).toString());
            layout->addWidget(plainLabel(dotted(author, date), section));

            auto *body = new RichText(m_base, section);
            const QString html = htmlById.value(comment.value(QStringLiteral("id")).toString());
            const QJsonValue raw = comment.value(QStringLiteral("body"));
            if (!html.trimmed().isEmpty())
                body->setHtml(html);
            else if (raw.isString())
                body->setPlainText(raw.toString());
            layout->addWidget(body);
        }
        return section;
    }

    QWidget *makeLinks(const QJsonObject &fields, const QJsonObject &answer)
    {
        auto *section = new QWidget;
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        const QList<IssueRef> refs = readRefs(fields);
        const QJsonArray children = answer.value(QStringLiteral("children")).toArray();
        if (refs.isEmpty() && children.isEmpty())
            layout->addWidget(plainLabel(QCoreApplication::translate("jira", "No sub-tasks or links."), section));
        for (const IssueRef &ref : refs) {
            auto *row = new QHBoxLayout;
            row->addWidget(makeIssueButton(ref.key, section));
            row->addWidget(plainLabel(ref.text, section), 1);
            layout->addLayout(row);
        }
        for (const QJsonValue &value : children) {
            const QJsonObject child = value.toObject();
            const QJsonObject childFields = child.value(QStringLiteral("fields")).toObject();
            const QJsonObject status = childFields.value(QStringLiteral("status")).toObject();
            const QString category =
                status.value(QStringLiteral("statusCategory")).toObject().value(QStringLiteral("key")).toString();
            auto *row = new QHBoxLayout;
            row->addWidget(makeIssueButton(child.value(QStringLiteral("key")).toString(), section));
            row->addWidget(plainLabel(childFields.value(QStringLiteral("summary")).toString(), section), 1);
            row->addWidget(new widgets::StatusPill(status.value(QStringLiteral("name")).toString(),
                                                   statusCategoryRole(category), section));
            layout->addLayout(row);
        }
        const int more = answer.value(QStringLiteral("childrenTotal")).toInt() - int(children.size());
        if (more > 0) {
            QLabel *line = plainLabel(QCoreApplication::translate("jira", "%1 more").arg(more), section);
            line->setObjectName(QStringLiteral("childrenMore"));
            layout->addWidget(line);
        }
        return section;
    }

    // An issue's key, as a button to its page in the external browser.
    QToolButton *makeIssueButton(const QString &key, QWidget *parent)
    {
        auto *button = new QToolButton(parent);
        button->setObjectName(QStringLiteral("issue:") + key);
        button->setText(withoutShortcuts(key));
        widgets::styleAsLink(button);
        const QUrl url = m_base.isEmpty() ? QUrl() : issueUrl(m_base, QJsonObject{{QStringLiteral("key"), key}});
        button->setEnabled(isWebAddress(url));
        connect(button, &QToolButton::clicked, parent, [url] { QDesktopServices::openUrl(url); });
        return button;
    }

    ItemContext &m_context;
    QUrl m_base;
    QJsonObject m_drawn;
    RichText *m_description = nullptr;
};

} // namespace

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    return new IssueView(context, parent);
}

widgets::StatusRole statusCategoryRole(const QString &category)
{
    if (category == QLatin1String("done"))
        return widgets::StatusRole::Good;
    if (category == QLatin1String("indeterminate"))
        return widgets::StatusRole::Waiting;
    return widgets::StatusRole::Neutral;
}

QDateTime readJiraDate(const QString &text)
{
    return QDateTime::fromString(text, Qt::ISODateWithMs);
}

QString shortJiraDate(const QString &text)
{
    const QDateTime date = readJiraDate(text);
    return date.isValid() ? QLocale().toString(date.toLocalTime(), QLocale::ShortFormat) : text;
}

void openPage(ItemContext &context, const QUrl &url)
{
    if (url.isEmpty())
        return;
    if (QGuiApplication::keyboardModifiers().testFlag(Qt::ShiftModifier))
        QDesktopServices::openUrl(url);
    else
        context.showPage(url);
}

} // namespace mirante::modules::jira
