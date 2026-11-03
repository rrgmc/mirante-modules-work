// An example extension of the github module's extension point "runlayout": the
// runs of a commit as two tables in tabs, with coloured buttons. It uses only
// the model in GitHubRunLayout.h and Qt. See ../../README.md.
#include "GitHubRunLayout.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace mirante::modules::github {

namespace {

QColor colourOf(widgets::StatusRole role)
{
    switch (role) {
    case widgets::StatusRole::Good:
        return QColor(0x1a, 0x7f, 0x37);
    case widgets::StatusRole::Bad:
        return QColor(0xcf, 0x22, 0x2e);
    case widgets::StatusRole::Waiting:
        return QColor(0x9a, 0x67, 0x00);
    case widgets::StatusRole::Neutral:
        break;
    }
    return QColor(0x59, 0x63, 0x6e);
}

QTableWidgetItem *stateItem(const Pill &pill)
{
    auto *item = new QTableWidgetItem(pill.text);
    item->setForeground(QBrush(colourOf(pill.role)));
    return item;
}

QPushButton *colouredButton(const QString &text, const QString &colour, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: white; border-radius: 4px; "
                                         "padding: 4px 10px; } QPushButton:disabled { background: #8c959f; }")
                              .arg(colour));
    return button;
}

QTableWidget *newTable(const QStringList &headers, QWidget *parent)
{
    auto *table = new QTableWidget(0, int(headers.size()), parent);
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    return table;
}

class RunTableLayout : public QWidget
{
public:
    RunTableLayout(RunModel &model, QWidget *parent)
        : QWidget(parent)
        , m_model(model)
    {
        auto *layout = new QVBoxLayout(this);
        m_title = new QLabel(this);
        QFont font = m_title->font();
        font.setPointSizeF(font.pointSizeF() * 1.3);
        font.setBold(true);
        m_title->setFont(font);
        layout->addWidget(m_title);

        auto *buttons = new QHBoxLayout;
        m_rerunFailed =
            colouredButton(QCoreApplication::translate("github", "Re-run failed"), QStringLiteral("#cf222e"), this);
        m_rerun = colouredButton(QCoreApplication::translate("github", "Re-run"), QStringLiteral("#1a7f37"), this);
        m_cancel = colouredButton(QCoreApplication::translate("github", "Cancel"), QStringLiteral("#59636e"), this);
        m_page = colouredButton(QCoreApplication::translate("github", "Open page"), QStringLiteral("#0969da"), this);
        connect(m_rerunFailed, &QPushButton::clicked, this, [this] { m_model.rerunFailed(); });
        connect(m_rerun, &QPushButton::clicked, this, [this] { m_model.rerun(); });
        connect(m_cancel, &QPushButton::clicked, this, [this] { m_model.cancel(); });
        connect(m_page, &QPushButton::clicked, this, [this] { m_model.openPage(); });
        for (QPushButton *button : {m_rerunFailed, m_rerun, m_cancel, m_page})
            buttons->addWidget(button);
        buttons->addStretch(1);
        layout->addLayout(buttons);

        m_error = new QLabel(this);
        m_error->setStyleSheet(QStringLiteral("color: #cf222e"));
        layout->addWidget(m_error);

        auto *tabs = new QTabWidget(this);
        m_runs = newTable({QCoreApplication::translate("github", "Run"), QCoreApplication::translate("github", "State"),
                           QCoreApplication::translate("github", "Branch"),
                           QCoreApplication::translate("github", "Started"),
                           QCoreApplication::translate("github", "Duration")},
                          tabs);
        m_jobs = newTable({QCoreApplication::translate("github", "Job"), QCoreApplication::translate("github", "State"),
                           QCoreApplication::translate("github", "Duration")},
                          tabs);
        tabs->addTab(m_runs, QCoreApplication::translate("github", "Runs"));
        tabs->addTab(m_jobs, QCoreApplication::translate("github", "Jobs"));
        layout->addWidget(tabs, 1);
        connect(m_runs, &QTableWidget::cellClicked, this, [this](int row) { m_model.showRun(row); });
        connect(m_jobs, &QTableWidget::cellDoubleClicked, this, [this](int row) {
            const QList<RunModel::Job> jobs = m_model.jobs();
            if (row < jobs.size() && !jobs.at(row).log.isEmpty())
                QDesktopServices::openUrl(jobs.at(row).log);
        });

        connect(&model, &SummaryModel::changed, this, [this] { draw(); });
        connect(&model, &SummaryModel::busyChanged, this, [this] { drawButtons(); });
        connect(&model, &SummaryModel::errorChanged, this, [this] { m_error->setText(m_model.error()); });
        draw();
    }

private:
    void draw()
    {
        m_title->setText(m_model.hasData() ? m_model.title() : m_model.message());
        drawButtons();

        const QList<RunModel::Run> runs = m_model.runs();
        m_runs->setRowCount(int(runs.size()));
        for (int row = 0; row < runs.size(); ++row) {
            const RunModel::Run &run = runs.at(row);
            m_runs->setItem(row, 0, new QTableWidgetItem(run.title));
            m_runs->setItem(row, 1, stateItem(run.state));
            m_runs->setItem(row, 2, new QTableWidgetItem(run.branch));
            m_runs->setItem(row, 3, new QTableWidgetItem(run.started));
            m_runs->setItem(row, 4, new QTableWidgetItem(run.duration));
        }
        if (m_model.hasRuns())
            m_runs->selectRow(int(m_model.shownIndex()));

        const QList<RunModel::Job> jobs = m_model.jobs();
        m_jobs->setRowCount(int(jobs.size()));
        for (int row = 0; row < jobs.size(); ++row) {
            const RunModel::Job &job = jobs.at(row);
            m_jobs->setItem(row, 0, new QTableWidgetItem(job.name));
            m_jobs->setItem(row, 1, stateItem(job.state));
            m_jobs->setItem(row, 2, new QTableWidgetItem(job.duration));
        }
    }

    void drawButtons()
    {
        m_rerunFailed->setVisible(m_model.canRerunFailed());
        m_rerunFailed->setEnabled(!m_model.isBusy(RunModel::RerunFailedAction));
        m_rerun->setVisible(m_model.canRerun());
        m_rerun->setEnabled(!m_model.isBusy(RunModel::RerunAction));
        m_cancel->setVisible(m_model.canCancel());
        m_cancel->setEnabled(!m_model.isBusy(RunModel::CancelAction));
        m_page->setVisible(!m_model.pageAddress().isEmpty());
    }

    RunModel &m_model;
    QLabel *m_title = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_rerunFailed = nullptr;
    QPushButton *m_rerun = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_page = nullptr;
    QTableWidget *m_runs = nullptr;
    QTableWidget *m_jobs = nullptr;
};

} // namespace

QWidget *makeRunLayout(RunModel &model, QWidget *parent)
{
    return new RunTableLayout(model, parent);
}

} // namespace mirante::modules::github
