#include "JiraIssueView.h"

#include "JiraClient.h"
#include "JiraSettings.h"
#include "JiraStatus.h"
#include "JiraUrls.h"

#include <mirante/ItemContext.h>
#include <mirante/Status.h>
#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/CommentDialog.h>
#include <mirante/widgets/FlowRow.h>
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
#include <QMenu>
#include <QPointer>
#include <QScrollBar>
#include <QString>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace mirante::modules::jira {

namespace {

// How many comments the section shows, newest first.
constexpr int kLatestComments = 5;

const QString kChangeStatus = QStringLiteral("changeStatus");
const QString kComment = QStringLiteral("comment");
const QString kOpenPage = QStringLiteral("openPage");

const QString kDetails = QStringLiteral("details");
const QString kDescription = QStringLiteral("description");
const QString kComments = QStringLiteral("comments");
const QString kLinks = QStringLiteral("links");

QString trJira(const char *text)
{
    return QCoreApplication::translate("jira", text);
}

// A text from a server as a menu entry or a button, where "&" marks a shortcut.
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
        refs << readRef(subtask.toObject(), trJira("Sub-task"));

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

// The summary of one issue. A view draws from the item's status; a preview has
// no status of its own, so it reads the issue through the server, and reads it
// again after an action. See decision 0068.
class IssueView : public widgets::SummaryView
{
public:
    IssueView(ItemContext &context, const QString &previewKey, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_context(context)
        , m_previewKey(previewKey)
        , m_source(previewKey.isEmpty() ? context.status() : nullptr)
    {
        m_base = baseUrlOf(context.moduleSettings(kGeneralScreen));

        actions()->addMenuAction(kChangeStatus, trJira("Change status"), trJira("Status"),
                                 [this](QMenu &menu) { fillTransitions(menu); });
        actions()->addAction(kComment, trJira("Comment"), QString(), [this] { addComment(); });
        actions()->addAction(kOpenPage, trJira("Open page"), trJira("Page"),
                             [this] { openPage(m_context, pageUrl()); });
        actions()->setWebLink(pageUrl());
        applySectionFields();

        if (m_source) {
            m_source->subscribe(this, [this](const ItemStatus &status) { showStatus(status); });
            showStatus(m_source->current());
        } else {
            reload();
        }
    }

    // Reads the issue through the server, for a view without a status.
    void reload()
    {
        const std::optional<JiraClient> jira = client();
        if (!jira) {
            setMessage(trJira("This profile has no Jira server."));
            return;
        }
        if (key().isEmpty()) {
            setMessage(trJira("Set the issue key in the item's settings."));
            return;
        }
        if (m_drawn.isEmpty())
            setMessage(trJira("Loading…"));
        // Answers can overtake each other, and only the last one counts.
        const int reading = ++m_reading;
        jira->issue(
            key(), {}, this,
            [this, reading](const ApiReply &reply) {
                if (reading != m_reading)
                    return;
                if (reply.ok()) {
                    if (m_readFailed)
                        actions()->clearError();
                    m_readFailed = false;
                    setMessage(QString());
                    draw(reply.json.object());
                } else if (m_drawn.isEmpty()) {
                    setMessage(reply.message);
                } else {
                    m_readFailed = true;
                    actions()->showError(reply.message);
                }
            },
            summaryExpand());
    }

private:
    bool isPreview() const { return !m_previewKey.isEmpty(); }

    QString key() const
    {
        return isPreview() ? m_previewKey : m_context.config().value(QStringLiteral("key")).toString().trimmed();
    }

    QUrl pageUrl() const
    {
        return m_base.isEmpty() ? QUrl() : issueUrl(m_base, QJsonObject{{QStringLiteral("key"), key()}});
    }

    std::optional<JiraClient> client() const
    {
        ApiClient *server = m_context.server();
        if (!server)
            return std::nullopt;
        return JiraClient(*server, deploymentOf(m_context.moduleSettings(kGeneralScreen)));
    }

    void showStatus(const ItemStatus &status)
    {
        const QJsonObject issue = status.data.value(QStringLiteral("issue")).toObject();
        if (issue.isEmpty()) {
            setMessage(status.message.isEmpty() ? trJira("Loading…") : status.message);
            return;
        }
        // An error keeps the old data, and the cell header shows the error.
        setMessage(QString());
        draw(issue);
    }

    // A list's configuration has no such fields, so a preview shows every
    // section.
    void applySectionFields()
    {
        const QJsonObject config = m_context.config();
        setSectionVisible(kDetails, config.value(QStringLiteral("showDetails")).toBool(true));
        setSectionVisible(kDescription, config.value(QStringLiteral("showDescription")).toBool(true));
        setSectionVisible(kComments, config.value(QStringLiteral("showComments")).toBool(true));
        setSectionVisible(kLinks, config.value(QStringLiteral("showLinks")).toBool(true));
    }

    void draw(const QJsonObject &issue)
    {
        // A poll usually brings the same answer, and drawing would lose a
        // selection in the text.
        if (issue == m_drawn)
            return;
        m_drawn = issue;

        const QJsonObject fields = issue.value(QStringLiteral("fields")).toObject();
        const QString issueKey = issue.value(QStringLiteral("key")).toString();
        setTitle(issueKey + QLatin1Char(' ') + fields.value(QStringLiteral("summary")).toString());
        setStateRow(makeStateRow(fields));
        setSection(kDetails, trJira("Details"), makeDetails(issue));
        drawDescription(issue);
        setSection(kComments, trJira("Comments"), makeComments(issue));
        setSection(kLinks, trJira("Sub-tasks and links"), makeLinks(fields));
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
        addRow(trJira("Priority"),
               fields.value(QStringLiteral("priority")).toObject().value(QStringLiteral("name")).toString());
        addRow(trJira("Assignee"), assignee.isEmpty() ? trJira("Unassigned") : assignee);
        addRow(trJira("Reporter"), person(QStringLiteral("reporter")));
        addRow(trJira("Sprint"), sprintOf(issue));
        return details;
    }

    // Changes the text in place, so that the height stays until the new one is
    // known, and the view keeps its scroll position.
    void drawDescription(const QJsonObject &issue)
    {
        if (!m_description) {
            m_description = new RichText(m_base, nullptr);
            setSection(kDescription, trJira("Description"), m_description);
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
            m_description->setPlainText(trJira("No description."));
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
            layout->addWidget(plainLabel(trJira("No comments."), section));
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

    QWidget *makeLinks(const QJsonObject &fields)
    {
        auto *section = new QWidget;
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        const QList<IssueRef> refs = readRefs(fields);
        if (refs.isEmpty())
            layout->addWidget(plainLabel(trJira("No sub-tasks or links."), section));
        for (const IssueRef &ref : refs) {
            auto *row = new QHBoxLayout;
            auto *button = new QToolButton(section);
            button->setObjectName(QStringLiteral("issue:") + ref.key);
            button->setText(withoutShortcuts(ref.key));
            button->setAutoRaise(true);
            const QUrl url = m_base.isEmpty()
                                 ? QUrl()
                                 : issueUrl(m_base, QJsonObject{{QStringLiteral("key"), ref.key}});
            button->setEnabled(isWebAddress(url));
            connect(button, &QToolButton::clicked, section, [url] { QDesktopServices::openUrl(url); });
            row->addWidget(button);
            row->addWidget(plainLabel(ref.text, section), 1);
            layout->addLayout(row);
        }
        return section;
    }

    // Asks for the transitions the issue allows now, each time the menu opens.
    void fillTransitions(QMenu &menu)
    {
        const std::optional<JiraClient> jira = client();
        if (!jira) {
            actions()->showError(trJira("This profile has no Jira server."));
            return;
        }
        menu.addAction(trJira("Loading…"))->setEnabled(false);
        QMenu *target = &menu;
        jira->transitions(key(), target, [this, target](const ApiReply &reply) {
            target->clear();
            if (!reply.ok()) {
                target->addAction(trJira("No status changes"))->setEnabled(false);
                actions()->showError(actionError(reply));
                return;
            }
            const QJsonArray transitions = reply.json.object().value(QStringLiteral("transitions")).toArray();
            if (transitions.isEmpty())
                target->addAction(trJira("No status changes"))->setEnabled(false);
            for (const QJsonValue &value : transitions) {
                const QJsonObject transition = value.toObject();
                const QString id = transition.value(QStringLiteral("id")).toString();
                QAction *entry =
                    target->addAction(withoutShortcuts(transition.value(QStringLiteral("name")).toString()));
                connect(entry, &QAction::triggered, this, [this, id] { moveTo(id); });
            }
        });
    }

    void moveTo(const QString &transitionId)
    {
        // A menu entry does not clear the last error, as a button does.
        actions()->clearError();
        act(kChangeStatus, [this, transitionId](const JiraClient &jira, JiraDone done) {
            jira.transition(key(), transitionId, this, std::move(done));
        });
    }

    void addComment()
    {
        // The dialog runs its own event loop, and the view can go meanwhile, as
        // when a change of configuration makes it again.
        const QPointer<IssueView> self(this);
        const std::optional<QString> text =
            widgets::askComment(this, trJira("Comment on %1").arg(key()));
        if (!self || !text)
            return;
        act(kComment, [this, text = *text](const JiraClient &jira, JiraDone done) {
            jira.comment(key(), text, this, std::move(done));
        });
    }

    // Runs one request of an action, with its button disabled meanwhile.
    void act(const QString &action, const std::function<void(const JiraClient &, JiraDone)> &send)
    {
        const std::optional<JiraClient> jira = client();
        if (!jira) {
            actions()->showError(trJira("This profile has no Jira server."));
            return;
        }
        actions()->setBusy(action, true);
        send(*jira, [this, action](const ApiReply &reply) {
            actions()->setBusy(action, false);
            if (!reply.ok()) {
                actions()->showError(actionError(reply));
                return;
            }
            // The cell, the badges and the view change together. A list reads
            // again after this as well, so a preview follows it.
            if (ItemStatusSource *status = m_context.status())
                status->refresh();
            if (!m_source)
                reload();
        });
    }

    static QString actionError(const ApiReply &reply)
    {
        if (reply.error == ApiReply::Error::Forbidden)
            return trJira("The token cannot do this: %1").arg(reply.message);
        return reply.message;
    }

    ItemContext &m_context;
    QString m_previewKey;
    ItemStatusSource *m_source;
    QUrl m_base;
    QJsonObject m_drawn;
    RichText *m_description = nullptr;
    int m_reading = 0;
    bool m_readFailed = false;
};

} // namespace

QWidget *createIssueView(ItemContext &context, QWidget *parent)
{
    return new IssueView(context, QString(), parent);
}

QWidget *createIssuePreview(ItemContext &context, const QString &key, QWidget *parent)
{
    return new IssueView(context, key, parent);
}

void reloadIssuePreview(QWidget *preview)
{
    if (auto *view = dynamic_cast<IssueView *>(preview))
        view->reload();
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
