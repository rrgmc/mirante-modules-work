// The native view of a Jira issue. See README.md#views.
#pragma once

#include <mirante/widgets/StatusPill.h>

#include <QDateTime>

class QString;
class QUrl;
class QWidget;

namespace mirante {
class ItemContext;
}

namespace mirante::modules::jira {

QWidget *createIssueView(ItemContext &context, QWidget *parent);

// The colour of a status, from Jira's status category.
widgets::StatusRole statusCategoryRole(const QString &category);

// A date from Jira's answers, such as "2026-10-28T14:03:11.123-0300". Invalid
// if it cannot be read.
QDateTime readJiraDate(const QString &text);
// The same date as short local text, or the text itself if it cannot be read.
QString shortJiraDate(const QString &text);

// Shows url in place of the view, or in the external browser if Shift is held.
void openPage(ItemContext &context, const QUrl &url);

} // namespace mirante::modules::jira
