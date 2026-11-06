#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>

QString loadStyleSheet(const QString &path) {
    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    return stream.readAll();
}

int main(int argc, char *argv[]) {
    QApplication a(argc, argv);

    QString stylePath = ":/stylesheet/dark_gateway.qss";
    QString styleSheet = loadStyleSheet(stylePath);

    if (!styleSheet.isEmpty()) {
        // 将样式表应用到整个应用程序
        a.setStyleSheet(styleSheet);
    } else {
        qDebug()<<"qss加载失败";
    }

    MainWindow w;
    w.show();
    return a.exec();

}
