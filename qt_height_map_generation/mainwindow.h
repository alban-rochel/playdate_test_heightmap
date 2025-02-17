#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPixmap>

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

protected slots:
    void loadColorButtonClicked();
    void loadHeightButtonClicked();
    void exportClicked();

private:
    Ui::MainWindow *ui;
    QPixmap colorMap;
    QPixmap heightMap;

};
#endif // MAINWINDOW_H
