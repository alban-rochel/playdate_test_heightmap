#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <iostream>
#include <QFileDialog>
#include <fstream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    connect(ui->loadColorButton, SIGNAL(clicked()), this, SLOT(loadColorButtonClicked()));
    connect(ui->loadHeightButton, SIGNAL(clicked()), this, SLOT(loadHeightButtonClicked()));
    connect(ui->exportButton, SIGNAL(clicked()), this, SLOT(exportClicked()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::loadColorButtonClicked()
{
    QString res = QFileDialog::getOpenFileName(this, "Color map", QString(), "*.png");

    if(!res.isEmpty())
    {
        QPixmap pixmap;
        if(pixmap.load(res))
        {
            ui->colorLabel->setPixmap(pixmap.scaledToHeight(512));
            colorMap = pixmap;
        }
    }
}

void MainWindow::loadHeightButtonClicked()
{
    QString res = QFileDialog::getOpenFileName(this, "Height map", QString(), "*.png");

    if(!res.isEmpty())
    {
        QPixmap pixmap;
        if(pixmap.load(res))
        {
            ui->heightLabel->setPixmap(pixmap.scaledToHeight(512));
            heightMap = pixmap;
        }
    }
}

void MainWindow::exportClicked()
{
    if(heightMap.isNull() || colorMap.isNull())
        return;

    if(heightMap.size() != colorMap.size())
        return;

    QImage colorImg = colorMap.toImage().convertToFormat(QImage::Format::Format_Grayscale8);

    QImage heightImg = heightMap.toImage().convertToFormat(QImage::Format::Format_Grayscale8);

    std::vector<uint8_t> outBuffer(colorImg.width() * colorImg.height());
    const uint8_t* colorBufferPtr = colorImg.bits();
    const uint8_t* heightBufferPtr = heightImg.bits();
    uint8_t* outBufferPtr = outBuffer.data();

    uint8_t maxHeight = 0;
    for(uint32_t counter = 0; counter < colorImg.width() * colorImg.height(); ++counter, ++heightBufferPtr)
    {
        const uint8_t height = *heightBufferPtr;
        maxHeight = std::max(height, maxHeight);
    }

    heightBufferPtr = heightImg.bits();
    QImage outHeight(heightImg.size(), QImage::Format::Format_Grayscale8);
    QImage outColor(colorImg.size(), QImage::Format::Format_Grayscale8);
    uint8_t* outColorBufferPtr = outColor.bits();
    uint8_t* outHeightBufferPtr = outHeight.bits();

    for(uint32_t counter = 0; counter < colorImg.width() * colorImg.height(); ++counter, ++colorBufferPtr, ++heightBufferPtr, ++outBufferPtr, ++outColorBufferPtr, ++outHeightBufferPtr)
    {
        const uint8_t color = *colorBufferPtr;
        const uint8_t height = *heightBufferPtr;
        float heightF = height;
        heightF /= maxHeight;
        heightF *= 255;
        uint8_t outHeight = (uint8_t(heightF) & 0xF8);
        uint8_t outColor = ((color / 32) & 0x07);
        *outBufferPtr = outHeight | outColor;
        *outHeightBufferPtr = outHeight;
        *outColorBufferPtr = outColor*32;
    }

    QString res = QFileDialog::getSaveFileName(this, "Output", QString(), "*.terrain");
    if(!res.isEmpty())
    {
        std::ofstream stream(res.toStdString(), std::ios::binary);
        stream.write((const char*)outBuffer.data(), outBuffer.size());
        outHeight.save(res + ".height.png");
        outColor.save(res + ".color.png");
    }
}

