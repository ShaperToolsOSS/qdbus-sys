// Copyright (C) 2026 Shaper Tools
// SPDX-License-Identifier: LGPL-2.1-only

#include <QDebug>
#include <QGuiApplication>
#include <QSettings>
#include <QQuickView>
#include <QQmlContext>
#include <QLoggingCategory>
#include <QDirIterator>

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName("shapertools");
    QCoreApplication::setOrganizationDomain("shapertools.com");
    QCoreApplication::setApplicationName("iwd-qt");

    QSettings settings;
    QString additionalRules = settings.value("additionalLoggingRules", "").toString();

    // export QT_LOGGING_RULES=shapertools.iwdmodel=true
    QString envLoggingRules = qEnvironmentVariable("QT_LOGGING_RULES");
    if (envLoggingRules.isEmpty()) {
        QString envLoggingRulesAppend = qEnvironmentVariable("QT_LOGGING_RULES_APPEND");
        QString rules = QString("*.debug=false\n"
                         "*.info=false\n"
                         "qml=true\n"
                         "default.debug=true\n"
                         "%1\n"
                         "%2\n"
                        ).arg(additionalRules)
                         .arg(envLoggingRulesAppend);
        qDebug() << "Effective logging rules:" << rules;
        QLoggingCategory::setFilterRules(rules);
    } else {
        qDebug() << "Effective logging rules:" << envLoggingRules;
    }

    /*
    QDirIterator it(":", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        qDebug() << it.next();
    }*/

    QGuiApplication app(argc, argv);
    QQuickView quickView;
    quickView.setResizeMode(QQuickView::SizeRootObjectToView);
#ifdef ARM
    quickView.showFullScreen();
#else
    quickView.resize(800, 480);
    quickView.show();
#endif
    quickView.setSource(QUrl::fromLocalFile(":/qml/iwd-ui.qml"));
    app.exec();
}
