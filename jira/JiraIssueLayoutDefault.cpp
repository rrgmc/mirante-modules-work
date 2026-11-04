// The default of the extension point "issuelayout": a SummaryView with the
// issue's status and type, Details, Description, Comments, and Sub-tasks and
// links. It is compiled only when the app names no extension for
// "issuelayout". See README.md#layouts.
#include "JiraIssueLayout.h"

#include <mirante/widgets/ActionRow.h>
#include <mirante/widgets/FlowRow.h>
#include <mirante/widgets/LinkStyle.h>
#include <mirante/widgets/SummaryView.h>

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QScrollBar>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
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

    // The HTML if there is any, else the plain text, else fallback.
    void setContent(const RichContent &content, const QString &fallback = {})
    {
        if (!content.html.trimmed().isEmpty())
            setHtml(content.html);
        else if (!content.text.isEmpty())
            setPlainText(content.text);
        else
            setPlainText(fallback);
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

class IssueLayout : public widgets::SummaryView
{
public:
    IssueLayout(IssueModel &model, QWidget *parent)
        : widgets::SummaryView(parent)
        , m_model(model)
    {
        setObjectName(QStringLiteral("jira-issue-default-layout"));
        actions()->addLinkAction(kOpenPage, QCoreApplication::translate("jira", "Open page"),
                                 QCoreApplication::translate("jira", "Page"), [this] { m_model.openPage(); });
        for (const QString &section : {kDetails, kDescription, kComments, kLinks})
            setSectionVisible(section, model.isSectionShown(section));

        connect(&model, &IssueModel::changed, this, [this] { draw(); });
        draw();
    }

private:
    void draw()
    {
        if (!m_model.hasData()) {
            setMessage(m_model.message());
            return;
        }
        setMessage(QString());
        setTitle(m_model.title());
        setStateRow(makeStateRow());
        setSection(kDetails, QCoreApplication::translate("jira", "Details"), makeDetails());
        drawDescription();
        setSection(kComments, QCoreApplication::translate("jira", "Comments"), makeComments());
        setSection(kLinks, QCoreApplication::translate("jira", "Sub-tasks and links"), makeLinks());
    }

    QWidget *makeStateRow() const
    {
        auto *row = new QWidget;
        auto *flow = new widgets::FlowRow(row);
        const Pill status = m_model.status();
        if (!status.text.isEmpty())
            flow->addWidget(new widgets::StatusPill(status.text, status.role, row));
        const QString type = m_model.type();
        if (!type.isEmpty())
            flow->addWidget(new widgets::StatusPill(type, widgets::StatusRole::Neutral, row));
        return row;
    }

    QWidget *makeDetails() const
    {
        auto *details = new QWidget;
        auto *form = new QFormLayout(details);
        form->setContentsMargins(0, 0, 0, 0);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        for (const IssueModel::Detail &detail : m_model.details())
            form->addRow(plainLabel(detail.label, details), plainLabel(detail.value, details));
        return details;
    }

    // Changes the text in place, so that the height stays until the new one is
    // known, and the view keeps its scroll position.
    void drawDescription()
    {
        if (!m_description) {
            m_description = new RichText(m_model.siteAddress(), nullptr);
            setSection(kDescription, QCoreApplication::translate("jira", "Description"), m_description);
        }
        m_description->setContent(m_model.description(), QCoreApplication::translate("jira", "No description."));
    }

    QWidget *makeComments() const
    {
        const QList<IssueModel::Comment> comments = m_model.comments();
        auto *section = new QWidget;
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        if (comments.isEmpty())
            layout->addWidget(plainLabel(QCoreApplication::translate("jira", "No comments."), section));
        for (qsizetype i = 0; i < std::min<qsizetype>(comments.size(), kLatestComments); ++i) {
            const IssueModel::Comment &comment = comments.at(i);
            layout->addWidget(plainLabel(dotted(comment.author, comment.date), section));
            auto *body = new RichText(m_model.siteAddress(), section);
            body->setContent(comment.body);
            layout->addWidget(body);
        }
        return section;
    }

    QWidget *makeLinks() const
    {
        auto *section = new QWidget;
        auto *layout = new QVBoxLayout(section);
        layout->setContentsMargins(0, 0, 0, 0);
        const QList<IssueModel::LinkedIssue> issues = m_model.linkedIssues();
        if (issues.isEmpty())
            layout->addWidget(plainLabel(QCoreApplication::translate("jira", "No sub-tasks or links."), section));
        for (const IssueModel::LinkedIssue &issue : issues) {
            auto *row = new QHBoxLayout;
            row->addWidget(makeIssueButton(issue, section));
            row->addWidget(plainLabel(issue.text, section), 1);
            if (issue.child)
                row->addWidget(new widgets::StatusPill(issue.state.text, issue.state.role, section));
            layout->addLayout(row);
        }
        if (const int more = m_model.moreChildren(); more > 0) {
            QLabel *line = plainLabel(QCoreApplication::translate("jira", "%1 more").arg(more), section);
            line->setObjectName(QStringLiteral("childrenMore"));
            layout->addWidget(line);
        }
        return section;
    }

    // An issue's key, as a button to its page in the external browser.
    static QToolButton *makeIssueButton(const IssueModel::LinkedIssue &issue, QWidget *parent)
    {
        auto *button = new QToolButton(parent);
        button->setObjectName(QStringLiteral("issue:") + issue.key);
        button->setText(withoutShortcuts(issue.key));
        widgets::styleAsLink(button);
        const QUrl url = issue.address;
        button->setEnabled(!url.isEmpty());
        connect(button, &QToolButton::clicked, parent, [url] { QDesktopServices::openUrl(url); });
        return button;
    }

    IssueModel &m_model;
    RichText *m_description = nullptr;
};

} // namespace

QWidget *makeIssueLayout(IssueModel &model, QWidget *parent)
{
    return new IssueLayout(model, parent);
}

} // namespace mirante::modules::jira
