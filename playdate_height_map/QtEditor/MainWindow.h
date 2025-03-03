#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QFileSystemWatcher>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void update();
    void up();
    void down();
    void left();
    void right();
    void a();
    void b();
    void onFileChanged(const QString& path);

private:
    Ui::MainWindow *ui;
    std::array<uint8_t, 52*240> buffer;
    QTimer* timer;
    QFileSystemWatcher* fileWatcher;
};
#endif // MAINWINDOW_H
