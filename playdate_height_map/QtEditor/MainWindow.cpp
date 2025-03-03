#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QTimer>
extern "C"
{
#include "display/display.h"
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(ui->upButton, SIGNAL(clicked()), this, SLOT(up()));
    connect(ui->downButton, SIGNAL(clicked()), this, SLOT(down()));
    connect(ui->leftButton, SIGNAL(clicked()), this, SLOT(left()));
    connect(ui->rightButton, SIGNAL(clicked()), this, SLOT(right()));
    connect(ui->aButton, SIGNAL(clicked()), this, SLOT(a()));
    connect(ui->bButton, SIGNAL(clicked()), this, SLOT(b()));
    memset(buffer.data(), 0, buffer.size());
    timer = new QTimer;
    timer->setInterval(20);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start();

    fileWatcher = new QFileSystemWatcher(this);
    fileWatcher->addPath("/home/perso/terrain.png");
    connect(fileWatcher, SIGNAL(fileChanged(const QString&)), this, SLOT(onFileChanged(const QString&)));
    onFileChanged("/home/perso/terrain.png");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::update()
{
    memset(buffer.data(), 0xFF, buffer.size());
    draw((uint8_t*)buffer.data());

    QImage image(QSize(400, 240), QImage::Format::Format_Grayscale8);

    uint8_t* source = buffer.data();
    uint8_t* dest = image.bits();

    for(size_t row = 0; row < 240; ++row)
    {
        for(size_t column = 0; column < 50; ++column)
        {
            uint8_t* sourcePointer = source + (row * 52 + column);
            uint8_t* destPointer = dest + (row * 400 + column * 8);
            destPointer[7] = (*sourcePointer & 0b00000001 ? 0xFF : 0x00);
            destPointer[6] = (*sourcePointer & 0b00000010 ? 0xFF : 0x00);
            destPointer[5] = (*sourcePointer & 0b00000100 ? 0xFF : 0x00);
            destPointer[4] = (*sourcePointer & 0b00001000 ? 0xFF : 0x00);
            destPointer[3] = (*sourcePointer & 0b00010000 ? 0xFF : 0x00);
            destPointer[2] = (*sourcePointer & 0b00100000 ? 0xFF : 0x00);
            destPointer[1] = (*sourcePointer & 0b01000000 ? 0xFF : 0x00);
            destPointer[0] = (*sourcePointer & 0b10000000 ? 0xFF : 0x00);
        }
    }

    ui->displayLabel->setPixmap(QPixmap::fromImage(image));
}

void MainWindow::up()
{
    upPushed();
}

void MainWindow::down()
{
    downPushed();
}

void MainWindow::left()
{
    leftPushed();
}

void MainWindow::right()
{
    rightPushed();
}

void MainWindow::a()
{
    aPushed();
}

void MainWindow::b()
{
    bPushed();
}

void MainWindow::onFileChanged(const QString& path)
{
    QPixmap pixmap;
    pixmap.load(path);
    QImage image = pixmap.toImage().convertToFormat(QImage::Format::Format_Grayscale8);

    if(!image.isNull() && image.width() == 1024 && image.height() == 1024)
    {
        setTerrainData(image.bits());
    }
}
