#include <vsgocct/StepModelLoader.h>

#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
QString resolveStepFile(const QStringList& arguments)
{
    if (arguments.size() > 1)
    {
        return arguments.at(1);
    }

    const QString initialDirectory = std::filesystem::exists("D:/OCCT/data/step")
                                         ? QStringLiteral("D:/OCCT/data/step")
                                         : QString();

    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open STEP Model"),
        initialDirectory,
        QStringLiteral("STEP Files (*.step *.stp *.STEP *.STP);;All Files (*.*)"));
}

vsgQt::Window* createRenderWindow(
    const vsg::ref_ptr<vsgQt::Viewer>& viewer,
    const vsg::ref_ptr<vsg::WindowTraits>& traits,
    const vsgocct::StepSceneData& sceneData)
{
    auto* window = new vsgQt::Window(viewer, traits, static_cast<QWindow*>(nullptr));
    window->setTitle(QString::fromStdString(traits->windowTitle));
    window->initializeWindow();

    if (!traits->device)
    {
        traits->device = window->windowAdapter->getOrCreateDevice();
    }

    const auto width = traits->width;
    const auto height = traits->height;
    const auto radius = std::max(sceneData.radius, 1.0);
    const auto centre = sceneData.center;

    auto lookAt = vsg::LookAt::create(
        centre + vsg::dvec3(0.0, -radius * 3.0, radius * 1.6),
        centre,
        vsg::dvec3(0.0, 0.0, 1.0));
    auto projection = vsg::Perspective::create(
        35.0,
        static_cast<double>(width) / static_cast<double>(height),
        std::max(radius * 0.001, 0.001),
        radius * 12.0);
    auto camera = vsg::Camera::create(projection, lookAt, vsg::ViewportState::create(VkExtent2D{width, height}));

    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(*window);

    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);
    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph});

    return window;
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        QApplication application(argc, argv);

        const QString stepFile = resolveStepFile(application.arguments());
        if (stepFile.isEmpty())
        {
            return 0;
        }

        auto sceneData = vsgocct::loadStepScene(std::filesystem::path(stepFile.toStdWString()));
        std::cout << "Loaded " << sceneData.triangleCount << " triangles from "
                  << stepFile.toLocal8Bit().constData() << std::endl;

        auto viewer = vsgQt::Viewer::create();

        auto traits = vsg::WindowTraits::create();
        traits->windowTitle = "vsgQt OCCT STEP Viewer";
        traits->width = 1280;
        traits->height = 900;
        traits->samples = VK_SAMPLE_COUNT_4_BIT;

        QMainWindow mainWindow;
        auto* renderWindow = createRenderWindow(viewer, traits, sceneData);
        auto* container = QWidget::createWindowContainer(renderWindow, &mainWindow);
        container->setFocusPolicy(Qt::StrongFocus);

        mainWindow.setWindowTitle(QStringLiteral("STEP Viewer - %1").arg(QFileInfo(stepFile).fileName()));
        mainWindow.setCentralWidget(container);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));
        mainWindow.statusBar()->showMessage(
            QStringLiteral("Triangles: %1").arg(static_cast<qulonglong>(sceneData.triangleCount)));

        viewer->continuousUpdate = true;
        viewer->setInterval(16);
        viewer->compile();

        mainWindow.show();

        return application.exec();
    }
    catch (const vsg::Exception& ex)
    {
        std::cerr << "VSG exception: " << ex.message << " (result=" << ex.result << ')' << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("VSG Error"), QString::fromLocal8Bit(ex.message.c_str()));
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("STEP Viewer Error"), QString::fromLocal8Bit(ex.what()));
        return 1;
    }
}
